/*
 * Copyright (c) 2013 - 2014 Owain G. Ainsworth <oga@nicotinebsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * All section references in comments in this file are the:
 * Intel(R) Virtualization Technology For Directed I/O Architecture
 * Specification. September 2013.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/tree.h>

#include <machine/apicvar.h>
#include <machine/cpuvar.h>
#include <machine/bus.h>

#include <uvm/uvm_extern.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcivar.h>

#define DMAR_VER_REG	0x0

/* 10.4.2 Capability Register. */
#define DMAR_CAP_REG	0x8
#define DMAR_CAP_FL1GP		(1ULL<<56)
#define DMAR_CAP_DRD		(1ULL<<55)
#define DMAR_CAP_DWD		(1ULL<<54)
#define DMAR_CAP_MAMV_MASK	0x3fULL
#define DMAR_CAP_MAMV_SHIFT	48
#define DMAR_CAP_NFR_MASK	0xfULL
#define DMAR_CAP_NFR_SHIFT	40
#define DMAR_CAP_PSI		(1ULL<<39)
#define DMAR_CAP_SLLPS_MASK	0xfULL
#define DMAR_CAP_SLLPS_SHIFT	34
#define DMAR_CAP_FRO_MASK	(0x3ffULL)
#define DMAR_CAP_FRO_SHIFT	24
#define DMAR_CAP_ZLR		(1ULL<<22)
#define DMAR_CAP_MGAW_MASK	0x3fULL
#define DMAR_CAP_MGAW_SHIFT	16
#define DMAR_CAP_SAGAW_MASK	0x1fULL
#define DMAR_CAP_SAGAW_SHIFT	8
#define DMAR_CAP_CM		(1ULL<<7)
#define DMAR_CAP_PHMR		(1ULL<<6)
#define DMAR_CAP_PLMR		(1ULL<<5)
#define DMAR_CAP_RWBF		(1ULL<<4)
#define DMAR_CAP_AFL		(1ULL<<3)
#define DMAR_CAP_ND_MASK	0x7ULL
#define DMAR_CAP_ND_SHIFT	0
#define DMAR_CAP_ND_4BIT	0
#define DMAR_CAP_ND_6BIT	1
#define DMAR_CAP_ND_8BIT	2
#define DMAR_CAP_ND_10BIT	3
#define DMAR_CAP_ND_12BIT	4
#define DMAR_CAP_ND_14BIT	5
#define DMAR_CAP_ND_16BIT	6

/* 10.4.3 Extended Capability Register */
#define DMAR_ECAP_REG	0x10
#define DMAR_ECAP_PSS_MASK	0x1fULL
#define DMAR_ECAP_PSS_SHIFT	35
#define DMAR_ECAP_EAFS		(1ULL<<34)
#define DMAR_ECAP_NWFS		(1ULL<<33)
#define DMAR_ECAP_POT		(1ULL<<32)
#define DMAR_ECAP_SRS		(1ULL<<31)
#define DMAR_ECAP_ERS		(1ULL<<30)
#define DMAR_ECAP_PRS		(1ULL<<29)
#define DMAR_ECAP_PASID		(1ULL<<28)
#define DMAR_ECAP_DIS		(1ULL<<27)
#define DMAR_ECAP_NEST		(1ULL<<26)
#define DMAR_ECAP_MTS		(1ULL<<25)
#define DMAR_ECAP_ECS		(1ULL<<24)
#define DMAR_ECAP_MHMV_MASK	0xfULL
#define DMAR_ECAP_MHMV_SHIFT	20
#define DMAR_ECAP_IRO_MASK	0x3ffULL
#define DMAR_ECAP_IRO_SHIFT	8
#define DMAR_ECAP_SC		(1ULL<<7)
#define DMAR_ECAP_PT		(1ULL<<6)
#define DMAR_ECAP_EIM		(1ULL<<4)
#define DMAR_ECAP_IR		(1ULL<<3)
#define DMAR_ECAP_DT		(1ULL<<2)
#define DMAR_ECAP_QI		(1ULL<<1)
#define DMAR_ECAP_C		(1ULL<<0)

/* 10.4.4 Global Command Register. */
#define DMAR_GCMD_REG	0x18
#define DMAR_GCMD_TE		(1ULL<<31)
#define DMAR_GCMD_SRTP		(1ULL<<30)
#define DMAR_GCMD_SFL		(1ULL<<29)
#define DMAR_GCMD_EAFL		(1ULL<<28)
#define DMAR_GCMD_WBF		(1ULL<<27)
#define DMAR_GCMD_QIE		(1ULL<<26)
#define DMAR_GCMD_IRE		(1ULL<<25)
#define DMAR_GCMD_SITRP		(1ULL<<24)
#define DMAR_GCMD_CFI		(1ULL<<23)

/* 10.4.5 Global Status Register. */
#define DMAR_GSTS_REG	0x1C
#define DMAR_GSTS_TES		(1ULL<<31)
#define DMAR_GSTS_RTPS		(1ULL<<30)
#define DMAR_GSTS_FLS		(1ULL<<29)
#define DMAR_GSTS_AFLS		(1ULL<<28)
#define DMAR_GSTS_WBFS		(1ULL<<27)
#define DMAR_GSTS_QIES		(1ULL<<26)
#define DMAR_GSTS_IRES		(1ULL<<25)
#define DMAR_GSTS_ITRPS		(1ULL<<24)
#define DMAR_GSTS_CFIS		(1ULL<<23)

/* 10.4.6 Root Table Address Register. */
#define DMAR_RTADDR_REG	0x20
#define DMAR_DTADDR_RTT	(1ULL<<11) /* 1 for extended, 0 for normal */

/* 10.4.7 Context Command Register. */
#define DMAR_CCMD_REG	0x28
#define DMAR_CCMD_ICC		(1ULL<<63)
#define DMAR_CCMD_ICC		(1ULL<<63)
#define DMAR_CCMD_CIRG_MASK	(0x3ULL)
#define DMAR_CCMD_CIRG_SHIFT	61
#define DMAR_CCMD_CAIG_MASK	(0x3ULL)
#define DMAR_CCMD_CAIG_SHIFT	59
#define DMAR_CCMD_INVAL_GLOBAL		(1ULL)
#define DMAR_CCMD_INVAL_DOMAIN		(2ULL)
#define DMAR_CCMD_INVAL_DEVICE		(3ULL)
#define DMAR_CCMD_FM_MASK	(0x3ULL)
#define DMAR_CCMD_FM_SHIFT	32
#define DMAR_CCMD_SID_MASK	(0xffffULL)
#define DMAR_CCMD_SID_SHIFT	16
#define DMAR_CCMD_DID_MASK	(0xffffULL)
#define DMAR_CCMD_DID_SHIFT	0

/* 10.4.16 Protected Memory Enable Register. */
#define DMAR_PMEN_REG	0x64
#define DMAR_PMEN_EPM		(1<<31) /* enable/disable */
#define DMAR_PMEN_PRS		(1<<0)	/* status. */

/* 10.4.11 Fault Event Data Register. */
#define DMAR_FEDATA_REG		0x3c

/* 10.4.12 Fault Event Address Register. */
#define DMAR_FEADDR_REG		0x40

/* 10.4.12 Fault Event Upper Address Register. */
#define DMAR_FEUADDR_REG	0x44


int	 acpidmar_match(struct device *, void *, void *);
void	 acpidmar_attach(struct device *, struct device *, void *);

int	 acpidmar_validate(struct acpi_dmar *);
int	 acpidmar_validate_devscope(caddr_t, uint8_t);
int	 acpidmar_print(void *, const char *);
void	 acpidmar_pci_hook(pci_chipset_tag_t, struct pci_attach_args *);

int
acpidmar_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpi_table_header *hdr;

	/*
	 * If we do not have a table, it is not us
	 */
	if (aaa->aaa_table == NULL)
		return (0);

	/*
	 * If it is an DMAR table, we can attach
	 */
	hdr = (struct acpi_table_header *)aaa->aaa_table;
	if (memcmp(hdr->signature, DMAR_SIG, sizeof(DMAR_SIG) - 1) != 0)
		return (0);

	return (1);
}

int
acpidmar_validate_devscope(caddr_t devscope, uint8_t length)
{
	caddr_t		addr = devscope;

	while (addr < devscope + length) {
		struct acpidmar_devscope *scope =
		    (struct acpidmar_devscope *)addr;
		uint8_t scopelen = scope->length;

		if (addr + scopelen > devscope + length)
			return (0);

		addr += scopelen;
	}
		
	return (1);
}

int
acpidmar_validate(struct acpi_dmar *dmar)
{
	caddr_t addr = (caddr_t)(dmar + 1);

	while (addr < (caddr_t)dmar + dmar->hdr.length) {
		union acpidmar_entry *entry = (union acpidmar_entry *)addr;
		u_int8_t length = entry->length;

		if (length < 2)
			return (0);

		if (addr + length > (caddr_t)dmar + dmar->hdr.length)
			return (0);

		switch (entry->type) {
		case DMAR_DRHD:
			if (length < sizeof(entry->drhd))
				return (0);
			if (!acpidmar_validate_devscope(addr +
			    sizeof(entry->drhd), length - sizeof(entry->drhd)))
				return (0);
			break;
		case DMAR_RMRR:
			if (length < sizeof(entry->rmrr))
				return (0);
			if (!acpidmar_validate_devscope(addr +
			    sizeof(entry->rmrr), length - sizeof(entry->rmrr)))
				return (0);

			break;
		case DMAR_ATSR:
			if (length < sizeof(entry->atsr))
				return (0);
			if (!acpidmar_validate_devscope(addr +
			    sizeof(entry->atsr), length - sizeof(entry->atsr)))
				return (0);
			break;
		case DMAR_RHSA:
			if (length != sizeof(entry->rhsa))
				return (0);
			break;
		default:
			/*
			 * Specification states that unknown types are to be
			 * skipped.
			 */
			 break;
		}

		addr += length;
	}

	return (1);
}

void acpidmar_print_devscope(caddr_t, uint8_t);
void
acpidmar_print_devscope(caddr_t devscope, uint8_t length)
{
	caddr_t			 addr = devscope;
	int			 first = 1;


	while (addr < devscope + length) {
		struct acpidmar_devscope	*scope =
		    (struct acpidmar_devscope *)addr;
		caddr_t			  	 path =
		    addr + sizeof(*scope);
		uint8_t				 scopelen = scope->length;

		if (!first) {
			printf(",");
		}
		first = 0;

		switch (scope->type) {
		case DMAR_ENDPOINT:
			printf(" devscope endpoint: start busno %d (", scope->bus);
			break;
		case DMAR_BRIDGE:
			printf(" devscope bridge: (");
			break;
		case DMAR_IOAPIC:
			printf(" devscope ioapic: apicid %d (", scope->enumid);
			break;
		case DMAR_HPET:
			printf(" devscope hpet: hpetno: %d (", scope->enumid);
			break;
		case DMAR_NAMESPACE:
			printf(" devscope namespace: acpi devno: %d (",
			    scope->enumid);
			break;
		default:
			printf("unknown type %d! (", scope->type);
			break;
		}

		scopelen -= sizeof(*scope);
		if (scopelen % 2) {
			printf("path length isn't divisible by two %d\n",
			    scopelen);
			return;
		}

		first = 1;
		while (scopelen > 0) {
			printf(" %d:%d", path[0], path[1]);
			path += 2;
			scopelen -= 2;
			first = 0;
		}
		printf(")");

		addr += scope->length;
	}
}

/*
 * Section 9.1
 * Entry in root_table. These point to the context_tables for a bus.
 */
#define RE_PRESENT	(1ULL<<0)
struct root_entry {
	uint64_t	 high;
	uint64_t	 low;
};

static inline bool
root_entry_is_valid(struct root_entry *entry)
{
	return (entry->low & RE_PRESENT);
}

static inline paddr_t
root_entry_get_context_pointer(struct root_entry *entry)
{
	return (entry->low & ~(paddr_t)0xfffULL);
}

static inline struct root_entry
make_root_entry(paddr_t context_table)
{

	if (context_table & 0xfff) {
		panic("%s: context table %llx not 4kb aligned", __func__,
			(long long)context_table);
	}

	struct root_entry ret = {
		.high = 0,
		.low = context_table | RE_PRESENT,
	};

	return (ret);
}

struct root_table {
	struct root_entry	entries[256];
};

/*
 * Section 9.3 Context Entry an entry in the context table.
 * These detail the domain, address width and page table pointers for a
 * bus/device/function triplet.
 */
#define	CTX_AW30BIT		(0ULL<<0)
#define	CTX_AW39BIT		(1ULL<<0)
#define	CTX_AW48BIT		(1ULL<<1)
#define	CTX_AW57BIT		((1ULL<<1)|(1ULL<<0))
#define	CTX_AW64BIT		(1ULL<<2)
#define CTX_AWMASK		(7)
#define CTX_DOMAIN_MASK		((1ULL<<16)-1)
#define CTX_DOMAIN_SHIFT	(8)
#define	CTX_TT_TRANSLATE	(0ULL<<0)
#define	CTX_TT_FULLTRANSLATE	(1ULL<<0)
#define	CTX_TT_PASSTHROUGH	(1ULL<<1)
#define CTX_TT_MASK		(3ULL)
#define CTX_TT_SHIFT		(2)
#define CTX_FPD			(1ULL<<1)
#define CTX_PRESENT		(1ULL<<0)
#define CTX_SLPTPTR_MASK	(~0xfffULL)
struct context_entry {
	uint64_t	high;
	uint64_t	low;
};

static inline struct context_entry
make_context_entry(uint16_t domain_id,
    uint8_t address_width, uint64_t /* XXX paddr_t? */ slptptr,
    uint8_t translation_type)
{
	/* slptptr must be 12 bit aligned */
	if (slptptr & 0xfff)
		panic("%s: unaligned slptptr %llx", __func__, slptptr);

	struct context_entry ret =  {
		/* 64:66 aw, 72:87 domain id */
		.high = (uint64_t)(address_width & CTX_AWMASK) |
		    (domain_id << CTX_DOMAIN_SHIFT),
		/*
		 * 12:63 second level page table, 3:2 translation type.
		 * 1 fault processing disable, 0 present
		 */
		.low = slptptr |
		    ((translation_type & CTX_TT_MASK) << CTX_TT_SHIFT) |
		    CTX_PRESENT,
	};

	return (ret);
}

#ifdef notyet
static inline bool
context_get_present(struct context_entry *ce)
{
	return (ce->low & CTX_PRESENT);
}
#endif

#ifdef notyet
static inline uint8_t 
context_get_translation_type(struct context_entry *ce)
{
	return ((ce->low >> CTX_TT_SHIFT) & CTX_TT_MASK);
}
#endif

#ifdef notyet
static inline uint8_t 
context_get_address_width(struct context_entry *ce)
{
	return (ce->high & CTX_AWMASK);
}
#endif

#ifdef notyet
static inline uint16_t
context_get_domain_id(struct context_entry *ce)
{
	return ((ce->high >> CTX_DOMAIN_SHIFT) & CTX_DOMAIN_MASK);
}
#endif

#ifdef notyet
static inline uint64_t
context_get_slptptr(struct context_entry *ce)
{
	return (ce->low & CTX_SLPTPTR_MASK);
}
#endif

struct context_table {
	struct context_entry entries[4096/16];
};


struct acpidmar_domain;

struct acpidmar_address_space {
	uint64_t	address_size;
	uint8_t		address_width; /* for hardware */
	int		level;
};

int	acpidmar_enter_level(struct acpidmar_domain *ad, int level, void *ctx,
	    bus_addr_t vaddr, paddr_t paddr, int flags);
void	acpidmar_remove_level(struct acpidmar_domain *ad, int level, void *ctx,
	    bus_addr_t vaddr);

struct acpidmar_address_space acpidmar_address_spaces[] = {
	{
		.address_size = (1ULL<<30)-1,
		.address_width = CTX_AW30BIT,
		.level = 2,
	},
	{
		.address_size = (1ULL<<39)-1,
		.address_width = CTX_AW39BIT,
		.level = 3,
	},
	{
		.address_size = (1ULL<<48)-1,
		.address_width = CTX_AW48BIT,
		.level = 4,
	},
	{
		.address_size = (1ULL<<57)-1,
		.address_width = CTX_AW57BIT,
		.level = 5,
	},
	{
		.address_size = (~0ULL),
		.address_width = CTX_AW64BIT,
		.level = 6,
	},
};

struct acpidmar_domain {
	TAILQ_ENTRY(acpidmar_domain)	 ad_entry;	/* XXX RB? */
	struct acpidmar_drhd_softc	*ad_parent;
	struct acpidmar_address_space	*ad_aspace;
	bus_dma_tag_t			 ad_dmat;
	int				 ad_refs;
	uint16_t			 ad_id;		/* domain id */
	/* list of members... */
	/* page tables */
	void				*ad_root_entry;
	struct vm_page			*ad_slptptr;
	/* dma tag */
};

int	acpidmar_domain_bind_page(void *, bus_addr_t, paddr_t, int);
void	acpidmar_domain_unbind_page(void *, bus_addr_t);
void	acpidmar_domain_flush_tlb(void *);

enum ctx_flush_type {
	CTX_FLUSH_GLOBAL,
	CTX_FLUSH_DOMAIN,
	CTX_FLUSH_DEVICE,
};
struct acpidmar_pic;

/*
 * acpidmar_drhd_softc encapsulates all of the state required to handle a single
 * remapping device.
 */
struct acpidmar_drhd_softc {
	TAILQ_ENTRY(acpidmar_drhd_softc)	 ads_entry;
	uint8_t					*ads_scopes;
	struct acpidmar_pic			*ads_pic;
	void					*ads_intr;
	bus_space_handle_t			 ads_memh;
	struct mutex				 ads_reg_lock;
	uint64_t				 ads_addr;
	uint64_t				 ads_cap;
	uint64_t				 ads_ecap;
	/* flags that must be set in gcmd to prevent disabling things we want */
	uint32_t				 ads_gcmd_persist;
	uint16_t				 ads_scopelen;
	uint16_t				 ads_max_domains;
	uint16_t				 ads_next_domain;
	uint8_t	 				 ads_flags;

	/* root table of paddrs for contexts for this remapper. */
	struct root_table			*ads_rtable;
	struct vm_page				*ads_rtable_pg;
	TAILQ_HEAD(,acpidmar_domain)		 ads_domains;
	void				 	 (*ads_flush_tlb)(
						     struct acpidmar_drhd_softc *,
						     struct acpidmar_domain *);
	void				 	 (*ads_flush_ctx)(
						     struct acpidmar_drhd_softc *,
						     enum ctx_flush_type,
						     uint16_t, uint16_t,
						     uint8_t);
	bool					 ads_enabled;
};

/* interrupt code defined on a machine dependant basis */
struct acpidmar_pic	*acpidmar_new_intrsrc(struct acpidmar_drhd_softc *);
void			*acpidmar_intr_establish(void *, int, int (*)(void *),  
			     void *, const char *);

void	acpidmar_enable_drhd(struct acpidmar_drhd_softc *ads);
void	acpidmar_set_rtp(struct acpidmar_drhd_softc *ads);
void	acpidmar_flush_tlb_register(struct acpidmar_drhd_softc *ads,
	    struct acpidmar_domain *ad);
void	acpidmar_flush_ctx_register(struct acpidmar_drhd_softc *,
	    enum ctx_flush_type type, uint16_t domain_id, uint16_t  source,
	    uint8_t function_mask);
void	acpidmar_flush_write_buffer(struct acpidmar_drhd_softc *ads);
static inline void
acpidmar_flush_cache(struct acpidmar_drhd_softc *ads, vaddr_t addr, vsize_t sz)
{
	if ((ads->ads_ecap & DMAR_ECAP_C) == 0)
		pmap_flush_cache(addr, sz);
}

struct context_entry	*context_for_pcitag(struct acpidmar_drhd_softc *,
			     pci_chipset_tag_t, pcitag_t);

/*
 * context_for_pcitag looks up the content entry for a given pci device.
 * if alloc_if_not_present is set then missing entries will be allocated..
 */
struct context_entry *
context_for_pcitag(struct acpidmar_drhd_softc *drhd,
    pci_chipset_tag_t pc, pcitag_t tag)
{
	struct vm_page		*pg;
	struct root_entry	*root_entry;
	struct context_table	*ctx_table;
	int 			 bus, dev, func, ctx_offset;

	pci_decompose_tag(pc, tag, &bus, &dev, &func);

	if (bus > nitems(drhd->ads_rtable->entries))
		panic("%s: bus (%d) over %d!", __func__, bus,
		    nitems(drhd->ads_rtable->entries));

	root_entry = &(drhd->ads_rtable->entries[bus]);
	if (root_entry_is_valid(root_entry)) {
		printf("%s: context entry already valid for %d:%d:%d\n",
			__func__, bus, dev, func);
		pg = PHYS_TO_VM_PAGE(
		    root_entry_get_context_pointer(root_entry));
	} else {
		struct pglist	 pglist;

		printf("%s: allocating context entry for %d:%d:%d\n",
			__func__, bus, dev, func);

		/*
		 * WAITOK can't fail. zeroed out page is equal to all invalid
		 * contexts.
		 */
		TAILQ_INIT(&pglist);
		(void)uvm_pglistalloc(PAGE_SIZE, 0, -1, PAGE_SIZE, 0, &pglist,
			1, UVM_PLA_WAITOK | UVM_PLA_ZERO);
		pg = TAILQ_FIRST(&pglist);
		/* purge the zeroing if not coherent. */
		acpidmar_flush_cache(drhd, pmap_map_direct(pg), PAGE_SIZE);

		*root_entry = make_root_entry(VM_PAGE_TO_PHYS(pg));
		acpidmar_flush_cache(drhd, (vaddr_t)root_entry,
		    sizeof(*root_entry));

		/*
		 * We don't flush context cache here, since the caller will
		 * update the context entry and do the full flush.
		 */

		KASSERT(root_entry_is_valid(root_entry));
	} 

	ctx_table = (struct context_table *)pmap_map_direct(pg);

	/* table does from dev 0, func 0 -> dev 31 func 7. 0-255 */
	ctx_offset = (dev << 3) | func;
	if  (ctx_offset > nitems(ctx_table->entries))
		panic("%s: dev(%d)<<3|func(%d) > %d!", __func__, dev, func,
		    nitems(ctx_table->entries));

	return (&(ctx_table->entries[ctx_offset]));
}

/*
 * Entry in the tree we will build on pci enumeration, this type is shared
 * for all types of entities.
 */
struct pci_tree_entry {
	TAILQ_ENTRY(pci_tree_entry)	 pte_entry;
	struct pci_tree_entry		*pte_parent;
	struct acpidmar_drhd_softc	*pte_drhd;
	struct acpidmar_domain		*pte_domain;
	pcitag_t			 pte_tag;
	enum {
		DMAR_PCI_ROOT,
		DMAR_PCI_BRIDGE,
		DMAR_PCI_DEVICE,
	}				 pte_type;
	uint8_t				 pte_depth; /* 0 if root bus */
};

/* Plain PCI devices don't currently need any extra data. */
struct pci_tree_device {
	struct pci_tree_entry 		ptd_base;
};

/*
 * A specialisation of pci_tre_entry for a pbb so tha we can look devices up
 * by parent and search their children.
 */
struct pci_tree_bridge {
	struct pci_tree_entry 		ptb_base;
	TAILQ_HEAD(, pci_tree_entry)	ptb_children;
	RB_ENTRY(pci_tree_bridge)	ptb_entry;
	bool				ptb_is_pcie;
};
RB_HEAD(acpidmar_bridges, pci_tree_bridge);

static inline int
ptb_cmp(struct pci_tree_bridge *a, struct pci_tree_bridge *b)
{

	/* this may be considered cheeky */
	return (memcmp(&a->ptb_base.pte_tag, &b->ptb_base.pte_tag,
		sizeof(pcitag_t)));
}
RB_PROTOTYPE_STATIC(acpidmar_bridges, pci_tree_bridge, ptb_entry, ptb_cmp);

struct acpidmar_rmrr_softc {
	TAILQ_ENTRY(acpidmar_rmrr_softc)	 ars_entry;
	uint8_t					*ars_scopes;
	uint64_t				 ars_addr;
	uint64_t				 ars_limaddr;
	uint16_t				 ars_scopelen;
};

struct acpidmar_pci_domain {
	struct pci_tree_bridge			apd_root; /* root of pci tree */
	struct acpidmar_bridges			apd_bridges; /* bridge tree */
	TAILQ_HEAD(,acpidmar_drhd_softc)	apd_drhds;
	TAILQ_HEAD(,acpidmar_rmrr_softc)	apd_rmrrs;
	bool					apd_enabled;
};

struct acpidmar_softc {
	struct device			 as_dev;
	bus_space_tag_t			 as_memt;
	struct acpidmar_pci_domain	**as_domains;
	uint16_t			 as_num_pci_domains;

};

struct acpidmar_softc	*acpidmar_softc;

struct cfattach acpidmar_ca = {
	sizeof(struct acpidmar_softc), acpidmar_match, acpidmar_attach
};

struct cfdriver acpidmar_cd = {
	NULL, "acpidmar", DV_DULL
};

void	acpidmar_enable_iommu(int domain);
void	acpidmar_add_drhd(struct acpidmar_softc *, struct acpidmar_drhd *);
void	acpidmar_add_rmrr(struct acpidmar_softc *, struct acpidmar_rmrr *);
bool	acpidmar_single_devscope_matches(pci_chipset_tag_t,
	    struct acpidmar_devscope *, struct pci_tree_entry *);
bool	acpidmar_devscope_matches(pci_chipset_tag_t, uint8_t *, uint16_t,
	    struct pci_tree_entry *);
void	acpidmar_find_drhd(pci_chipset_tag_t, struct acpidmar_pci_domain *,
	    struct pci_tree_entry *);
void	acpidmar_create_domain(pci_chipset_tag_t, struct acpidmar_pci_domain *,
	    struct pci_tree_entry *);
struct vm_page *acpidmar_alloc_page(struct acpidmar_domain *, int);

int	acpidmar_intr(void *);

/* XXX drhd should probably be a separate device... */
void
acpidmar_add_drhd(struct acpidmar_softc *sc, struct acpidmar_drhd *drhd)
{
	struct acpidmar_drhd_softc	*ads;
	struct acpidmar_pci_domain	*domain;
	struct vm_page			*pg;
	struct pglist			 pglist;
	uint32_t			 fr_offset, fr_num,  max_fr_offset;
	uint32_t			 iotlb_offset, max_offset;
	/*
	 * we only handle growing this array and allocating here since the
	 * specification specfiically states that
	 * 1) table entries are in order of type.
	 * 2) there will be at least one drhd enry for every segment (domain in
	 * the language of our pci stack) in the machine.
	 * so if we don't haven allocated domain in later entry types then the
	 * table is bad.
	 */
	if (drhd->segment >= sc->as_num_pci_domains) {
		/*
		 * It is easier to use an array of pointers here than it is
		 * to fix up rb trees etc when we move the parent. We could
		 * do an initial scan and allocate the domains upfront then
		 * rescan to fill in, if we chose.
		 */
		struct acpidmar_pci_domain **newsegments;

		if  (ULONG_MAX / sizeof(*sc->as_domains) < drhd->segment + 1)
			panic("%s: overflow!", __func__);
		newsegments = malloc(sizeof(*sc->as_domains) *
			(drhd->segment + 1), M_DEVBUF, M_WAITOK|M_ZERO);
		memcpy(newsegments, sc->as_domains,
		    sizeof(*sc->as_domains) * sc->as_num_pci_domains);
		free(sc->as_domains, M_DEVBUF);
		sc->as_domains = newsegments;
		sc->as_num_pci_domains = drhd->segment + 1;
	}
	domain = sc->as_domains[drhd->segment];
	if (domain == NULL) {
		domain = malloc(sizeof(*domain), M_DEVBUF, M_WAITOK|M_ZERO);

		domain->apd_root.ptb_base.pte_parent = NULL;
		domain->apd_root.ptb_base.pte_type = DMAR_PCI_ROOT;
		TAILQ_INIT(&domain->apd_root.ptb_children);
		RB_INIT(&domain->apd_bridges);
		TAILQ_INIT(&domain->apd_drhds);
		TAILQ_INIT(&domain->apd_rmrrs);

		sc->as_domains[drhd->segment] = domain;
	}

	/* allocate drhd structure and insert */


	printf("found drhd: 0x%llx for domain %d%s: ",
	    drhd->address, drhd->segment, drhd->flags & DMAR_DRHD_PCI_ALL ?
	    ", whole segment " : "");
	acpidmar_print_devscope((uint8_t *)drhd + sizeof(*drhd),
	    drhd->length - sizeof(*drhd));
	printf("\n");

	ads = malloc(sizeof(*ads), M_DEVBUF, M_WAITOK|M_ZERO);
	mtx_init(&ads->ads_reg_lock, IPL_HIGH);

	ads->ads_pic = acpidmar_new_intrsrc(ads);

	if (bus_space_map(sc->as_memt, drhd->address, PAGE_SIZE, 0,
	    &ads->ads_memh) != 0)
		panic("%s: failed to map registers at %llx\n", drhd->address);
	/* check sizes, the buffer could actually be more than a page, then we
	 * have to unmap and remap
	 */
	ads->ads_cap = bus_space_read_8(acpidmar_softc->as_memt, ads->ads_memh,
	    DMAR_CAP_REG);
	ads->ads_ecap = bus_space_read_8(acpidmar_softc->as_memt, ads->ads_memh,
	    DMAR_ECAP_REG);
	/*
	 * This will truncate to 16 bits, so a 16 bit id will be 0.
	 * note that we always preallocate the 0th domain for simplicity.
	 * if we ever hit issues with this we should allocate an appropriately
	 * sized  bitmap instead.
	 */
	ads->ads_max_domains = (uint16_t)(1 <<
	    (4 + 2 * ((ads->ads_cap & DMAR_CAP_ND_MASK) >> DMAR_CAP_ND_SHIFT)));

	/*
	 * We have a couple of variable position registers in the register file.
	 * check that they both fit in a page, else we have to enlarge our
	 * mapping.
	 */
	fr_offset =  (ads->ads_cap & DMAR_CAP_FRO_MASK) >>
	    DMAR_CAP_FRO_SHIFT;
	fr_offset *= 16;
	fr_num = ((ads->ads_cap & DMAR_CAP_NFR_MASK) >> DMAR_CAP_NFR_SHIFT) + 1;
	max_fr_offset = fr_offset + fr_num * 16;

	iotlb_offset = (ads->ads_ecap & DMAR_ECAP_IRO_MASK) >>
	    DMAR_ECAP_IRO_SHIFT;
	iotlb_offset += 16; /* end offset */
	max_offset = max_fr_offset > iotlb_offset ? max_fr_offset :
	    iotlb_offset;
	if (max_offset > PAGE_SIZE) {
		bus_space_unmap(acpidmar_softc->as_memt, ads->ads_memh,
		    PAGE_SIZE);
		if (bus_space_map(sc->as_memt, drhd->address,
		    roundup(max_offset, PAGE_SIZE), 0, &ads->ads_memh) != 0)
			panic("%s: failed to map registers at %llx (2)\n",
			    drhd->address);
	}

	/* print some basic stuff for now for debugging. */
	printf("rd: %s, wd: %s, zlr: %s, cm: %s, rwbf: %s, dis %s, pt %s, "
	    "qi: %s, C: %s\n", ads->ads_cap & DMAR_CAP_DRD ? "true": "false",
	    ads->ads_cap & DMAR_CAP_DWD ? "true": "false",
	    ads->ads_cap & DMAR_CAP_ZLR ? "true": "false",
	    ads->ads_cap & DMAR_CAP_CM ? "true": "false",
	    ads->ads_cap & DMAR_CAP_RWBF ? "true": "false",
	    ads->ads_ecap & DMAR_ECAP_DIS ? "true": "false",
	    ads->ads_ecap & DMAR_ECAP_PT ? "true": "false",
	    ads->ads_ecap & DMAR_ECAP_QI ? "true": "false",
	    ads->ads_ecap & DMAR_ECAP_C ? "true": "false");


	ads->ads_flags = drhd->flags;
	ads->ads_addr = drhd->address;
	ads->ads_scopes = (uint8_t *)drhd + sizeof(*drhd);
	ads->ads_scopelen  = drhd->length - sizeof(*drhd);
	ads->ads_next_domain = 1;
	ads->ads_flush_tlb = acpidmar_flush_tlb_register;
	ads->ads_flush_ctx = acpidmar_flush_ctx_register;
	TAILQ_INIT(&ads->ads_domains);
	TAILQ_INSERT_TAIL(&domain->apd_drhds, ads, ads_entry);

	/*
	 * Allocate memory for the root context table.
	 * XXX km_alloc(, &kv_singlepage) would also work here but we would have
	 * to have a structure to keep track of the virtual address mappings.
	 * for simplicity we make use of the fact this code as written will only
	 * work on PMAP_DIRECT architectures and we allocate pages directly 
	 * then we can find their virtual address implicitly.
	 */
	TAILQ_INIT(&pglist);
	(void)uvm_pglistalloc(PAGE_SIZE, 0, -1, PAGE_SIZE, 0, &pglist,
		1, UVM_PLA_WAITOK | UVM_PLA_ZERO);
	ads->ads_rtable_pg = pg = TAILQ_FIRST(&pglist);
	ads->ads_rtable = (struct root_table *)pmap_map_direct(pg);
	/* Purge allocation zeroing from cache before the hardware sees it. */
	acpidmar_flush_cache(ads, (vaddr_t)ads->ads_rtable, PAGE_SIZE);

	/* program hardware and flush caches */
	acpidmar_set_rtp(ads);

	ads->ads_intr = acpidmar_intr_establish(ads->ads_pic, IPL_BIO,
	    acpidmar_intr, ads, "acpidmar_drhd");
	if (ads->ads_intr == NULL) {
		panic("%s: failed to establish interrupt", __func__); /* XXX */
	}

	/* If we are the catch-all for this domain then root has us. */
	if (ads->ads_flags & DMAR_DRHD_PCI_ALL) {
		domain->apd_root.ptb_base.pte_drhd = ads;
	}
}

void
acpidmar_add_rmrr(struct acpidmar_softc *sc, struct acpidmar_rmrr *rmrr)
{
	struct acpidmar_rmrr_softc	*ars;
	struct acpidmar_pci_domain	*domain;

	/*
	 * We must have seen all drhd by now and thus we should have allocated
	 * this array correctly. If we have a missing one then the table is bad.
	 */
	if (rmrr->segment >= sc->as_num_pci_domains) {
		panic("%s: rmrr with unseen pci domain %d (max %d)",
			__func__, rmrr->segment, sc->as_num_pci_domains);
	}
	domain = sc->as_domains[rmrr->segment];
	if (domain == NULL) {
		panic("%s: rmrr with NULL pci domain %d (max %d)", __func__,
			rmrr->segment, sc->as_num_pci_domains);
	}

	printf("found rmrr for 0x%llx-0x%llx for domain %d",
	    rmrr->base, rmrr->limit, rmrr->segment);
	acpidmar_print_devscope((uint8_t *)rmrr + sizeof(*rmrr),
	    rmrr->length - sizeof(*rmrr));
	printf("\n");

	ars = malloc(sizeof(*ars), M_DEVBUF, M_WAITOK|M_ZERO);

	ars->ars_addr = rmrr->base;
	ars->ars_limaddr = rmrr->limit;
	ars->ars_scopes = (uint8_t *)rmrr + sizeof(*rmrr);
	ars->ars_scopelen  = rmrr->length - sizeof(*rmrr);
	TAILQ_INSERT_TAIL(&domain->apd_rmrrs, ars, ars_entry);
}

void
acpidmar_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpidmar_softc	*sc = (struct acpidmar_softc *)self;
	struct acpi_softc	*psc = (struct acpi_softc *)parent;
	struct acpi_attach_args	*aaa = aux;
	struct acpi_dmar	*dmar = (struct acpi_dmar *)aaa->aaa_table;
	extern void		(*pci_enable_iommu)(int domain);
	caddr_t			 addr;

	if (acpidmar_softc != NULL) {
		panic("%s: we've already got one!", __func__);
	}
	acpidmar_softc = sc;
	sc->as_memt = psc->sc_memt;

	/* Sanity check table before we start building */
	if (!acpidmar_validate(dmar)) {
		printf(": invalid, skipping\n");
		return;
	}

	printf(": checks out as valid\n");

	pci_enable_iommu = acpidmar_enable_iommu;

	addr = (caddr_t)(dmar + 1);

	while (addr < (caddr_t)dmar + dmar->hdr.length) {
		union acpidmar_entry *entry = (union acpidmar_entry *)addr;
		u_int8_t length = entry->length;

		switch (entry->type) {
		case DMAR_DRHD:
			acpidmar_add_drhd(acpidmar_softc, &entry->drhd);
			break;
		case DMAR_RMRR:
			acpidmar_add_rmrr(acpidmar_softc, &entry->rmrr);
			break;
		case DMAR_ATSR:
			printf("found atsr, not supported yet\n");
			/* FALLTHROUGH not handled yet */
		case DMAR_RHSA:
			/* FALLTHROUGH unsupported */
		default:
			/*
			 * Specification states that unknown types are to be
			 * skipped.
			 */
			 break;
		}

		addr += length;
	}
}

int
acpidmar_print(void *aux, const char *pnp)
{
	struct apic_attach_args *aaa = aux;

	if (pnp)
		printf("%s at %s:", aaa->aaa_name, pnp);

	return (UNCONF);
}

/*
 * acpidmar_single_devscope_matches returns true if the single device scope
 * in scope matches the device in entry.
 */
bool
acpidmar_single_devscope_matches(pci_chipset_tag_t pc,
    struct acpidmar_devscope *scope, struct pci_tree_entry *entry)
{
	int				 bus, dev, func;
	uint8_t				 path_len = (scope->length -
					     sizeof(*scope))/2;
	uint8_t				 *path_entry;

	if (entry->pte_depth + 1 != path_len) {
		return (false);
	}

	/*
	 * Check that the type of device matches the type of scope.
	 * So far all we care about is endpoints and bridges.
	 */
	switch (scope->type) {
	case DMAR_ENDPOINT:
		if (entry->pte_type != DMAR_PCI_DEVICE)
			return (false);
		break;
	case DMAR_BRIDGE:
		if (entry->pte_type != DMAR_PCI_BRIDGE)
			return (false);
		break;
	default:
		return (false);
	}

	/* point to last entry in the path */
	path_entry = (uint8_t *)scope + scope->length - 2;
	/* validate path starting with bottom and walking up */
	for (;;) {
		pci_decompose_tag(pc, entry->pte_tag, &bus, &dev, &func);

		if (dev != path_entry[0] || func != path_entry[1]) {
			return (false);
		}

		path_entry -= 2;
		/* all but the last loop */
		if (entry->pte_depth == 0)
			break;
		entry = entry->pte_parent;
	}

	/*
	 * if we are still here, the dev/func paths worked all the way up to the
	 * top bus. all that remains is for us to check that the initial bus
	 * matches. The current entry will be non null and have a depth of 0.
	 */
	KASSERT(entry != NULL && entry->pte_depth == 0);

	pci_decompose_tag(pc, entry->pte_tag, &bus, &dev, &func);
	if (bus != scope->bus) {
		return (false);
	}
	return (true);
}

/*
 * acpidmar_devscope_matches returns true if one of the device scopes
 * in scope-scope+scopelen matches the device in entry.
 */
bool
acpidmar_devscope_matches(pci_chipset_tag_t pc, uint8_t *scopes,
    uint16_t scopelen, struct pci_tree_entry *entry)
{
	uint8_t		*pos = scopes;
	
	while (pos < scopes + scopelen) {
		struct acpidmar_devscope	*scope =
		    (struct acpidmar_devscope *)pos;
		if (acpidmar_single_devscope_matches(pc, scope, entry))
			return (true);

		pos += scope->length;
	}
	return (false);
}

/*
 * acpidmar_find_drhd fills in the pte_drhd member of the provided entry.
 * we search all of the known drhds to find any that match, if none do we use
 * the catch-all if provided.
 */
void
acpidmar_find_drhd(pci_chipset_tag_t pc, struct acpidmar_pci_domain *domain,
    struct pci_tree_entry *entry)
{
	struct acpidmar_drhd_softc	*drhd;

	TAILQ_FOREACH(drhd, &domain->apd_drhds, ads_entry) {
		/*
		 * We don't expicitly look at the catchall, we always get it
		 * from parent devices (or the fake root device).
		 */
		if (drhd->ads_flags & DMAR_DRHD_PCI_ALL) {
			continue;
		}

		if (acpidmar_devscope_matches(pc, drhd->ads_scopes,
		    drhd->ads_scopelen, entry)) {
			entry->pte_drhd = drhd;
			return;
		}
	}

	/*
	 * parent is always non null. in the case of the root bus it points
	 * to our root entry. if root->drhd is NULL then we have no catchall
	 * which is only valid if all other drhds match otehr devices. If we
	 * hit that case either we have a nasty bug, or the table is bad.
	 */
	entry->pte_drhd = entry->pte_parent->pte_drhd;

	if (entry->pte_drhd == NULL) {
		int bus, dev, func;
		pci_decompose_tag(pc, entry->pte_tag, &bus, &dev, &func);
		panic("%s: %d:%d:%d has no valid dmar mapping\n",
		    __func__, bus, dev, func);
	}
}

void
acpidmar_create_domain(pci_chipset_tag_t pc, struct acpidmar_pci_domain *domain,
    struct pci_tree_entry *entry)
{
	struct acpidmar_drhd_softc	*drhd;
	struct acpidmar_rmrr_softc	*rmrr;
	struct acpidmar_domain		*ad;
	struct context_entry		*ctx_entry;
	struct pglist	 		 pglist;
	paddr_t				 highest_rmrr;
	uint16_t			 domain_id;

	drhd = entry->pte_drhd;

	/*
	 * Behind a bridge we must share domain with our parent.
	 * We make an exception here if drhds differ.
	 */
	if (entry->pte_parent->pte_type == DMAR_PCI_BRIDGE &&
	    entry->pte_drhd == entry->pte_parent->pte_drhd) {
		ad = entry->pte_parent->pte_domain;
		ad->ad_refs++;
		goto program_domain;
	}

	/*
	 * XXX handle wrapping? This may matter when we allow devices to move
	 * domain for virtualisation, for now the domain counter should be
	 * enough to fit all child devices.
	 */
	if (drhd->ads_next_domain == drhd->ads_max_domains) {
		panic("%s: domain count full!", __func__);
	}
	domain_id = drhd->ads_next_domain++;

	/* make new domain struct. */
	ad = malloc(sizeof(*domain), M_DEVBUF, M_WAITOK | M_ZERO);
	ad->ad_parent = drhd;
	ad->ad_id = domain_id;
	ad->ad_refs = 1;

	/*
	 * Pick appropriate rmrr devices. A reading of the spec implies that
	 * rmrr devscope are *always* precise device endpoints. so this should
	 * always match correctly.
	 * We actually scan this list twice, once to see what the highest memory
	 * address we need care about is (so we can pick the domain size), the
	 * second time actualy maps the memory. If this ever becomes a
	 * bottleneck we could save a list of known indexes into the rmrr array
	 * and use that for the second iteration.
	 */
	highest_rmrr = 0;
	TAILQ_FOREACH(rmrr, &domain->apd_rmrrs, ars_entry) {
		if (acpidmar_devscope_matches(pc, rmrr->ars_scopes,
		    rmrr->ars_scopelen, entry)) {
			if (rmrr->ars_limaddr > highest_rmrr)
				highest_rmrr = rmrr->ars_limaddr;
		}
	}

	/*
	 * Pick domain address width. We default to 30bits (1gb address space
	 * per domain) but if we have any hardware that won't fit in that 1gb
	 * space (for example some rmrrs are just under the 4gig mark) we need
	 * to increase the address space. We pick the minimum we can get away
	 * with to reduce page table overhead.
	 */
	for (int i = 0; i < nitems(acpidmar_address_spaces); i++) {
		if (highest_rmrr > acpidmar_address_spaces[i].address_size)
			continue;
		/*
		 * pick the first address space width that we will fit in.
		 * note that this always halts because the last one is the full
		 * address space.
		 */
		ad->ad_aspace = &acpidmar_address_spaces[i];
		break;
	}

	/*
	 * XXX should make these strings unique but then they'd never be
	 * freeable.
	 */
	/* we start at 16mb to avoid isa ranges. */
	if (sg_dmatag_alloc("vt-d iommmu", ad, 16*1024*1024,
	    ad->ad_aspace->address_size, acpidmar_domain_bind_page,
	    acpidmar_domain_unbind_page, acpidmar_domain_flush_tlb,
	    &ad->ad_dmat) != 0)
		panic("%s: unable to create dma tag", __func__);
	/* allocate root pagetable */
	TAILQ_INIT(&pglist);
	(void)uvm_pglistalloc(PAGE_SIZE, 0, -1, PAGE_SIZE, 0, &pglist,
		1, UVM_PLA_WAITOK | UVM_PLA_ZERO);
	ad->ad_slptptr = TAILQ_FIRST(&pglist);
	ad->ad_root_entry = (void *)pmap_map_direct(ad->ad_slptptr);
	/* ensure zeroing sticks before the hardware sees it */
	acpidmar_flush_cache(drhd, (vaddr_t)ad->ad_root_entry, PAGE_SIZE);

program_domain:
	entry->pte_domain = ad;
	ctx_entry = context_for_pcitag(drhd, pc, entry->pte_tag);
	{
		int 			 bus, dev, func;

		pci_decompose_tag(pc, entry->pte_tag, &bus, &dev, &func);
		printf("%s: ctx_entry is %p for %d:%d:%d\n", __func__,
		    ctx_entry, bus, dev, func);
	}
	*ctx_entry = make_context_entry(ad->ad_id, ad->ad_aspace->address_width,
	    VM_PAGE_TO_PHYS(ad->ad_slptptr), CTX_TT_TRANSLATE);
	if (drhd->ads_cap & DMAR_CAP_CM) {
		int 			 bus, dev, func;
		uint16_t		 source_id;

		pci_decompose_tag(pc, entry->pte_tag, &bus, &dev, &func);

		source_id = bus << 16 | dev << 3 | func;
		/*
		 * Not present to present mapping, we must flush domain 0
		 * to flush any not-present entries.
		 */
		drhd->ads_flush_ctx(drhd, CTX_FLUSH_DEVICE, 0, source_id, 0);
		drhd->ads_flush_tlb(drhd, NULL); /* XXX Global for now */
	} else {
		acpidmar_flush_write_buffer(ad->ad_parent);
	}

	/*
	 * If parent is pci-{,-x} bridge we need to handle requrests from
	 * current bus,0,0. if parent is pci-e we program the bridge device.
	 * anything behind a bridge that doesn't have its own remapping hardware
	 * ends up in the parent bridges domain.
	 * This may have already happened, but we reprogram here just in case.
	 */
	if (entry->pte_parent->pte_type == DMAR_PCI_BRIDGE) {
		struct pci_tree_bridge	*bridge;
		pcitag_t		 tag;
		uint16_t		 source_id;

		bridge = (struct pci_tree_bridge *)entry->pte_parent ;
		if (bridge->ptb_is_pcie) {
			int bus, dev, func;
			tag = bridge->ptb_base.pte_tag;

			pci_decompose_tag(pc, entry->pte_tag, &bus, &dev, &func);
			source_id = bus << 16 | dev << 3 | func;
		} else {
			int			 bus;

			pci_decompose_tag(pc, entry->pte_tag, &bus, NULL, NULL);
			tag = pci_make_tag(pc, bus, 0, 0);
			source_id = bus << 16 | 0 << 3 | 0;
		}
		/* We already confirmed that drhd matched. */
		ctx_entry = context_for_pcitag(drhd, pc, tag);
		*ctx_entry = make_context_entry(ad->ad_id,
		    ad->ad_aspace->address_width,
		    VM_PAGE_TO_PHYS(ad->ad_slptptr), CTX_TT_TRANSLATE);
		/*
		 * flush tlbs and context, same rules as above.
		 * arguably we could track if this has already happeend for a
		 * previous device attached to the same bridge. but since we
		 * add contexts only at boot time i'm not sure if it is
		 * worth the complexity.
		 */
		drhd->ads_flush_ctx(drhd, CTX_FLUSH_DEVICE, 0, source_id, 0);
		drhd->ads_flush_tlb(drhd, NULL); /* XXX Global for now */
	}

	/* note that device should not have translation switched on yet */
	TAILQ_FOREACH(rmrr, &domain->apd_rmrrs, ars_entry) {
		struct sg_cookie	*cookie;
		u_long			 result;

		if (!acpidmar_devscope_matches(pc, rmrr->ars_scopes,
		    rmrr->ars_scopelen, entry)) {
			continue;
		}
		{
			int bus, dev, func;
			pci_decompose_tag(pc, entry->pte_tag, &bus,
			    &dev, &func);
			printf("%s: %d:%d:%d matches rmrr at "
			    "%llx-%llx\n", __func__, bus, dev, func,
			    rmrr->ars_addr, rmrr->ars_limaddr);
		}
		/*
		 * We should only hit this if we have a pci-pci-{,x} btidge with
		 * a rmrr behind it with an odd space. This should be pretty
		 * unusual. (in the case it happens we could copy the
		 * pagetables, add anther level, redo the extent and then pray.
		 */
		if (rmrr->ars_limaddr > ad->ad_aspace->address_size) {
			panic("%s: rmrr 0x%llx-0x%llx doesn't fit "
			    "in domain address width %lld", __func__,
			    rmrr->ars_addr, rmrr->ars_limaddr,
			    ad->ad_aspace->address_size);
		}
		/* holy layering violation batman... */
		cookie = ad->ad_dmat->_cookie;

		/*
		 * This is autoconf, we shouldn't see EINTR.
		 * WAITOK is passed so we shouldn't see ENOMEM, so the only
		 * valid error is EAGAIN meaning the space wasn't available.
		 * in this case we assume that a previous device in this domain
		 * shared the rmrr and thus it is already allocated and mapped.
		 * We don't check result since we have constained the
		 * allocation.
		 */
		if (extent_alloc_subregion(cookie->sg_ex, rmrr->ars_addr,
		    rmrr->ars_limaddr + 1,
		    rmrr->ars_limaddr - rmrr->ars_addr + 1,
		    4096, 0, 0, EX_WAITOK, &result) == 0) {
			paddr_t addr;

			for (addr = rmrr->ars_addr; addr <= rmrr->ars_limaddr;
			    addr += PAGE_SIZE) {
				int	ret;

				if ((ret = acpidmar_enter_level(
				    ad, ad->ad_aspace->level, ad->ad_root_entry,
				    (bus_addr_t)addr, addr, BUS_DMA_WAITOK |
				    BUS_DMA_READ | BUS_DMA_WRITE)) != 0) {
					panic("%s: can't enter rmrr %d",
					    __func__, ret);
				}
			}
		}
		drhd->ads_flush_tlb(drhd, ad); /* XXX device selective. */
	}
}

int
acpidmar_domain_bind_page(void *hdl, bus_addr_t va, paddr_t pa, int flags)
{
	struct acpidmar_domain	*ad = hdl;

	/*
	 * Externally these flags determine if solely read or write are needed.
	 * we also use them to track required permissions.
	 */
	if ((flags & (BUS_DMA_READ|BUS_DMA_WRITE)) == 0) {
		flags |= BUS_DMA_READ | BUS_DMA_WRITE;
	}

	/*
	 * If we don't support zero length reads then permit reads for
	 * write posting purposes.
	 */
	if ((flags & (BUS_DMA_READ|BUS_DMA_WRITE)) == BUS_DMA_WRITE &&
	    (ad->ad_parent->ads_cap & DMAR_CAP_ZLR) == 0)
		flags |= BUS_DMA_READ;

	/* XXX this must return... */
	return (acpidmar_enter_level(ad, ad->ad_aspace->level,
	    ad->ad_root_entry, va, pa, flags));
}

void
acpidmar_domain_unbind_page(void *hdl, bus_addr_t va)
{
	struct acpidmar_domain	*ad = hdl;

	acpidmar_remove_level(ad, ad->ad_aspace->level, ad->ad_root_entry, va);
}

/*
 * XXX need the region we are flushing the tlb for so that we can do 
 * page selective invalidations eventually.
 */
void
acpidmar_domain_flush_tlb(void *hdl)
{
	struct acpidmar_domain	*ad = hdl;

	/*
	 * behaviour depends on if we just filled in previously zeroed options
	 * or not.
	 * XXX this does make the early page table marking read/write not work
	 * XXX so well we need a flag before we can turn that back on..
	 * flushing context cache /pasid cache or iotlb is an *implicit* write
	 * buffer flush.
	 * it would be better to use queued invalidation but for the first cut
	 * just use the register based version.
	 * it would also be good to know the region so we could do page
	 * invalidated invalidates.
	 */

#ifdef notyet
	if added && !modified && (ad->ad_parent->ads_cap & DMAR_CAP_CM) == 0 {
		acpidmar_flush_write_buffer(ad->ad_parent);
	} else {
		/*
		 * XXX we can defer this in the unmap case to a timeout to
		 * allow us to batch these.
		 */
		ad->ad_parent->ads_flush_tlb(ad->ad_parent, ad);
	}
#else /* notyet */
	ad->ad_parent->ads_flush_tlb(ad->ad_parent, ad);
#endif
}


RB_GENERATE_STATIC(acpidmar_bridges, pci_tree_bridge, ptb_entry, ptb_cmp);

void
acpidmar_pci_hook(pci_chipset_tag_t pc, struct pci_attach_args *pa)
{
	struct acpidmar_pci_domain	*domain;
	struct pci_tree_bridge		*parent;
	struct pci_tree_entry		*entry;
	uint8_t				 curdepth;

	
	if (acpidmar_softc == NULL) {
		return;
	}

	if (pa->pa_domain >= acpidmar_softc->as_num_pci_domains) {
		panic("%s: domain %d >= %d", __func__, pa->pa_domain,
			acpidmar_softc->as_num_pci_domains);
	}

	domain = acpidmar_softc->as_domains[pa->pa_domain];
	/*
	 * If it against the spec to have a domain in the system without
	 * an drhd entry, so if this occurs then either we have bugs, or the
	 * acpi table is full of horrific lies.
	 */
	if (domain == NULL) {
		panic("%s: no domain for domain %d", __func__, pa->pa_domain);
	}

	/* First, lookup direct parent in the tree */
	if (pa->pa_bridgetag == NULL) {
		parent = &domain->apd_root;
		curdepth = 0;
	} else {
		struct pci_tree_bridge  search;

		search.ptb_base.pte_tag = *pa->pa_bridgetag;
		if ((parent = RB_FIND(acpidmar_bridges, &domain->apd_bridges, 
			&search)) == NULL) {
			int bus, dev, func;
			pci_decompose_tag(pc, *pa->pa_bridgetag, &bus, &dev,
			    &func);
			panic("%s: can't find parent %d:%d:%d in bridge list\n",
			    __func__, bus, dev, func);
		}
		curdepth = parent->ptb_base.pte_depth + 1;
	}

	/*
	 * See if we already have the appropriate children (we can index this
	 * if it ends up being too slow).
	 */
	TAILQ_FOREACH(entry, &parent->ptb_children, pte_entry) {
		if (entry->pte_tag == pa->pa_tag) {
			goto add_tag;
		}
	}

	/* if bridges we have to be prepared to handle children */
        if (PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
		    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_PCI) {
		struct pci_tree_bridge	*bridge;
		/* if we fail this early in autoconf we are fucked */
		bridge = malloc(sizeof(*bridge), M_DEVBUF, M_WAITOK);

		TAILQ_INIT(&bridge->ptb_children);
		entry = &bridge->ptb_base;
		entry->pte_type = DMAR_PCI_BRIDGE;
		if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
		    NULL, NULL)) {
			bridge->ptb_is_pcie = true;
		}
	} else {
		struct pci_tree_device	*dev;
		/* if we fail this early in autoconf we are fucked */
		dev = malloc(sizeof(*dev), M_DEVBUF, M_WAITOK);

		entry = &dev->ptd_base;
		entry->pte_type = DMAR_PCI_DEVICE;
	}

	/* XXX set up tag, allocate dma domain, etc, set up tag. */
	/* else create domain, dma tag etc */
	/* recordd omain, bus, device, function */
	/* for now we don't need interrupt information */
	entry->pte_tag = pa->pa_tag;
	entry->pte_parent = &parent->ptb_base;
	entry->pte_depth = curdepth;

	acpidmar_find_drhd(pc, domain, entry);
	{
		int bus, dev, func;
		pci_decompose_tag(pc, entry->pte_tag, &bus, &dev, &func);
		printf("%s: %d:%d:%d matches drhd at %llx\n",
		    __func__, bus, dev, func, entry->pte_drhd->ads_addr);
	}
	acpidmar_create_domain(pc, domain, entry);

	TAILQ_INSERT_TAIL(&parent->ptb_children, entry, pte_entry);

	/*
	 * If bridge we need to be able to look this up later for our
	 * children.
	 */
	if (entry->pte_type == DMAR_PCI_BRIDGE) {
		struct pci_tree_bridge	*bridge =
			(struct pci_tree_bridge *)entry;

		/* no concurrent access, so won't be any duplicates */
		(void)RB_INSERT(acpidmar_bridges, &domain->apd_bridges, bridge);
	} else {
		/* set up tag for dma */
		/*struct pci_tree_device	*dev = (struct pci_tree_device *)entry; */
		/* pa->pa_tag = dev->ptd_dmatag */
	}

add_tag:
	if (entry->pte_drhd->ads_enabled)
		pa->pa_dmat = entry->pte_domain->ad_dmat;
}

void
acpidmar_set_rtp(struct acpidmar_drhd_softc *ads)
{
	struct acpidmar_softc	*sc = acpidmar_softc;

	/*
	 * Set root address. root address has no flags since we don't
	 * want extended root entries so value is just the physical address
	 * of the root table page.
	 */
	bus_space_write_8(sc->as_memt, ads->ads_memh, DMAR_RTADDR_REG,
	    VM_PAGE_TO_PHYS(ads->ads_rtable_pg));
	/* program root context */
	bus_space_write_4(sc->as_memt, ads->ads_memh, DMAR_GCMD_REG,
	    DMAR_GCMD_SRTP | ads->ads_gcmd_persist);
	while ((bus_space_read_4(sc->as_memt, ads->ads_memh, DMAR_GSTS_REG) &
	    DMAR_GSTS_RTPS) == 0)
		SPINLOCK_SPIN_HOOK;

	/* We've just reset the root table, so clean up caches and tlbs */
	ads->ads_flush_ctx(ads, CTX_FLUSH_GLOBAL, 0, 0, 0);
	ads->ads_flush_tlb(ads, NULL);
}

void
acpidmar_enable_iommu(int domain)
{
	struct acpidmar_softc		*sc = acpidmar_softc;
	struct acpidmar_drhd_softc	*ads;

	if (sc == NULL)
		panic("%s: no softc!", __func__);
	if (domain >= sc->as_num_pci_domains)
		panic("%s: domain %d out of range.", __func__, domain);
	if (sc->as_domains[domain] == NULL)
		panic("%s: domain %d not seen.", __func__, domain);

	sc->as_domains[domain]->apd_enabled = true;
	TAILQ_FOREACH(ads, &sc->as_domains[domain]->apd_drhds, ads_entry) {
		acpidmar_enable_drhd(ads);
	}
}

void
acpidmar_enable_drhd(struct acpidmar_drhd_softc *ads)
{
	struct acpidmar_softc	*sc = acpidmar_softc;

	/* Already on? */
	if (ads->ads_gcmd_persist & DMAR_GCMD_TE)
		return;
	printf("%s: enabling drhd %llx\n", __func__,
		ads->ads_addr);
	return;

	acpidmar_flush_write_buffer(ads);
	ads->ads_enabled = true;

	/* turn on fault logging when we can support */
	/* make sure root entry is set */

	acpidmar_set_rtp(ads);

	/* turn on translation */
	ads->ads_gcmd_persist |= DMAR_GCMD_TE;
	bus_space_write_4(sc->as_memt, ads->ads_memh, DMAR_GCMD_REG,
	    ads->ads_gcmd_persist);
	while ((bus_space_read_4(sc->as_memt, ads->ads_memh, DMAR_GSTS_REG) &
	    DMAR_GSTS_TES) == 0)
		SPINLOCK_SPIN_HOOK;

	/* turn off protected memory crap */
}

/* bootstrapping - how to handle gpu enablement. */

/*
 * Allocate a page for a levelled page table entry. Eventually this will do
 * caching for BUS_DMA_ALLOCNOW allocations.
 */
struct vm_page *
acpidmar_alloc_page(struct acpidmar_domain *ad, int flags)
{
	struct vm_page	*pg;
	struct pglist	 pglist;
	int		 wait;

	if (flags & BUS_DMA_NOWAIT) {
		wait = UVM_PLA_NOWAIT;
	} else {
		wait = UVM_PLA_WAITOK;
	}

	TAILQ_INIT(&pglist);
	if (uvm_pglistalloc(PAGE_SIZE, 0, -1, PAGE_SIZE, 0, &pglist, 1,
	    wait | UVM_PLA_ZERO) != 0) {
		return (NULL);
	}
	pg = TAILQ_FIRST(&pglist);
	/* ensure zeroing sticks before the hardware sees it */
	acpidmar_flush_cache(ad->ad_parent, pmap_map_direct(pg), PAGE_SIZE);
	return (pg);
}

#define PTE_MASK	(0x1ffULL)	/* bits per level. */
#define PTE_TRANSIENT	(1ULL<<62)
#define PTE_SNOOP	(1ULL<<11)
#define PTE_LARGEPAGE	(1ULL<<7)
#define PTE_IPAT	(1ULL<<6)
#define PTE_EMT_NC	0ULL
#define PTE_EMT_WC	1ULL
#define PTE_EMT_WT	4ULL
#define PTE_EMT_WP	5ULL
#define PTE_EMT_WB	6ULL
#define PTE_EMT_MASK	0x7ULL
#define PTE_EMT_SHIFT	3
#define PTE_EXECUTE	(1ULL<<2)
#define PTE_WRITE	(1ULL<<1)
#define PTE_READ	(1ULL<<0)

/*
 * helper function working out the offset in the page (measured in int64_ts)
 * for a given virtual address and current page table depth.
 */
static inline int
acpidmar_offset_for_level(int level, vaddr_t va)
{
	int shift;

	/* 9 bits for each level plus 12 bits for 4096 byte dma pages. */
	shift =  12 + 9 * (level-1);
	return ((va & (PTE_MASK<<shift)) >> shift);
}

#define PTE_MASK	(0x1ffULL)	/* bits per level. */
#define PTE_LARGEPAGE	(1ULL<<7)
/*
 * acpidmar_enter_level enters a mapping into the page table for vaddr to paddr
 * with the given (bus_dma) flags. We start at `level' using the page table page
 * pointed to in `ctx' and recurively call down to the bottom level.
 * For now, large pages are not handled.
 */
int
acpidmar_enter_level(struct acpidmar_domain *ad, int level, void *ctx,
    bus_addr_t vaddr, paddr_t paddr, int flags)
{
	uint64_t	*cur_entry, *current = ctx;
	struct vm_page	*pg;
	paddr_t		 next_phys;
	int		 offset;

	offset = acpidmar_offset_for_level(level, vaddr);
	cur_entry = &current[offset];

	/* we don't currently handle large pages. */
	KASSERT((*cur_entry & PTE_LARGEPAGE) == 0);

	if (level == 1) {
		uint64_t	 pteflags = 0;

		if (flags & BUS_DMA_READ)
			pteflags |= PTE_READ;
		if (flags & BUS_DMA_WRITE)
			pteflags |= PTE_WRITE;

		/* invalid to ask for nothing */
		KASSERT(pteflags != 0);

		*cur_entry = paddr | pteflags;
		acpidmar_flush_cache(ad->ad_parent, (vaddr_t)cur_entry,
		    sizeof(*cur_entry));
	
		return (0);
	}

	next_phys = *cur_entry & ~PAGE_MASK;

	if (next_phys == 0) {
		uint64_t	cur_entry_val;

		if ((pg = acpidmar_alloc_page(ad, flags)) == NULL)
			return (ENOMEM);

		next_phys = VM_PAGE_TO_PHYS(pg);
		cur_entry_val = next_phys;
		/*
		 * See comment in the else below as to why we always do
		 * read and write here.
		 */
#ifdef notyet
		if (flags & BUS_DMA_READ)
			cur_entry_val |= PTE_READ;
		if (flags & BUS_DMA_WRITE)
			cur_entry_val |= PTE_WRITE;
#endif
		cur_entry_val |= PTE_READ|PTE_WRITE;
		*cur_entry = cur_entry_val;
		acpidmar_flush_cache(ad->ad_parent, (vaddr_t)cur_entry,
		    sizeof(*cur_entry));
	} else {
		/*
		 * We can't do this level stuff yet because we need to handle
		 * the correct tlb flushing since this means an insert isn't
		 * always just adding brand new entires, we need a backchannel
		 * to pass this information.
		 */
#ifdef notyet 
		if ((*cur_entry & PTE_READ) == 0 && flags & BUS_DMA_READ)
			*cur_entry |= PTE_READ;
		if ((*cur_entry & PTE_WRITE) == 0 && flags & BUS_DMA_WRITE)
			*cur_entry |= PTE_WRITE;
#endif
		pg = PHYS_TO_VM_PAGE(next_phys);
	}
	return (acpidmar_enter_level(ad, level - 1, (void *)pmap_map_direct(pg),
	    vaddr, paddr, flags));
}

/*
 * acpidmar_remove_level removes mappings for vaddr from the page table system
 * starting at `level' with page table page `ctx'.
 */
void
acpidmar_remove_level(struct acpidmar_domain *ad, int level, void *ctx,
    bus_addr_t vaddr)
{
	uint64_t	*next, *cur_entry, *current = ctx;
	struct vm_page	*pg;
	paddr_t		 next_phys;
	int		 offset;

	offset = acpidmar_offset_for_level(level, vaddr);
	cur_entry = &current[offset];

	/* we don't currently handle large pages. */
	KASSERT((*cur_entry & PTE_LARGEPAGE) == 0);

	if (level == 1) {
		*cur_entry = 0;
		acpidmar_flush_cache(ad->ad_parent, (vaddr_t)cur_entry,
		    sizeof(*cur_entry));
		return;
	}

	next_phys = *cur_entry & ~PAGE_MASK;

	if (next_phys == 0)
		return;
	pg = PHYS_TO_VM_PAGE(next_phys);
	next = (uint64_t *)pmap_map_direct(pg);
	acpidmar_remove_level(ad, level -1, next, vaddr);

	/* XXX check if next next level is now empty, and free it if so.*/
}

#define DMAR_IOTLB_INVALIDATE	(1ULL<<63)
#define DMAR_IOTLB_IIRG_MASK	(2ULL)
#define DMAR_IOTLB_IIRG_SHIFT	(60)
#define DMAR_IOTLB_IAIG_MASK	(2ULL)
#define DMAR_IOTLB_IAIG_SHIFT	(58)
#define DMAR_IOTLB_INVAL_GLOBAL		(1ULL)
#define DMAR_IOTLB_INVAL_DOMAIN		(2ULL)
#define DMAR_IOTLB_INVAL_DOMAIN_PAGE	(3ULL)
#define DMAR_IOTLB_DR		(1ULL<<49)
#define DMAR_IOTLB_DW		(1ULL<<48)
#define DMAR_IOTLB_DID_MASK	(0xffffULL)
#define DMAR_IOTLB_DID_SHIFT	(58)
void
acpidmar_flush_tlb_register(struct acpidmar_drhd_softc *ads,
    struct acpidmar_domain *ad)
{
	bus_size_t		 iotlb_offset;
	uint64_t		 val;

	iotlb_offset = ((ads->ads_ecap & DMAR_ECAP_IRO_MASK) >>
		DMAR_ECAP_IRO_SHIFT) * 16;
	val = DMAR_IOTLB_INVALIDATE;
	if (ad != NULL) {
		val |= (ad->ad_id & DMAR_IOTLB_DID_MASK) <<
		    DMAR_IOTLB_DID_SHIFT;
		val |= DMAR_IOTLB_INVAL_DOMAIN << DMAR_IOTLB_IIRG_SHIFT;
	} else {
		/* no domain, global inval. */
		val |= DMAR_IOTLB_INVAL_GLOBAL << DMAR_IOTLB_IIRG_SHIFT;
	}

	if (ads->ads_cap & DMAR_CAP_DWD)
		val |= DMAR_IOTLB_DW;
	/* can't do any other invalidation etc while this is going on */
	mtx_enter(&ads->ads_reg_lock);
	bus_space_write_8(acpidmar_softc->as_memt, ads->ads_memh,
	    iotlb_offset + 8, val);
	while (bus_space_read_8(acpidmar_softc->as_memt, ads->ads_memh,
	    iotlb_offset + 8) & DMAR_IOTLB_INVALIDATE)
		SPINLOCK_SPIN_HOOK;
	mtx_leave(&ads->ads_reg_lock);
}

void
acpidmar_flush_ctx_register(struct acpidmar_drhd_softc *ads,
    enum ctx_flush_type type, uint16_t domain_id, uint16_t  source,
    uint8_t function_mask)
{
	uint64_t	val;

	val = DMAR_CCMD_ICC;
	switch (type) {
	case CTX_FLUSH_GLOBAL:
		val |= DMAR_CCMD_INVAL_GLOBAL << DMAR_CCMD_CIRG_SHIFT;
		break;
	case CTX_FLUSH_DOMAIN:
		val |= DMAR_CCMD_INVAL_DOMAIN << DMAR_CCMD_CIRG_SHIFT;
		break;
	case CTX_FLUSH_DEVICE:
		val |= DMAR_CCMD_INVAL_DEVICE << DMAR_CCMD_CIRG_SHIFT;
		val |= (uint64_t)function_mask << DMAR_CCMD_FM_SHIFT;
		val |= (uint64_t)domain_id << DMAR_CCMD_DID_SHIFT;
		val |= (uint64_t)source << DMAR_CCMD_SID_SHIFT;
		break;
	}

	mtx_enter(&ads->ads_reg_lock);
	bus_space_write_8(acpidmar_softc->as_memt, ads->ads_memh,
	     DMAR_CCMD_REG, val);

	while (bus_space_read_8(acpidmar_softc->as_memt, ads->ads_memh,
	    DMAR_CCMD_REG) & DMAR_CCMD_ICC)
		SPINLOCK_SPIN_HOOK;
	mtx_leave(&ads->ads_reg_lock);
}

void
acpidmar_flush_write_buffer(struct acpidmar_drhd_softc *ads)
{
	/* only if the hardware requires it. */
	if ((ads->ads_cap & DMAR_CAP_RWBF) == 0)
		return

	bus_space_write_4(acpidmar_softc->as_memt, ads->ads_memh,
	    DMAR_GCMD_REG, DMAR_GCMD_WBF | ads->ads_gcmd_persist);
	while ((bus_space_read_4(acpidmar_softc->as_memt, ads->ads_memh,
	    DMAR_GSTS_REG) & DMAR_GSTS_WBFS) != 0)
		SPINLOCK_SPIN_HOOK;
}

void
acpidmar_setup_msi(struct acpidmar_drhd_softc *, uint64_t, uint32_t);
void
acpidmar_setup_msi(struct acpidmar_drhd_softc *ads, uint64_t addr, uint32_t vec)
{
	struct acpidmar_softc	*sc = acpidmar_softc;

	mtx_enter(&ads->ads_reg_lock);

	/* write data pointer  31:9 reserved, 8 low prio?, 7:0 vector */
	bus_space_write_4(sc->as_memt, ads->ads_memh, DMAR_FEDATA_REG,
	    vec);
	/*
	 * Set up address. MD code programmed the address register based on
	 * apic mode etc. We just program it.
	 */
	bus_space_write_4(sc->as_memt, ads->ads_memh, DMAR_FEADDR_REG,
	    addr & ((1ULL<<32)-1));
	bus_space_write_4(sc->as_memt, ads->ads_memh, DMAR_FEUADDR_REG,
	    (addr >> 32));
	mtx_leave(&ads->ads_reg_lock);
}

int
acpidmar_intr(void *ctx)
{
	//struct acpidmar_drhd_softc	*ads = ctx;

	/* msi, always ours */
	return (1);
}
