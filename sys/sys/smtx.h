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
#ifndef _SYS_SMTX_H_
#define _SYS_SMTX_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sleepyhead.h>

struct smtx {
	struct sleepyhead	 sm_queue;
	int			 sm_priority;
	_Atomic(struct proc *)	 sm_owner;
};


static __inline int
smtx_enter_try(struct smtx *sm)
{
	struct proc	*expect = NULL;

	/*
	 * 1 function call, 1 instruction.
	 */
	return (zzz_empty(&sm->sm_queue) &&
	    atomic_compare_exchange_strong_explicit(&sm->sm_owner,
	     &expect, curproc, memory_order_acquire, memory_order_relaxed));
}

static __inline void
smtx_leave(struct smtx *sm)
{
	struct proc	*prv_owner;

	/*
	 * 1 instruction.
	 * 1 function call.
	 */
	prv_owner = atomic_exchange_explicit(&sm->sm_owner, NULL,
	    memory_order_release);
	if (prv_owner != curproc)
		panic("smtx_leave: did not own sleepable mutex");

	zzz_wakeup(&sm->sm_queue);
}

#define SMTX_INITIALIZER(sm, wmsg, priority)				\
	{								\
		SLEEPYHEAD_INITIALIZER((sm).sm_queue, (wmsg)),		\
		(priority),						\
	}

void	 smtx_init(struct smtx *, const char *, int);
int	 smtx_enter(struct smtx *, int, int);

#endif /* _SYS_SMTX_H_ */
