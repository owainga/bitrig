/*	$OpenBSD: ifb.c,v 1.13 2009/01/02 20:36:19 miod Exp $	*/

/*
 * Copyright (c) 2007, 2008, 2009 Miodrag Vallat.
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
 * Least-effort driver for the Sun Expert3D cards (based on the
 * ``Wildcat'' chips).
 *
 * There is no public documentation for these chips available.
 * Since they are no longer supported by 3DLabs (which got bought by
 * Creative), and Sun does not want to publish even minimal information
 * or source code, the best we can do is experiment.
 *
 * Quoting Alan Coopersmith in
 * http://mail.opensolaris.org/pipermail/opensolaris-discuss/2005-December/011885.html
 * ``Unfortunately, the lawyers have asked we not give details about why
 *   specific components are not being released.''
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/pciio.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/openfirm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <dev/rasops/rasops.h>

#include <machine/fbvar.h>

/*
 * Parts of the following hardware knowledge come from David S. Miller's
 * XVR-500 Linux driver (drivers/video/sunxvr500.c).
 */

/*
 * The Expert3D and Expert3d-Lite cards are built around the Wildcat
 * 5110, 6210 and 7210 chips.
 *
 * The card exposes the following resources:
 * - a 32MB aperture window in which views to the different frame buffer
 *   areas can be mapped, in the first BAR.
 * - a 64KB PROM and registers area, in the second BAR, with the registers
 *   starting 32KB within this area.
 * - a 8MB memory mapping, which purpose is unknown, in the third BAR.
 *
 * In the state the PROM leaves us in, the 8MB frame buffer windows map
 * the video memory as interleaved stripes, of which the non-visible parts
 * can still be addressed (probably for fast screen switching).
 *
 * Unfortunately, since we do not know how to reconfigure the stripes
 * to provide at least a linear frame buffer, we have to write to both
 * windows and have them provide the complete image.
 *
 * Moreover, high pixel values in the overlay planes (such as 0xff or 0xfe)
 * seem to enable other planes with random contents, so we'll limit ourselves
 * to 7bpp opration.
 */

/*
 * The Expert3D has an extra BAR that is not present on the -Lite
 * version.  This register contains bits that tell us how many BARs to
 * skip before we get to the BARs that interest us.
 */
#define IFB_PCI_CFG			0x5c
#define IFB_PCI_CFG_BAR_OFFSET(x)	((x & 0x000000e0) >> 3)

#define	IFB_REG_OFFSET			0x8000

/*
 * 0000 magic
 * This register seems to be used to issue commands to the
 * acceleration hardware.
 *
 */
#define IFB_REG_MAGIC			0x0000
#define IFB_REG_MAGIC_DIR_BACKWARDS_Y		(0x08 | 0x02)
#define IFB_REG_MAGIC_DIR_BACKWARDS_X		(0x04 | 0x01)

/*
 * 0040 component configuration
 * This register controls which parts of the board will be addressed by
 * writes to other configuration registers.
 * Apparently the low two bytes control the frame buffer windows for the
 * given head (starting at 1).
 * The high two bytes are texture related.
 */
#define	IFB_REG_COMPONENT_SELECT	0x0040

/*
 * 0044 status
 * This register has a bit that signals completion of commands issued
 * to the acceleration hardware.
 */
#define IFB_REG_STATUS			0x0044
#define IFB_REG_STATUS_DONE			0x00000004

/*
 * 0058 magnifying configuration
 * This register apparently controls magnifying.
 * bits 5-6 select the window width divider (00: by 2, 01: by 4, 10: by 8,
 *   11: by 16)
 * bits 7-8 select the zoom factor (00: disabled, 01: x2, 10: x4, 11: x8)
 */
#define	IFB_REG_MAGNIFY			0x0058
#define	IFB_REG_MAGNIFY_DISABLE			0x00000000
#define	IFB_REG_MAGNIFY_X2			0x00000040
#define	IFB_REG_MAGNIFY_X4			0x00000080
#define	IFB_REG_MAGNIFY_X8			0x000000c0
#define	IFB_REG_MAGNIFY_WINDIV2			0x00000000
#define	IFB_REG_MAGNIFY_WINDIV4			0x00000010
#define	IFB_REG_MAGNIFY_WINDIV8			0x00000020
#define	IFB_REG_MAGNIFY_WINDIV16		0x00000030

/*
 * 0070 display resolution
 * Contains the size of the display, as ((height - 1) << 16) | (width - 1)
 */
#define	IFB_REG_RESOLUTION		0x0070
/*
 * 0074 configuration register
 * Contains 0x1a000088 | ((Log2 stride) << 16)
 */
#define	IFB_REG_CONFIG			0x0074
/*
 * 0078 32bit frame buffer window #0 (8 to 9 MB)
 * Contains the offset (relative to BAR0) of the 32 bit frame buffer window.
 */
#define	IFB_REG_FB32_0			0x0078
/*
 * 007c 32bit frame buffer window #1 (8 to 9 MB)
 * Contains the offset (relative to BAR0) of the 32 bit frame buffer window.
 */
#define	IFB_REG_FB32_1			0x007c
/*
 * 0080 8bit frame buffer window #0 (2 to 2.2 MB)
 * Contains the offset (relative to BAR0) of the 8 bit frame buffer window.
 */
#define	IFB_REG_FB8_0			0x0080
/*
 * 0084 8bit frame buffer window #1 (2 to 2.2 MB)
 * Contains the offset (relative to BAR0) of the 8 bit frame buffer window.
 */
#define	IFB_REG_FB8_1			0x0084
/*
 * 0088 unknown window (as large as a 32 bit frame buffer)
 */
#define	IFB_REG_FB_UNK0			0x0088
/*
 * 008c unknown window (as large as a 8 bit frame buffer)
 */
#define	IFB_REG_FB_UNK1			0x008c
/*
 * 0090 unknown window (as large as a 8 bit frame buffer)
 */
#define	IFB_REG_FB_UNK2			0x0090

/*
 * 00bc RAMDAC palette index register
 */
#define	IFB_REG_CMAP_INDEX		0x00bc
/*
 * 00c0 RAMDAC palette data register
 */
#define	IFB_REG_CMAP_DATA		0x00c0

/*
 * 00e4 DPMS state register
 * States ``off'' and ``suspend'' need chip reprogramming before video can
 * be enabled again.
 */
#define	IFB_REG_DPMS_STATE		0x00e4
#define	IFB_REG_DPMS_OFF			0x00000000
#define	IFB_REG_DPMS_SUSPEND			0x00000001
#define	IFB_REG_DPMS_STANDBY			0x00000002
#define	IFB_REG_DPMS_ON				0x00000003

/*
 * (some) ROP codes
 */

#define	IFB_ROP_CLEAR	0x00000000	/* clear bits in rop mask */
#define	IFB_ROP_SRC	0x00330000	/* copy src bits matching rop mask */
#define	IFB_ROP_XOR	0x00cc0000	/* xor src bits with rop mask */
#define	IFB_ROP_SET	0x00ff0000	/* set bits in rop mask */

#define IFB_COORDS(x, y)	((x) | (y) << 16)

#define	IFB_PIXELMASK	0x7f	/* 7bpp */

struct ifb_softc {
	struct sunfb sc_sunfb;

	bus_space_tag_t sc_mem_t;
	pcitag_t sc_pcitag;

	bus_space_handle_t sc_mem_h;
	bus_addr_t sc_membase, sc_fb8bank0_base, sc_fb8bank1_base;
	bus_size_t sc_memlen;
	vaddr_t	sc_memvaddr, sc_fb8bank0_vaddr, sc_fb8bank1_vaddr;

	bus_space_handle_t sc_reg_h;
	bus_addr_t sc_regbase;
	bus_size_t sc_reglen;

	u_int	sc_mode;

	void (*sc_old_putchar)(void *, int, int, u_int, long);

	int sc_console;
	u_int8_t sc_cmap_red[256];
	u_int8_t sc_cmap_green[256];
	u_int8_t sc_cmap_blue[256];
};

int	ifb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	ifb_mmap(void *, off_t, int);
void	ifb_burner(void *, u_int, u_int);

struct wsdisplay_accessops ifb_accessops = {
	ifb_ioctl,
	ifb_mmap,
	NULL,	/* alloc_screen */
	NULL,	/* free_screen */
	NULL,	/* show_screen */
	NULL,	/* load_font */
	NULL,	/* scrollback */
	NULL,	/* getchar */
	ifb_burner,
	NULL	/* pollc */
};

int	ifbmatch(struct device *, void *, void *);
void	ifbattach(struct device *, struct device *, void *);

struct cfattach ifb_ca = {
	sizeof (struct ifb_softc), ifbmatch, ifbattach
};

struct cfdriver ifb_cd = {
	NULL, "ifb", DV_DULL
};

int	ifb_getcmap(struct ifb_softc *, struct wsdisplay_cmap *);
int	ifb_is_console(int);
int	ifb_mapregs(struct ifb_softc *, struct pci_attach_args *);
int	ifb_putcmap(struct ifb_softc *, struct wsdisplay_cmap *);
void	ifb_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);
void	ifb_setcolormap(struct sunfb *,
	    void (*)(void *, u_int, u_int8_t, u_int8_t, u_int8_t));

void	ifb_copyrect(struct ifb_softc *, int, int, int, int, int, int);
void	ifb_fillrect(struct ifb_softc *, int, int, int, int, int);
void	ifb_rop(struct ifb_softc *, int, int, int, int, int, int, uint32_t,
	    int32_t);
void	ifb_rop_wait(struct ifb_softc *);

void	ifb_putchar(void *, int, int, u_int, long);
void	ifb_copycols(void *, int, int, int, int);
void	ifb_erasecols(void *, int, int, int, long);
void	ifb_copyrows(void *, int, int, int);
void	ifb_eraserows(void *, int, int, long);
void	ifb_do_cursor(struct rasops_info *);

int
ifbmatch(struct device *parent, void *cf, void *aux)
{
	return ifb_ident(aux);
}

void    
ifbattach(struct device *parent, struct device *self, void *aux)
{
	struct ifb_softc *sc = (struct ifb_softc *)self;
	struct pci_attach_args *paa = aux;
	struct rasops_info *ri;
	int node;

	sc->sc_mem_t = paa->pa_memt;
	sc->sc_pcitag = paa->pa_tag;

	printf("\n");

	if (ifb_mapregs(sc, paa))
		return;

	sc->sc_fb8bank0_base = bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
	      IFB_REG_OFFSET + IFB_REG_FB8_0);
	sc->sc_fb8bank1_base = bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
	      IFB_REG_OFFSET + IFB_REG_FB8_1);

	sc->sc_memvaddr = (vaddr_t)bus_space_vaddr(sc->sc_mem_t, sc->sc_mem_h);
	sc->sc_fb8bank0_vaddr = sc->sc_memvaddr +
	    sc->sc_fb8bank0_base - sc->sc_membase;
	sc->sc_fb8bank1_vaddr = sc->sc_memvaddr +
	    sc->sc_fb8bank1_base - sc->sc_membase;

	node = PCITAG_NODE(paa->pa_tag);
	sc->sc_console = ifb_is_console(node);

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);

	printf("%s: %dx%d\n",
	    self->dv_xname, sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

#if 0
	/*
	 * Make sure the frame buffer is configured to sane values.
	 * So much more is needed there... documentation permitting.
	 */
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_COMPONENT_SELECT, 0x00000101);
	delay(1000);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGNIFY, IFB_REG_MAGNIFY_DISABLE);
#endif

	ri = &sc->sc_sunfb.sf_ro;
	ri->ri_bits = NULL;
	ri->ri_hw = sc;

	fbwscons_init(&sc->sc_sunfb, RI_BSWAP, sc->sc_console);

	/*
	 * Clear the unwanted pixel planes: all if non console (thus
	 * white background), and all planes above 7bpp otherwise.
	 */
	ifb_rop(sc, 0, 0, 0, 0, sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height,
	    IFB_ROP_CLEAR, sc->sc_console ? ~IFB_PIXELMASK : ~0);
	ifb_rop_wait(sc);

	/* pick centering delta */
	sc->sc_fb8bank0_vaddr += ri->ri_bits - ri->ri_origbits;
	sc->sc_fb8bank1_vaddr += ri->ri_bits - ri->ri_origbits;

	sc->sc_old_putchar = ri->ri_ops.putchar;
	ri->ri_ops.copyrows = ifb_copyrows;
	ri->ri_ops.copycols = ifb_copycols;
	ri->ri_ops.eraserows = ifb_eraserows;
	ri->ri_ops.erasecols = ifb_erasecols;
	ri->ri_ops.putchar = ifb_putchar;
	ri->ri_do_cursor = ifb_do_cursor;

	ifb_setcolormap(&sc->sc_sunfb, ifb_setcolor);
	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	if (sc->sc_console)
		fbwscons_console_init(&sc->sc_sunfb, -1);
	fbwscons_attach(&sc->sc_sunfb, &ifb_accessops, sc->sc_console);
}

int
ifb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct ifb_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;
	int mode;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_IFB;
		break;

	case WSDISPLAYIO_SMODE:
		mode = *(u_int *)data;
		if (mode == WSDISPLAYIO_MODE_EMUL)
			ifb_setcolormap(&sc->sc_sunfb, ifb_setcolor);
		sc->sc_mode = mode;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		return ifb_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return ifb_putcmap(sc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_GPCIID:
		sel = (struct pcisel *)data;
		sel->pc_bus = PCITAG_BUS(sc->sc_pcitag);
		sel->pc_dev = PCITAG_DEV(sc->sc_pcitag);
		sel->pc_func = PCITAG_FUN(sc->sc_pcitag);
		break;

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return -1; /* not supported yet */
        }

	return 0;
}

int
ifb_getcmap(struct ifb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	int error;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	error = copyout(&sc->sc_cmap_red[index], cm->red, count);
	if (error)
		return error;
	error = copyout(&sc->sc_cmap_green[index], cm->green, count);
	if (error)
		return error;
	error = copyout(&sc->sc_cmap_blue[index], cm->blue, count);
	if (error)
		return error;
	return 0;
}

int
ifb_putcmap(struct ifb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	u_int i;
	int error;
	u_char *r, *g, *b;

	if (index >= 256 || count > 256 - index)
		return EINVAL;

	if ((error = copyin(cm->red, &sc->sc_cmap_red[index], count)) != 0)
		return error;
	if ((error = copyin(cm->green, &sc->sc_cmap_green[index], count)) != 0)
		return error;
	if ((error = copyin(cm->blue, &sc->sc_cmap_blue[index], count)) != 0)
		return error;

	r = &sc->sc_cmap_red[index];
	g = &sc->sc_cmap_green[index];
	b = &sc->sc_cmap_blue[index];

	for (i = 0; i < count; i++) {
		bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
		    IFB_REG_OFFSET + IFB_REG_CMAP_INDEX, index);
		bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
		    IFB_REG_OFFSET + IFB_REG_CMAP_DATA,
		    (((u_int)*b) << 22) | (((u_int)*g) << 12) | (((u_int)*r) << 2));
		r++, g++, b++, index++;
	}
	return 0;
}

void
ifb_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct ifb_softc *sc = v;

	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;

	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_CMAP_INDEX, index);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_CMAP_DATA,
	    (((u_int)b) << 22) | (((u_int)g) << 12) | (((u_int)r) << 2));
}

/* similar in spirit to fbwscons_setcolormap() */
void
ifb_setcolormap(struct sunfb *sf,
    void (*setcolor)(void *, u_int, u_int8_t, u_int8_t, u_int8_t))
{
	struct rasops_info *ri = &sf->sf_ro;
	int i;
	const u_char *color;

	/*
	 * Compensate for overlay plane limitations. Since we'll operate
	 * in 7bpp mode, our basic colors will use positions 00 to 0f,
	 * and the inverted colors will use positions 7f to 70.
	 */

	for (i = 0x00; i < 0x10; i++) {
		color = &rasops_cmap[i * 3];
		setcolor(sf, i, color[0], color[1], color[2]);
	}
	for (i = 0x70; i < 0x80; i++) {
		color = &rasops_cmap[(0xf0 | i) * 3];
		setcolor(sf, i, color[0], color[1], color[2]);
	}

	/*
	 * Proper operation apparently needs black to be 01, always.
	 * Replace black, red and white with white, black and red.
	 * Kind of ugly, but it works.
	 */
	ri->ri_devcmap[WSCOL_WHITE] = 0x00000000;
	ri->ri_devcmap[WSCOL_BLACK] = 0x01010101;
	ri->ri_devcmap[WSCOL_RED] = 0x07070707;

	color = &rasops_cmap[(WSCOL_WHITE + 8) * 3];	/* real white */
	setcolor(sf, 0, color[0], color[1], color[2]);
	setcolor(sf, IFB_PIXELMASK ^ 0, ~color[0], ~color[1], ~color[2]);
	color = &rasops_cmap[WSCOL_BLACK * 3];
	setcolor(sf, 1, color[0], color[1], color[2]);
	setcolor(sf, IFB_PIXELMASK ^ 1, ~color[0], ~color[1], ~color[2]);
	color = &rasops_cmap[WSCOL_RED * 3];
	setcolor(sf, 7, color[0], color[1], color[2]);
	setcolor(sf, IFB_PIXELMASK ^ 7, ~color[0], ~color[1], ~color[2]);
}

paddr_t
ifb_mmap(void *v, off_t off, int prot)
{
	struct ifb_softc *sc = (struct ifb_softc *)v;

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
		/*
		 * In mapped mode, provide access to the two overlays,
		 * followed by the control registers, at the following
		 * addresses:
		 * 00000000	overlay 0, size up to 2MB (visible fb size)
		 * 01000000	overlay 1, size up to 2MB (visible fb size)
		 * 02000000	control registers
		 */
		off -= 0x00000000;
		if (off >= 0 && off < round_page(sc->sc_sunfb.sf_fbsize)) {
			return bus_space_mmap(sc->sc_mem_t,
			    sc->sc_fb8bank0_base,
			    off, prot, BUS_SPACE_MAP_LINEAR);
		}
		off -= 0x01000000;
		if (off >= 0 && off < round_page(sc->sc_sunfb.sf_fbsize)) {
			return bus_space_mmap(sc->sc_mem_t,
			    sc->sc_fb8bank1_base,
			    off, prot, BUS_SPACE_MAP_LINEAR);
		}
#ifdef notyet	/* not needed so far, will require an aperture check */
		off -= 0x01000000;
		if (off >= 0 && off < round_page(sc->sc_reglen)) {
			return bus_space_mmap(sc->sc_mem_t, sc->sc_regbase,
			    off, prot, BUS_SPACE_MAP_LINEAR);
		}
#endif
		break;
	}

	return -1;
}

void
ifb_burner(void *v, u_int on, u_int flags)
{
	struct ifb_softc *sc = (struct ifb_softc *)v;
	int s;
	uint32_t dpms;

	s = splhigh();
	if (on)
		dpms = IFB_REG_DPMS_ON;
	else {
#ifdef notyet
		if (flags & WSDISPLAY_BURN_VBLANK)
			dpms = IFB_REG_DPMS_SUSPEND;
		else
#endif
			dpms = IFB_REG_DPMS_STANDBY;
	}
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_DPMS_STATE, dpms);
	splx(s);
}

int
ifb_is_console(int node)
{
	extern int fbnode;

	return fbnode == node;
}

int
ifb_mapregs(struct ifb_softc *sc, struct pci_attach_args *pa)
{
	u_int32_t cf;
	int bar, rc;

	cf = pci_conf_read(pa->pa_pc, pa->pa_tag, IFB_PCI_CFG);
	bar = PCI_MAPREG_START + IFB_PCI_CFG_BAR_OFFSET(cf);

	cf = pci_conf_read(pa->pa_pc, pa->pa_tag, bar);
	if (PCI_MAPREG_TYPE(cf) == PCI_MAPREG_TYPE_IO)
		rc = EINVAL;
	else {
		rc = pci_mapreg_map(pa, bar, cf,
		    BUS_SPACE_MAP_LINEAR, NULL, &sc->sc_mem_h,
		    &sc->sc_membase, &sc->sc_memlen, 0);
	}
	if (rc != 0) {
		printf("%s: can't map video memory\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
		return rc;
	}

	cf = pci_conf_read(pa->pa_pc, pa->pa_tag, bar + 4);
	if (PCI_MAPREG_TYPE(cf) == PCI_MAPREG_TYPE_IO)
		rc = EINVAL;
	else {
		rc = pci_mapreg_map(pa, bar + 4, cf,
		    0, NULL, &sc->sc_reg_h,
		     &sc->sc_regbase, &sc->sc_reglen, 0x9000);
	}
	if (rc != 0) {
		printf("%s: can't map register space\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
		return rc;
	}

	return 0;
}

void
ifb_putchar(void *cookie, int row, int col, u_int uc, long attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	ri->ri_bits = (void *)sc->sc_fb8bank0_vaddr;
	sc->sc_old_putchar(cookie, row, col, uc, attr);
	ri->ri_bits = (void *)sc->sc_fb8bank1_vaddr;
	sc->sc_old_putchar(cookie, row, col, uc, attr);
}

void
ifb_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	ifb_copyrect(sc, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight);
}

void
ifb_erasecols(void *cookie, int row, int col, int num, long attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= ri->ri_font->fontheight;
	col *= ri->ri_font->fontwidth;
	num *= ri->ri_font->fontwidth;

	ifb_fillrect(sc, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight, ri->ri_devcmap[bg]);
}

void
ifb_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	ifb_copyrect(sc, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);
}

void
ifb_eraserows(void *cookie, int row, int num, long attr)
{
	struct rasops_info *ri = cookie;
	struct ifb_softc *sc = ri->ri_hw;
	int bg, fg;
	int x, y, w;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	if ((num == ri->ri_rows) && ISSET(ri->ri_flg, RI_FULLCLEAR)) {
		num = ri->ri_height;
		x = y = 0;
		w = ri->ri_width;
	} else {
		num *= ri->ri_font->fontheight;
		x = ri->ri_xorigin;
		y = ri->ri_yorigin + row * ri->ri_font->fontheight;
		w = ri->ri_emuwidth;
	}
	ifb_fillrect(sc, x, y, w, num, ri->ri_devcmap[bg]);
}

void
ifb_copyrect(struct ifb_softc *sc, int sx, int sy, int dx, int dy, int w, int h)
{
	ifb_rop(sc, sx, sy, dx, dy, w, h, IFB_ROP_SRC, IFB_PIXELMASK);
	ifb_rop_wait(sc);
}

void
ifb_fillrect(struct ifb_softc *sc, int x, int y, int w, int h, int bg)
{
	int32_t mask;

	/* pixels to set... */
	mask = IFB_PIXELMASK & bg;
	if (mask != 0) {
		ifb_rop(sc, x, y, x, y, w, h, IFB_ROP_SET, mask);
		ifb_rop_wait(sc);
	}

	/* pixels to clear... */
	mask = IFB_PIXELMASK & ~bg;
	if (mask != 0) {
		ifb_rop(sc, x, y, x, y, w, h, IFB_ROP_CLEAR, mask);
		ifb_rop_wait(sc);
	}
}

/*
 * Perform a raster operation on both overlay planes.
 * Puzzled by all the magic numbers in there? So are we. Isn't a dire
 * lack of documentation wonderful?
 */
void
ifb_rop(struct ifb_softc *sc, int sx, int sy, int dx, int dy, int w, int h,
    uint32_t rop, int32_t planemask)
{
	int dir = 0;

	/*
	 * Compute rop direction. This only really matters for
	 * screen-to-screen copies.
	 */
	if (sy < dy /* && sy + h > dy */) {
		sy += h - 1;
		dy += h;
		dir |= IFB_REG_MAGIC_DIR_BACKWARDS_Y;
	}
	if (sx < dx /* && sx + w > dx */) {
		sx += w - 1;
		dx += w;
		dir |= IFB_REG_MAGIC_DIR_BACKWARDS_X;
	}

	/* Which one of those below is your magic number for today? */
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 2);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 1);
	/* the ``0101'' part is probably a component selection */
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x540101ff);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x61000001);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x6301c080);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x80000000);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, rop);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, planemask);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x64000303);
	/*
	 * This value is a pixel offset within the destination area. It is
	 * probably used to define complex polygon shapes, with the
	 * last pixel in the list being back to (0,0).
	 */
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, IFB_COORDS(0, 0));
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x00030000);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x2200010d);

	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, 0x33f01000 | dir);
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, IFB_COORDS(dx, dy));
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, IFB_COORDS(w, h));
	bus_space_write_4(sc->sc_mem_t, sc->sc_reg_h,
	    IFB_REG_OFFSET + IFB_REG_MAGIC, IFB_COORDS(sx, sy));
}

void
ifb_rop_wait(struct ifb_softc *sc)
{
	int i;

	for (i = 1000000; i > 0; i--) {
		if (bus_space_read_4(sc->sc_mem_t, sc->sc_reg_h,
		    IFB_REG_OFFSET + IFB_REG_STATUS) & IFB_REG_STATUS_DONE)
			break;
		DELAY(1);
	}
}

void
ifb_do_cursor(struct rasops_info *ri)
{
	struct ifb_softc *sc = ri->ri_hw;
	int y, x;

	y = ri->ri_yorigin + ri->ri_crow * ri->ri_font->fontheight;
	x = ri->ri_xorigin + ri->ri_ccol * ri->ri_font->fontwidth;

	ifb_rop(sc, x, y, x, y, ri->ri_font->fontwidth, ri->ri_font->fontheight,
	    IFB_ROP_XOR, IFB_PIXELMASK);
	ifb_rop_wait(sc);
}
