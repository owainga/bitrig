/*	$OpenBSD: swapgeneric.c,v 1.6 2001/05/30 20:37:54 miod Exp $	*/
/*	$NetBSD: swapgeneric.c,v 1.14 1995/04/26 23:30:08 gwr Exp $	*/

/*
 * Copyright (c) 1994 Gordon W. Ross
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Gordon W. Ross
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Generic swap locations
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>

int (*mountroot) __P((void)) = NULL;	/* tells autoconf.c that we are "generic" */

dev_t	rootdev = NODEV;
dev_t	dumpdev = NODEV;

struct	swdevt swdevt[] = {
	{ makedev(7, 1),	0,	0 },	/* sd0b */
	{ makedev(7, 17),	0,	0 },	/* sd1b */
	{ makedev(7, 33),	0,	0 },	/* sd2b */
	{ makedev(7, 49),	0,	0 },	/* sd3b */
	{ makedev(3, 1),	0,	0 },	/* xy0b */
	{ makedev(3, 17),	0,	0 },	/* xy1b */
	{ makedev(10, 1),	0,	0 },	/* xd0b */
	{ makedev(10, 17),	0,	0 },	/* xd1b */
	{ NODEV,		0,	0 },
	{ NODEV,		0,	0 }
};
