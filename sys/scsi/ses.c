/*	$OpenBSD: ses.c,v 1.11 2005/08/03 15:00:26 dlg Exp $ */

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/scsiio.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/sensors.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <scsi/ses.h>

#ifdef SES_DEBUG
#define DPRINTF(x...)		do { if (sesdebug) printf(x); } while (0)
#define DPRINTFN(n, x...)	do { if (sesdebug > (n)) printf(x); } while (0)
int	sesdebug = 2;
#else
#define DPRINTF(x...)		/* x */
#define DPRINTFN(n,x...)	/* n: x */
#endif

int	ses_match(struct device *, void *, void *);
void	ses_attach(struct device *, struct device *, void *);
int	ses_detach(struct device *, int);

struct ses_sensor {
	struct sensor		se_sensor;
	u_int8_t		se_type;
	struct ses_status	*se_stat;

	TAILQ_ENTRY(ses_sensor)	se_entry;
};

struct ses_softc {
	struct device		sc_dev;
	struct scsi_link	*sc_link;

	enum {
		SES_ST_NONE,
		SES_ST_OK,
		SES_ST_ERR
	}			sc_state;
	
	u_char			*sc_buf;
	ssize_t			sc_buflen;

	TAILQ_HEAD(, ses_sensor) sc_sensors;
	struct timeout		sc_timeout;
};

struct cfattach ses_ca = {
	sizeof(struct ses_softc), ses_match, ses_attach, ses_detach
};

struct cfdriver ses_cd = {
	NULL, "ses", DV_DULL
};

#define DEVNAME(s)	((s)->sc_dev.dv_xname)

#define SES_BUFLEN	2048 /* XXX is this enough? */

void	ses_refresh(void *);

int	ses_read_config(struct ses_softc *);
int	ses_read_status(struct ses_softc *, int refresh);
int	ses_make_sensors(struct ses_softc *, struct ses_type_desc *, int);
int	ses_refresh_sensors(struct ses_softc *);

void	ses_cool2sensor(struct ses_sensor *);
void	ses_temp2sensor(struct ses_sensor *);

#ifdef SES_DEBUG
void	ses_dump_enc_desc(struct ses_enc_desc *);
char	*ses_dump_enc_string(u_char *, ssize_t);
#endif

int
ses_match(struct device *parent, void *match, void *aux)
{
	struct scsibus_attach_args	*sa = aux;
	struct scsi_inquiry_data	*inq = sa->sa_inqbuf;

	if (inq == NULL)
		return (0);

	if ((inq->device & SID_TYPE) == T_ENCLOSURE &&
	    (inq->version & SID_ANSII) >= SID_ANSII_SCSI2)
		return (2);

	/* XXX apparently we can match on passthrough devs too? */

	return (0);
}

void
ses_attach(struct device *parent, struct device *self, void *aux)
{
	struct ses_softc		*sc = (struct ses_softc *)self;
	struct scsibus_attach_args	*sa = aux;

	sc->sc_link = sa->sa_sc_link;
	sc->sc_state = SES_ST_NONE;

	printf("\n");
	printf("%s: SCSI Enclosure Services\n", DEVNAME(sc));

	if (ses_read_config(sc) != 0) {
		printf("%s: unable to read enclosure configuration\n",
		    DEVNAME(sc));
		return;
	}

	sc->sc_state = SES_ST_OK;

	timeout_set(&sc->sc_timeout, ses_refresh, sc);
	timeout_add(&sc->sc_timeout, 10 * hz);
}

int
ses_detach(struct device *self, int flags)
{
	struct ses_softc		*sc = (struct ses_softc *)self;
	struct ses_sensor		*sensor;

	if (sc->sc_state != SES_ST_NONE) {
		timeout_del(&sc->sc_timeout);

		/*
		 * We cant free the sensors once theyre in the systems sensor
		 * list, so just mark them as invalid.
		 */
		TAILQ_FOREACH(sensor, &sc->sc_sensors, se_entry)
			sensor->se_sensor.flags |= SENSOR_FINVALID;

		free(sc->sc_buf, M_DEVBUF);
	}

	return (0);
}

void
ses_refresh(void *arg)
{
	struct ses_softc		*sc = arg;

	if (ses_refresh_sensors(sc) != 0) {
		if (sc->sc_state != SES_ST_ERR)
			printf("%s: error reading enclosure status\n",
			    DEVNAME(sc));
		sc->sc_state = SES_ST_ERR;
	} else {
		if (sc->sc_state != SES_ST_OK)
			printf("%s: reading enclosure status\n", DEVNAME(sc));
		sc->sc_state = SES_ST_OK;
	}

	timeout_add(&sc->sc_timeout, 10 * hz);
}

int
ses_read_config(struct ses_softc *sc)
{
	struct ses_scsi_diag		cmd;
	int				flags;

	u_char				*buf, *p;

	struct ses_config_hdr		*cfg;
	struct ses_enc_hdr		*enc;
#ifdef SES_DEBUG
	struct ses_enc_desc		*desc;
#endif
	struct ses_type_desc		*tdh, *tdlist;

	int				i, ntypes = 0, nelems = 0;

	buf = malloc(SES_BUFLEN, M_DEVBUF, M_NOWAIT);
	if (buf == NULL)
		return (1);

	memset(buf, 0, SES_BUFLEN);
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = RECEIVE_DIAGNOSTIC;
	cmd.flags |= SES_DIAG_PCV;
	cmd.pgcode = SES_PAGE_CONFIG;
	cmd.length = htobe16(SES_BUFLEN);
	flags = SCSI_DATA_IN;
#ifndef SCSIDEBUG
	flags |= SCSI_SILENT;
#endif

	if (scsi_scsi_cmd(sc->sc_link, (struct scsi_generic *)&cmd,
	    sizeof(cmd), buf, SES_BUFLEN, 2, 3000, NULL, flags) != 0) {
		free(buf, M_DEVBUF);
		return (1);
	}

	cfg = (struct ses_config_hdr *)buf;
	if (cfg->pgcode != cmd.pgcode || betoh16(cfg->length) > SES_BUFLEN) {
		free(buf, M_DEVBUF);
		return (1);
	}

	DPRINTF("%s: config: n_subenc: %d length: %d\n", DEVNAME(sc),
	    cfg->n_subenc, betoh16(cfg->length));

	p = buf + SES_CFG_HDRLEN;
	for (i = 0; i <= cfg->n_subenc; i++) {
		enc = (struct ses_enc_hdr *)p;
#ifdef SES_DEBUG
		DPRINTF("%s: enclosure %d enc_id: 0x%02x n_types: %d\n",
		    DEVNAME(sc), i, enc->enc_id, enc->n_types);
		desc = (struct ses_enc_desc *)(p + SES_ENC_HDRLEN);
		ses_dump_enc_desc(desc);
#endif /* SES_DEBUG */

		ntypes += enc->n_types;

		p += SES_ENC_HDRLEN + enc->vendor_len;
	}

	tdlist = (struct ses_type_desc *)p; /* stash this for later */

	for (i = 0; i < ntypes; i++) {
		tdh = (struct ses_type_desc *)p;
		DPRINTF("%s: td %d subenc_id: %d type 0x%02x n_elem: %d\n",
		    DEVNAME(sc), i, tdh->subenc_id, tdh->type, tdh->n_elem);

		nelems += tdh->n_elem;

		p += SES_TYPE_DESCLEN;
	}

#ifdef SES_DEBUG
	for (i = 0; i < ntypes; i++) {
		DPRINTF("%s: td %d '%s'\n", DEVNAME(sc), i,
		    ses_dump_enc_string(p, tdlist[i].desc_len));

		p += tdlist[i].desc_len;
	}
#endif /* SES_DEBUG */

	sc->sc_buflen = SES_STAT_LEN(ntypes, nelems);
	sc->sc_buf = malloc(sc->sc_buflen, M_DEVBUF, M_NOWAIT);
	if (sc->sc_buf == NULL) {
		free(buf, M_DEVBUF);
		return (1);
	}

	/* get the status page and then use it to generate a list of sensors */
	if (ses_make_sensors(sc, tdlist, ntypes) != 0) {
		free(buf, M_DEVBUF);
		free(sc->sc_buf, M_DEVBUF);
		return (1);
	}

	free(buf, M_DEVBUF);
	return (0);
}

int
ses_read_status(struct ses_softc *sc, int refresh)
{
	struct ses_scsi_diag		cmd;
	int				flags;

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = RECEIVE_DIAGNOSTIC;
	cmd.flags |= SES_DIAG_PCV;
	cmd.pgcode = SES_PAGE_STATUS;
	cmd.length = htobe16(sc->sc_buflen);
	flags = SCSI_DATA_IN;
#ifndef SCSIDEBUG
	flags |= SCSI_SILENT;
#endif
	if (refresh)
		flags |= SCSI_NOSLEEP;

	if (scsi_scsi_cmd(sc->sc_link, (struct scsi_generic *)&cmd,
	    sizeof(cmd), sc->sc_buf, sc->sc_buflen, 2, 3000, NULL, flags) != 0)
		return (1);

	/* XXX should we check any values in the status header? */

	return (0);
}

int
ses_make_sensors(struct ses_softc *sc, struct ses_type_desc *types, int ntypes)
{
	struct ses_status		*status;
	struct ses_sensor		*sensor;
	enum sensor_type		stype;
	char				*fmt;
	int				typecnt[SES_NUM_TYPES];
	int				i, j;

	if (ses_read_status(sc, 0) != 0)
		return (1);

	memset(typecnt, 0, sizeof(typecnt));
	TAILQ_INIT(&sc->sc_sensors);

	status = (struct ses_status *)(sc->sc_buf + SES_STAT_HDRLEN);
	for (i = 0; i < ntypes; i++) {
		/* ignore the overall status element for this type */
		DPRINTFN(1, "%s: %3d:-   0x%02x 0x%02x%02x%02x type: 0x%02x\n",
		     DEVNAME(sc), i, status->com, status->f1, status->f2,
		    status->f3, types[i].type);

		for (j = 0; j < types[i].n_elem; j++) {
			/* move to the current status element */
			status++;

			DPRINTFN(1, "%s: %3d:%-3d 0x%02x 0x%02x%02x%02x\n",
			    DEVNAME(sc), i, j, status->com, status->f1,
			    status->f2, status->f3);

			if (SES_STAT_CODE(status->com) == SES_STAT_CODE_NOTINST)
				continue;

			switch (types[i].type) {
			case SES_T_COOLING:
				stype = SENSOR_FANRPM;
				fmt = "fan%d";
				break;

			case SES_T_TEMP:
				stype = SENSOR_TEMP;
				fmt = "temp%d";
				break;

			default:
				continue;
			}

			sensor = malloc(sizeof(struct ses_sensor), M_DEVBUF,
			    M_NOWAIT);
			if (sensor == NULL)
				goto error;

			memset(sensor, 0, sizeof(struct ses_sensor));
			sensor->se_type = types[i].type;
			sensor->se_stat = status;
			sensor->se_sensor.type = stype;
			strlcpy(sensor->se_sensor.device, DEVNAME(sc),
			    sizeof(sensor->se_sensor.device));
			snprintf(sensor->se_sensor.desc,
			    sizeof(sensor->se_sensor.desc), fmt, 
			    typecnt[types[i].type]++);

			TAILQ_INSERT_TAIL(&sc->sc_sensors, sensor, se_entry);
		}

		/* move to the overall status element of the next type */
		status++;
	}

	TAILQ_FOREACH(sensor, &sc->sc_sensors, se_entry)
		SENSOR_ADD(&sensor->se_sensor);
	return (0);

error:
	while (!TAILQ_EMPTY(&sc->sc_sensors)) {
		sensor = TAILQ_FIRST(&sc->sc_sensors);
		TAILQ_REMOVE(&sc->sc_sensors, sensor, se_entry);
		free(sensor, M_DEVBUF);
	}
	return (1);
}

int
ses_refresh_sensors(struct ses_softc *sc)
{
	struct ses_sensor		*sensor;
	int				ret = 0;

	if (ses_read_status(sc, 1) != 0)
		return (1);

	TAILQ_FOREACH(sensor, &sc->sc_sensors, se_entry) {
		DPRINTFN(10, "%s: %s 0x%02x 0x%02x%02x%02x\n", DEVNAME(sc),
		    sensor->se_sensor.desc, sensor->se_stat->com,
		    sensor->se_stat->f1, sensor->se_stat->f2,
		    sensor->se_stat->f3);

		switch (SES_STAT_CODE(sensor->se_stat->com)) {
		case SES_STAT_CODE_OK:
			sensor->se_sensor.status = SENSOR_S_OK;
			break;

		case SES_STAT_CODE_CRIT:
		case SES_STAT_CODE_UNREC:
			sensor->se_sensor.status = SENSOR_S_CRIT;
			break;

		case SES_STAT_CODE_NONCRIT:
			sensor->se_sensor.status = SENSOR_S_WARN;
			break;

		case SES_STAT_CODE_NOTINST:
		case SES_STAT_CODE_UNKNOWN:
		case SES_STAT_CODE_NOTAVAIL:
			sensor->se_sensor.status = SENSOR_S_UNKNOWN;
			break;
		}

		switch (sensor->se_type) {
		case SES_T_COOLING:
			ses_cool2sensor(sensor);
			break;

		case SES_T_TEMP:
			ses_temp2sensor(sensor);
			break;

		default:
			ret = 1;
			break;
		}
	}

	return (ret);
}

void
ses_cool2sensor(struct ses_sensor *s)
{
	s->se_sensor.value = (int64_t)SES_S_COOL_SPEED(s->se_stat);
	s->se_sensor.value *= SES_S_COOL_FACTOR;

	/* if the fan is on but not showing an rpm then mark as unknown */
	if (SES_S_COOL_CODE(s->se_stat) != SES_S_COOL_C_STOPPED &&
	    s->se_sensor.value == 0)
		s->se_sensor.flags |= SENSOR_FUNKNOWN;
	else
		s->se_sensor.flags &= ~SENSOR_FUNKNOWN;
}

void
ses_temp2sensor(struct ses_sensor *s)
{
	s->se_sensor.value = (int64_t)SES_S_TEMP(s->se_stat);
	s->se_sensor.value += SES_S_TEMP_OFFSET;
	s->se_sensor.value *= 1000000; /* convert to micro (mu) degrees */
	s->se_sensor.value += 273150000; /* convert to kelvin */
}

#ifdef SES_DEBUG
void
ses_dump_enc_desc(struct ses_enc_desc *desc)
{
	char				str[32];

#if 0
	/* XXX not a string. wwn? */
	memset(str, 0, sizeof(str));
	memcpy(str, desc->logical_id, sizeof(desc->logical_id));
	DPRINTF("logical_id: %s", str);
#endif

	memset(str, 0, sizeof(str));
	memcpy(str, desc->vendor_id, sizeof(desc->vendor_id));
	DPRINTF(" vendor_id: %s", str);

	memset(str, 0, sizeof(str));
	memcpy(str, desc->prod_id, sizeof(desc->prod_id));
	DPRINTF(" prod_id: %s", str);

	memset(str, 0, sizeof(str));
	memcpy(str, desc->prod_rev, sizeof(desc->prod_rev));
	DPRINTF(" prod_rev: %s\n", str);
}

char *
ses_dump_enc_string(u_char *buf, ssize_t len)
{
	static char			str[256];

	memset(str, 0, sizeof(str));
	if (len > 0)
		memcpy(str, buf, len);

	return (str);
}
#endif /* SES_DEBUG */
