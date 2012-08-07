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

	spl->pspl_owner = getthrid();
	return 0;
}

int
pthread_spin_init(pthread_spinlock_t *spl, int pshared)
{
	if (!spl)
		return EINVAL;
	if (pshared != PTHREAD_PROCESS_PRIVATE &&
	    pshared != PTHREAD_PROCESS_SHARED)
		return EINVAL;

	atomic_init(&spl->pspl_wstart, 0);
	atomic_init(&spl->pspl_wend, 0);
	spl->pspl_owner = 0;
	return 0;
}

int
pthread_spin_destroy(pthread_spinlock_t *spl)
{
	if (!spl)
		return EINVAL;

	/* Check that the spinlock is unlocked. */
	if (atomic_load_explicit(&spl->pspl_wstart, memory_order_relaxed) !=
	    atomic_load_explicit(&spl->pspl_wend, memory_order_relaxed))
		return EBUSY;
	return 0;
}

int
pthread_spin_trylock(pthread_spinlock_t *spl)
{
	unsigned int w;

	if (!spl)
		return EINVAL;

	w = atomic_load_explicit(&spl->pspl_wstart, memory_order_relaxed);
	if (!atomic_compare_exchange_strong_explicit(&spl->pspl_wend,
	    &w, w + 1,
	    memory_order_acquire, memory_order_relaxed))
		return EBUSY;
	spl->pspl_owner = getthrid();
	return 0;
}

int
pthread_spin_lock(pthread_spinlock_t *spl)
{
	if (!spl)
		return EINVAL;

	/*
	 * Try to use the trylock first.  Inline since it is only a few
	 * instructions.
	 * If the trylock fails, perform the spin_lock_blocked, which
	 * will do all the spinning magic.
	 */
	if (pthread_spin_trylock(spl) == 0)
		return 0;
	return _pthread_spin_lock_blocked(spl);
}

int
pthread_spin_unlock(pthread_spinlock_t *spl)
{
	if (!spl)
		return EINVAL;

	/* Check that the spinlock is indeed locked. */
	if (atomic_load_explicit(&spl->pspl_wstart, memory_order_relaxed) ==
	    atomic_load_explicit(&spl->pspl_wend, memory_order_relaxed))
		return EPERM;
	/* Check that the lock is ours. */
	if (spl->pspl_owner != getthrid())
		return EPERM;
	spl->pspl_owner = 0;

	/* Grant ticket to next in line. */
	atomic_fetch_add_explicit(&spl->pspl_wstart, 1, memory_order_release);
	return 0;
}
