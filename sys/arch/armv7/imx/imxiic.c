/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <machine/bus.h>
#include <machine/clock.h>

#include <armv7/armv7/armv7var.h>
#include <armv7/imx/imxiomuxcvar.h>
#include <armv7/imx/imxccmvar.h>
#include <armv7/imx/imxiicvar.h>

/* registers */
#define I2C_IADR	0x00
#define I2C_IFDR	0x04
#define I2C_I2CR	0x08
#define I2C_I2SR	0x0C
#define I2C_I2DR	0x10

#define I2C_I2CR_RSTA	(1 << 2)
#define I2C_I2CR_TXAK	(1 << 3)
#define I2C_I2CR_MTX	(1 << 4)
#define I2C_I2CR_MSTA	(1 << 5)
#define I2C_I2CR_IIEN	(1 << 6)
#define I2C_I2CR_IEN	(1 << 7)
#define I2C_I2SR_RXAK	(1 << 0)
#define I2C_I2SR_IIF	(1 << 1)
#define I2C_I2SR_IBB	(1 << 5)

struct imxiic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	void			*sc_ih;
	int			unit;

	struct rwlock		sc_buslock;
	struct i2c_controller	i2c_tag;

	uint16_t		frequency;
	uint16_t		intr_status;
	uint16_t		stopped;

	struct clk 		*sc_clk;
};

void imxiic_attach(struct device *, struct device *, void *);
int imxiic_detach(struct device *, int);
void imxiic_setspeed(struct imxiic_softc *, u_int);
int imxiic_intr(void *);
int imxiic_wait_intr(struct imxiic_softc *, int, int);
int imxiic_wait_state(struct imxiic_softc *, uint32_t, uint32_t);
int imxiic_start(struct imxiic_softc *, int, int, void *, int);
int imxiic_read(struct imxiic_softc *, int, int, void *, int);
int imxiic_write(struct imxiic_softc *, int, int, const void *, int);

int imxiic_i2c_acquire_bus(void *, int);
void imxiic_i2c_release_bus(void *, int);
int imxiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);

#define HREAD2(sc, reg)							\
	(bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE2(sc, reg, val)						\
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET2(sc, reg, bits)						\
	HWRITE2((sc), (reg), HREAD2((sc), (reg)) | (bits))
#define HCLR2(sc, reg, bits)						\
	HWRITE2((sc), (reg), HREAD2((sc), (reg)) & ~(bits))


struct cfattach imxiic_ca = {
	sizeof(struct imxiic_softc), NULL, imxiic_attach, imxiic_detach
};

struct cfdriver imxiic_cd = {
	NULL, "imxiic", DV_DULL
};

void
imxiic_attach(struct device *parent, struct device *self, void *args)
{
	struct imxiic_softc *sc = (struct imxiic_softc *)self;
	struct armv7_attach_args *aa = args;
	char i2c[5];

	sc->sc_iot = aa->aa_iot;
	sc->sc_ios = aa->aa_dev->mem[0].size;
	sc->unit = aa->aa_dev->unit;
	if (bus_space_map(sc->sc_iot, aa->aa_dev->mem[0].addr,
	    aa->aa_dev->mem[0].size, 0, &sc->sc_ioh))
		panic("imxiic_attach: bus_space_map failed!");

#if 0
	sc->sc_ih = arm_intr_establish(aa->aa_dev->irq[0], IPL_BIO,
	    imxiic_intr, sc, sc->sc_dev.dv_xname);
#endif

	snprintf(i2c, sizeof(i2c), "i2c%d", aa->aa_dev->unit + 1);
	sc->sc_clk = clk_get(i2c);
	if (sc->sc_clk == NULL)
		panic("imxiic_attach: clock not available");
	clk_enable(sc->sc_clk);

	printf("\n");

	/* set iomux pins */
	imxiomuxc_enable_i2c(sc->unit);

	/* set speed to 100kHz */
	imxiic_setspeed(sc, 100);

	/* reset */
	HWRITE2(sc, I2C_I2CR, 0);
	HWRITE2(sc, I2C_I2SR, 0);

	sc->stopped = 1;
	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);

	struct i2cbus_attach_args iba;

	sc->i2c_tag.ic_cookie = sc;
	sc->i2c_tag.ic_acquire_bus = imxiic_i2c_acquire_bus;
	sc->i2c_tag.ic_release_bus = imxiic_i2c_release_bus;
	sc->i2c_tag.ic_exec = imxiic_i2c_exec;

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->i2c_tag;
	config_found(&sc->sc_dev, &iba, NULL);
}

void
imxiic_setspeed(struct imxiic_softc *sc, u_int speed)
{
	if (!sc->frequency) {
		uint32_t i2c_clk_rate;
		uint32_t div;
		int i;

		i2c_clk_rate = clk_get_rate(sc->sc_clk);
		div = (i2c_clk_rate + speed - 1) / speed;
		if (div < imxiic_clk_div[0][0])
			i = 0;
		else if (div > imxiic_clk_div[49][0])
			i = 49;
		else
			for (i = 0; imxiic_clk_div[i][0] < div; i++);

		sc->frequency = imxiic_clk_div[i][1];
	}

	HWRITE2(sc, I2C_IFDR, sc->frequency);
}

#if 0
int
imxiic_intr(void *arg)
{
	struct imxiic_softc *sc = arg;
	u_int16_t status;

	status = HREAD2(sc, I2C_I2SR);

	if (ISSET(status, I2C_I2SR_IIF)) {
		/* acknowledge the interrupts */
		HWRITE2(sc, I2C_I2SR,
		    HREAD2(sc, I2C_I2SR) & ~I2C_I2SR_IIF);

		sc->intr_status |= status;
		wakeup(&sc->intr_status);
	}

	return (0);
}

int
imxiic_wait_intr(struct imxiic_softc *sc, int mask, int timo)
{
	int status;
	int s;

	s = splbio();

	status = sc->intr_status & mask;
	while (status == 0) {
		if (tsleep(&sc->intr_status, PWAIT, "hcintr", timo)
		    == EWOULDBLOCK) {
			break;
		}
		status = sc->intr_status & mask;
	}
	status = sc->intr_status & mask;
	sc->intr_status &= ~status;

	splx(s);
	return status;
}
#endif

int
imxiic_wait_state(struct imxiic_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t state;
	int timeout;
	state = HREAD2(sc, I2C_I2SR);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (((state = HREAD2(sc, I2C_I2SR)) & mask) == value)
			return 0;
		delay(10);
	}
	return ETIMEDOUT;
}

int
imxiic_read(struct imxiic_softc *sc, int addr, int subaddr, void *data, int len)
{
	int i;

	HWRITE2(sc, I2C_I2DR, addr | 1);

	if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
		return (EIO);
	while(!(HREAD2(sc, I2C_I2SR) & I2C_I2SR_IIF));
	if (HREAD2(sc, I2C_I2SR) & I2C_I2SR_RXAK)
		return (EIO);

	HCLR2(sc, I2C_I2CR, I2C_I2CR_MTX);
	if (len)
		HCLR2(sc, I2C_I2CR, I2C_I2CR_TXAK);

	/* dummy read */
	HREAD2(sc, I2C_I2DR);

	for (i = 0; i < len; i++) {
		if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
			return (EIO);
		if (i == (len - 1)) {
			HCLR2(sc, I2C_I2CR, I2C_I2CR_MSTA | I2C_I2CR_MTX);
			imxiic_wait_state(sc, I2C_I2SR_IBB, 0);
			sc->stopped = 1;
		} else if (i == (len - 2)) {
			HSET2(sc, I2C_I2CR, I2C_I2CR_TXAK);
		}
		((uint8_t*)data)[i] = HREAD2(sc, I2C_I2DR);
	}

	return 0;
}

int
imxiic_write(struct imxiic_softc *sc, int addr, int subaddr, const void *data, int len)
{
	int i;

	HWRITE2(sc, I2C_I2DR, addr);

	if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
		return (EIO);
	if (HREAD2(sc, I2C_I2SR) & I2C_I2SR_RXAK)
		return (EIO);

	for (i = 0; i < len; i++) {
		HWRITE2(sc, I2C_I2DR, ((uint8_t*)data)[i]);
		if (imxiic_wait_state(sc, I2C_I2SR_IIF, I2C_I2SR_IIF))
			return (EIO);
		if (HREAD2(sc, I2C_I2SR) & I2C_I2SR_RXAK)
			return (EIO);
	}
	return 0;
}

int
imxiic_i2c_acquire_bus(void *cookie, int flags)
{
	struct imxiic_softc *sc = cookie;

	return (rw_enter(&sc->sc_buslock, RW_WRITE));
}

void
imxiic_i2c_release_bus(void *cookie, int flags)
{
	struct imxiic_softc *sc = cookie;

	(void) rw_exit(&sc->sc_buslock);
}

int
imxiic_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct imxiic_softc *sc = cookie;
	uint32_t ret = 0;
	u_int8_t cmd = 0;

	if (!I2C_OP_STOP_P(op) || cmdlen > 1)
		return (EINVAL);

	if (cmdlen > 0)
		cmd = *(u_int8_t *)cmdbuf;

	addr &= 0x7f;

	/* set speed to 100kHz */
	imxiic_setspeed(sc, 100);

	/* enable the controller */
	HWRITE2(sc, I2C_I2SR, 0);
	HWRITE2(sc, I2C_I2CR, I2C_I2CR_IEN);

	/* wait for it to be stable */
	delay(50);

	/* start transaction */
	HSET2(sc, I2C_I2CR, I2C_I2CR_MSTA);

	if (imxiic_wait_state(sc, I2C_I2SR_IBB, I2C_I2SR_IBB)) {
		ret = (EIO);
		goto fail;
	}

	sc->stopped = 0;

	HSET2(sc, I2C_I2CR, I2C_I2CR_IIEN | I2C_I2CR_MTX | I2C_I2CR_TXAK);

	if (I2C_OP_READ_P(op)) {
		if (imxiic_read(sc, (addr << 1), cmd, buf, len) != 0)
			ret = (EIO);
	} else {
		if (imxiic_write(sc, (addr << 1), cmd, buf, len) != 0)
			ret = (EIO);
	}

fail:
	if (!sc->stopped) {
		HCLR2(sc, I2C_I2CR, I2C_I2CR_MSTA | I2C_I2CR_MTX);
		imxiic_wait_state(sc, I2C_I2SR_IBB, 0);
		sc->stopped = 1;
	}

	HWRITE2(sc, I2C_I2CR, 0);

	return ret;
}

int
imxiic_detach(struct device *self, int flags)
{
	struct imxiic_softc *sc = (struct imxiic_softc *)self;

	HWRITE2(sc, I2C_IADR, 0);
	HWRITE2(sc, I2C_IFDR, 0);
	HWRITE2(sc, I2C_I2CR, 0);
	HWRITE2(sc, I2C_I2SR, 0);

	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}
