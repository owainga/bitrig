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
#include <sys/smtx.h>

void
smtx_init(struct smtx *sm, const char *wmsg, int priority)
{
	zzz_init(&sm->sm_queue, wmsg);
	atomic_init(&sm->sm_owner, (struct proc *)NULL);
}

/* Sleep predicate: someone holds the lock. */
static int
smtx_pred(struct smtx *sm)
{
	struct proc	*owner;

	owner = atomic_load_explicit(&sm->sm_owner, memory_order_relaxed);
	return (owner != NULL && owner != curproc);
}

int
smtx_enter(struct smtx *sm, int timo, int allow_intr)
{
	int		 zzz_err;
	struct proc	*prev_owner;
	int		 prio;

	assertwaitok();

	prio = sm->sm_priority;
	if (!allow_intr)
		prio &= ~PCATCH;

	do {
		/* Attempt to bypass sleeping first. */
		if (smtx_enter_try(sm))
			return 0;

		/* Sleep until it is our turn. */
		zzz_err = zzz(&sm->sm_queue, prio, timo,
		    (int(*)(void *))smtx_pred, sm, NULL);
	} while (zzz_err == EBUSY);
	if (zzz_err)
		return zzz_err;

	/* Assign ourselves as the new owner. */
	prev_owner = atomic_exchange_explicit(&sm->sm_owner, curproc,
	    memory_order_acquire);
	KASSERT(prev_owner == NULL);
	return 1;
}
