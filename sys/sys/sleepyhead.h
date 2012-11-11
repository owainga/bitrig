/*
 * Copyright (c) 2012 Ariane van der Steldt <ariane@stack.nl>
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
#ifndef _SYS_SLEEPYHEAD_H
#define _SYS_SLEEPYHEAD_H

#include <sys/ll.h>
#include <sys/stdatomic.h>

/*
 * Kernel synchronization structures.
 *
 * sleepyhead:
 *     A queue of sleepers, which are awakened or transfered in fifo order.
 *     This is a primitive for most things that can be slept on.
 */


/* Something on which you can sleep. */
struct sleepyhead {
	struct ll_head		 ll_head;	/* Sleep queue head. */
	const char		*sh_wmesg;	/* Wmesg of this sleepyhead. */
};

/* Callback for conditional sleep. */
typedef int (*sleepyhead_predicate)(void *);


#define SLEEPYHEAD_INITIALIZER(sh, wmsg)				\
	{ LL_HEAD_INITIALIZER__HEAD((sh).ll_head), (wmsg) }

void	 zzz_init(struct sleepyhead *, const char *);
int	 zzz_wakeup_n(struct sleepyhead *, int);
void	 zzz_wakeup(struct sleepyhead *);
int	 zzz_transfer(struct sleepyhead *, struct sleepyhead *, int);
int	 zzz(struct sleepyhead *, int, int, sleepyhead_predicate, void *,
	    struct sleepyhead **);
int	 zzz_empty(struct sleepyhead *);

#endif /* _SYS_SLEEPYHEAD_H */
