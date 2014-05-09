/* $OpenBSD: acpimadt.c,v 1.26 2012/01/07 20:13:16 kettenis Exp $ */
/*
 * Copyright (c) 2013 Owain G. Ainsworth <oga@nicotinebsd.org>
 * Copyright (c) 2006 Mark Kettenis <kettenis@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/tree.h>

#include <machine/apicvar.h>
#include <machine/cpuvar.h>
#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcivar.h>

int	 acpidmar_match(struct device *, void *, void *);
void	 acpidmar_attach(struct device *, struct device *, void *);

struct cfattach acpidmar_ca = {
	sizeof(struct device), acpidmar_match, acpidmar_attach
};

struct cfdriver acpidmar_cd = {
	NULL, "acpidmar", DV_DULL
};

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

struct acpidmar_drhd_softc {
	uint8_t	 	flags;
	uint16_t	pci_domain;
	uint64_t	addr;
	/* list of devices that we know about */
	/* actual device connected to bullshit. */
};

void
acpidmar_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args	*aaa = aux;
	struct acpi_dmar	*dmar = (struct acpi_dmar *)aaa->aaa_table;
	caddr_t			 addr;

	/* Do some sanity checks before committing to run in APIC mode. */
	if (!acpidmar_validate(dmar)) {
		printf(": invalid, skipping\n");
		return;
	}

	printf(": checks out as valid\n");

	addr = (caddr_t)(dmar + 1);

	while (addr < (caddr_t)dmar + dmar->hdr.length) {
		union acpidmar_entry *entry = (union acpidmar_entry *)addr;
		u_int8_t length = entry->length;

		/* no sanity checks here, we already checked everything */
		switch (entry->type) {
		case DMAR_DRHD:
			printf("found drhd: 0x%llx for domain %d%s: ",
			    entry->drhd.address, entry->drhd.segment,
			    entry->drhd.flags & DMAR_DRHD_PCI_ALL ?
			    ", whole segment " : "");
			acpidmar_print_devscope(addr + sizeof(entry->drhd),
				length - sizeof(entry->drhd));
			printf("\n");
			break;
		case DMAR_RMRR:
			printf("found rmrr for 0x%llx-0x%llx for domain %d",
			    entry->rmrr.base, entry->rmrr.limit,
			    entry->rmrr.segment);
			acpidmar_print_devscope(addr + sizeof(entry->rmrr),
				length - sizeof(entry->rmrr));
			printf("\n");
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

/* rb tree of entries */
/* list of children */
/* pointer to the parent dmar structure */

struct pci_tree_entry {
	TAILQ_ENTRY(pci_tree_entry)	 pte_entry;
	struct pci_tree_entry		*pte_parent;
	pcitag_t			 pte_tag;
	enum {
		DMAR_PCI_ROOT,
		DMAR_PCI_BRIDGE,
		DMAR_PCI_DEVICE,
	}				pte_type;
};

/* device:function pair */
struct pci_tree_device {
	struct pci_tree_entry 		ptd_base;
};

/* ppb device with children. */
struct pci_tree_bridge {
	struct pci_tree_entry 		ptb_base;
	TAILQ_HEAD(, pci_tree_entry)	ptb_children;
	RB_ENTRY(pci_tree_bridge)	ptb_entry;
};

struct pci_tree_bridge dmar_pci_root = {
	.ptb_base = {
		.pte_parent = NULL,
		.pte_type = DMAR_PCI_ROOT,
	},
	.ptb_children = TAILQ_HEAD_INITIALIZER((&dmar_pci_root)->ptb_children),
};

static inline int
ptb_cmp(struct pci_tree_bridge *a, struct pci_tree_bridge *b)
{

	/* this may be considered cheeky */
	return (memcmp(&a->ptb_base.pte_tag, &b->ptb_base.pte_tag,
		sizeof(pcitag_t)));
}
/* XXX this and the root may need to be per domain */
RB_HEAD(acpidmar_bridges, pci_tree_bridge) acpidmar_bridges =
	RB_INITIALIZER(&acpidmar_bridges);
RB_PROTOTYPE_STATIC(acpidmar_bridges, pci_tree_bridge, ptb_entry, ptb_cmp);
RB_GENERATE_STATIC(acpidmar_bridges, pci_tree_bridge, ptb_entry, ptb_cmp);

void
acpidmar_pci_hook(pci_chipset_tag_t pc, struct pci_attach_args *pa)
{
	struct pci_tree_bridge	*parent;
	struct pci_tree_entry	*entry;
	bool			 is_bridge = false;

	
	/* we can't currently handle anything other than 1 pci domain. */
	if (pa->pa_domain != 0)
		panic("%s: domain %d != 0", pa->pa_domain);

	/* First, lookup direct parent in the tree */
	if (pa->pa_bridgetag == NULL) {
		printf("%s: searching root bus\n", __func__);
		parent = &dmar_pci_root;
	} else {
		struct pci_tree_bridge  search;
		{
			int bus, dev, func;
			pci_decompose_tag(pc, *pa->pa_bridgetag, &bus, &dev,
			    &func);
			printf("%s: searching child bus %d:%d:%d\n", __func__,
			    bus, dev, func);
		}

		search.ptb_base.pte_tag = *pa->pa_bridgetag;
		if ((parent = RB_FIND(acpidmar_bridges, &acpidmar_bridges, 
			&search)) == NULL) {
			/* XXX report bus/dev/func */
			int bus, dev, func;
			pci_decompose_tag(pc, *pa->pa_bridgetag, &bus, &dev,
			    &func);
			panic("%s: can't find parent %d:%d:%d in bridge list\n",
			    __func__, bus, dev, func);
		}
	}

	/*
	 * See if we already have the appropriate children (we can index this
	 * if it ends up being too slow).
	 */
	TAILQ_FOREACH(entry, &parent->ptb_children, pte_entry) {
		if (entry->pte_tag == pa->pa_tag) {
			int bus, dev, func;
			pci_decompose_tag(pc, pa->pa_tag, &bus, &dev, &func);
			printf("%s: found %d:%d:%d already in tree\n",
			    __func__, bus, dev, func);
			/* report we found it, swizzle dma tag and return */
			return;
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

		is_bridge = true;
	} else {
		struct pci_tree_device	*dev;
		/* if we fail this early in autoconf we are fucked */
		dev = malloc(sizeof(*dev), M_DEVBUF, M_WAITOK);

		entry = &dev->ptd_base;
		entry->pte_type = DMAR_PCI_DEVICE;
	}

	{

		int bus, dev, func;
		pci_decompose_tag(pc, pa->pa_tag, &bus, &dev, &func);
		printf("%s: %d:%d:%d not found creating (bridge: %s)\n",
		    __func__, bus, dev, func, is_bridge ? "yes" : "no");
	}
	/* XXX set up tag, allocate dma domain, etc, set up tag. */
	/* else create domain, dma tag etc */
	/* recordd omain, bus, device, function */
	/* for now we don't need interrupt information */
	entry->pte_tag = pa->pa_tag;

	/*
	 * If bridge we need to be able to look this up later for our
	 * children.
	 */
	if (is_bridge) {
		struct pci_tree_bridge	*bridge =
			(struct pci_tree_bridge *)entry;

		RB_INSERT(acpidmar_bridges, &acpidmar_bridges, bridge);
	}
}
