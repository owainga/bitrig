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

/*
 * Entry in the tree we will build on pci enumeration, this type is shared
 * for all types of entities.
 */
struct pci_tree_entry {
	TAILQ_ENTRY(pci_tree_entry)	 pte_entry;
	struct pci_tree_entry		*pte_parent;
	struct acpidmar_drhd_softc	*pte_drhd;
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

struct acpidmar_drhd_softc {
	TAILQ_ENTRY(acpidmar_drhd_softc)	 ads_entry;
	uint8_t					*ads_scopes;
	uint64_t				 ads_addr;
	uint16_t				 ads_scopelen;
	uint8_t	 				 ads_flags;
	/* list of devices that we know about */
	/* actual device connected to bullshit. */
};

struct acpidmar_pci_domain {
	struct pci_tree_bridge			apd_root; /* root of pci tree */
	struct acpidmar_bridges			apd_bridges; /* bridge tree */
	TAILQ_HEAD(,acpidmar_drhd_softc)	apd_drhds;
};

struct acpidmar_softc {
	struct acpidmar_pci_domain	**as_domains;
	uint16_t			 as_num_pci_domains;

};

struct acpidmar_softc	*acpidmar_softc;

void	acpidmar_add_drhd(struct acpidmar_softc *, struct acpidmar_drhd *);
void	acpidmar_add_rmrr(struct acpidmar_softc *, struct acpidmar_rmrr *);
bool	acpidmar_single_devscope_matches(pci_chipset_tag_t,
	    struct acpidmar_devscope *, struct pci_tree_entry *);
bool	acpidmar_devscope_matches(pci_chipset_tag_t, uint8_t *, uint16_t,
	    struct pci_tree_entry *);
void	acpidmar_find_drhd(pci_chipset_tag_t, struct acpidmar_pci_domain *,
	    struct pci_tree_entry *);

void
acpidmar_add_drhd(struct acpidmar_softc *sc, struct acpidmar_drhd *drhd)
{
	struct acpidmar_drhd_softc	*ads;
	struct acpidmar_pci_domain	*domain;
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
			panic("%s: overflow!");
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

	ads->ads_flags = drhd->flags;
	ads->ads_addr = drhd->address;
	ads->ads_scopes = (uint8_t *)drhd + sizeof(*drhd);
	ads->ads_scopelen  = drhd->length - sizeof(*drhd);
	TAILQ_INSERT_TAIL(&domain->apd_drhds, ads, ads_entry);

	/* If we are the catch-all for this domain then root has us. */
	if (ads->ads_flags & DMAR_DRHD_PCI_ALL) {
		domain->apd_root.ptb_base.pte_drhd = ads;
	}
}

void
acpidmar_add_rmrr(struct acpidmar_softc *sc, struct acpidmar_rmrr *rmrr)
{
	struct acpidmar_pci_domain	*domain;

	/*
	 * We must have seen all drhd by now and thus we should have allocated
	 * this array correctly. If we have a missing one then the table is bad.
	 */
	if (rmrr->segment >= sc->as_num_pci_domains) {
		panic("%s: rmrr with unseen pci domain %d (max %d)",
			rmrr->segment, sc->as_num_pci_domains);
	}
	domain = sc->as_domains[rmrr->segment];
	if (domain == NULL) {
		panic("%s: rmrr with NULL pci domain %d (max %d)",
			rmrr->segment, sc->as_num_pci_domains);
	}

	/* allocate rmrr structure and insert */


	printf("found rmrr for 0x%llx-0x%llx for domain %d",
	    rmrr->base, rmrr->limit, rmrr->segment);
	acpidmar_print_devscope((uint8_t *)rmrr + sizeof(*rmrr),
	    rmrr->length - sizeof(*rmrr));
	printf("\n");
}


void
acpidmar_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args	*aaa = aux;
	struct acpi_dmar	*dmar = (struct acpi_dmar *)aaa->aaa_table;
	caddr_t			 addr;

	if (acpidmar_softc != NULL) {
		panic("%s: we've already got one!", __func__);
	}

	/* Sanity check table before we start building */
	if (!acpidmar_validate(dmar)) {
		printf(": invalid, skipping\n");
		return;
	}

	printf(": checks out as valid\n");

	acpidmar_softc = malloc(sizeof(*acpidmar_softc), M_DEVBUF,
		M_WAITOK|M_ZERO);

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

RB_GENERATE_STATIC(acpidmar_bridges, pci_tree_bridge, ptb_entry, ptb_cmp);

void
acpidmar_pci_hook(pci_chipset_tag_t pc, struct pci_attach_args *pa)
{
	struct acpidmar_pci_domain	*domain;
	struct pci_tree_bridge		*parent;
	struct pci_tree_entry		*entry;
	bool				 is_bridge = false;
	uint8_t				 curdepth;

	
	if (acpidmar_softc == NULL) {
		return;
	}

	if (pa->pa_domain >= acpidmar_softc->as_num_pci_domains) {
		panic("%s: domain %d >= %d", pa->pa_domain,
			acpidmar_softc->as_num_pci_domains);
	}

	domain = acpidmar_softc->as_domains[pa->pa_domain];
	/*
	 * If it against the spec to have a domain in the system without
	 * an drhd entry, so if this occurs then either we have bugs, or the
	 * acpi table is full of horrific lies.
	 */
	if (domain == NULL) {
		panic("%s: no domain for domain %d", pa->pa_domain);
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
//	acpidmar_match_rmrrs(pc, domain, entry);
	TAILQ_INSERT_TAIL(&parent->ptb_children, entry, pte_entry);

	/*
	 * If bridge we need to be able to look this up later for our
	 * children.
	 */
	if (is_bridge) {
		struct pci_tree_bridge	*bridge =
			(struct pci_tree_bridge *)entry;

		/* no concurrent access, so won't be any duplicates */
		(void)RB_INSERT(acpidmar_bridges, &domain->apd_bridges, bridge);
	}
}
