/*	$OpenBSD: rthread_spin_lock.c,v 1.2 2012/05/06 10:01:18 pirofti Exp $	*/
/*
 * Copyright (c) 2012 Paul Irofti <pirofti@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <errno.h>
#include <stdlib.h>

#define _PTHREAD_SPIN_INLINE	/* Not inline. */
#include <pthread.h>

#include "rthread.h"

int
_pthread_spin_lock_blocked(pthread_spinlock_t *spl)
{
	unsigned int	w, cur;
	int		spin, maxspin;

	if (!spl)
		return EINVAL;

	/*
	 * Acquire a ticket, we only synchronize on the end of the window,
	 * any other global state is irrelevant at this point.
	 */
	w = atomic_fetch_add_explicit(&spl->pspl_wend, 1,
	    memory_order_relaxed);

	/*
	 * Try to acquire the lock immediately.
	 * Memory_order_acquire: forces the thread that released the lock
	 * to send us their cache.
	 */
	cur = w;
	if (atomic_compare_exchange_weak_explicit(&spl->pspl_wstart, &cur, w,
	    memory_order_acquire, memory_order_relaxed))
		return 0;

	/*
	 * Set maxspin.
	 * Default is 1024, unless this machine is not SMP capable.
	 */
	spin = maxspin = (_rthread_ncpu <= 1 ? 0 : 1024);

	/*
	 * Wait until the spinlock activates our ticket.
	 *
	 * If the lock queue exceeds the number of cpus, we yield
	 * unconditionally.
	 * If we are the next in line (and the above did not happen)
	 * we spin unconditionally.
	 * Otherwise, we mix spinning and yielding.
	 */
	while ((cur = atomic_load_explicit(&spl->pspl_wstart,
	    memory_order_relaxed)) != w) {
		if (w - cur > (unsigned)_rthread_ncpu ||
		    (w - cur > 1 && spin == 0)) {
			sched_yield();
			spin = maxspin;
		} else {
			CPU_SPINWAIT;
			spin--;
		}
	}

	/* We now own the lock.  Force synchronization of globals. */
	atomic_thread_fence(memory_order_acquire);
	return 0;
}
