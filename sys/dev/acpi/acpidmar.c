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

#include <machine/apicvar.h>
#include <machine/cpuvar.h>
#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <machine/mpbiosvar.h>

int acpidmar_match(struct device *, void *, void *);
void acpidmar_attach(struct device *, struct device *, void *);

struct cfattach acpidmar_ca = {
	sizeof(struct device), acpidmar_match, acpidmar_attach
};

struct cfdriver acpidmar_cd = {
	NULL, "acpidmar", DV_DULL
};

int acpidmar_validate(struct acpi_dmar *);
int acpidmar_validate_devscope(caddr_t, uint8_t);
int acpidmar_print(void *, const char *);

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
		    (struct acpidmar_devscope *)devscope;
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

void
acpidmar_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args	*aaa = aux;
	struct acpi_dmar	*dmar = (struct acpi_dmar *)aaa->aaa_table;

	printf("\n");
	/* Do some sanity checks before committing to run in APIC mode. */
	if (!acpidmar_validate(dmar)) {
		printf(": invalid, skipping\n");
		return;
	}

	printf(": checks out as valid\n");
}

int
acpidmar_print(void *aux, const char *pnp)
{
	struct apic_attach_args *aaa = aux;

	if (pnp)
		printf("%s at %s:", aaa->aaa_name, pnp);

	return (UNCONF);
}
