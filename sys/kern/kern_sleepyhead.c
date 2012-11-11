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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sleepyhead.h>
#include <sys/types.h>
#include <sys/proc.h>


/* A single sleeper on the queue. */
struct snore {
	struct ll_elem		 zzz_link;	/* Link into list. */
	struct proc		*zzz_proc;	/* Proc owning this snore. */
	_Atomic(struct sleepyhead *)
				 zzz_sh;	/* Sleep queue. */
};

/* Inlined access functions. */
LL_GENERATE(sleepyhead, snore, zzz_link);

/*
 * Wakeup a number of sleeping procs on the list.
 * Returns the number of awoken procs.
 * If n is -1, all processes are awoken.
 *
 * This function does not sleep, but it may spin.
 */
int
zzz_wakeup_n(struct sleepyhead *sh, int n)
{
	struct snore	*s;
	int		 i;

	if (n == -1)
		n = LL_SIZE(sleepyhead, sh);

	for (i = 0; i < n; i++) {
		s = LL_POP_FRONT(sleepyhead, sh);
		if (!s)
			break;
		wakeup_proc(s->zzz_proc);
		LL_RELEASE(sleepyhead, sh, s);
	}
	return i;
}

/*
 * Wakeup a single proc on the list.
 *
 * This function does not sleep, but it may spin.
 */
void
zzz_wakeup(struct sleepyhead *sh)
{
	zzz_wakeup_n(sh, 1);
}

/*
 * Transfer sleepers to a different queue.
 * Returns the number of transfered sleepers.
 * If n is -1, all processes are transfered.
 *
 * sh_src and sh_dst may be the same queue, in which case sleepers
 * are moved from the front to the back.  Transfer does not incur
 * a context switch.
 *
 * This function does not sleep, but it may spin.
 */
int
zzz_transfer(struct sleepyhead *sh_src, struct sleepyhead *sh_dst, int n)
{
	struct sleepyhead	*expect;
	struct snore		*s;
	int			 i;

	if (n == -1)
		n = LL_SIZE(sleepyhead, sh_src);

	for (i = 0; i < n; i++) {
		/* Acquire a snore that is has not awoken. */
		while ((s = LL_POP_FRONT_NOWAIT(sleepyhead, sh_src))) {
			/* Atomically change which queue this snore is bound to. */
			expect = sh_src;
			if (atomic_compare_exchange_strong_explicit(&s->zzz_sh,
			    &expect, sh_dst,
			    memory_order_relaxed, memory_order_relaxed))
				break;	/* GUARD */

			/* Cannot change queue if s. */
			KDASSERT(expect == NULL);
			LL_UNLINK_WAIT(sleepyhead, sh_src, s);
			LL_RELEASE(sleepyhead, sh_src, s);
		}
		if (!s)
			break;

		/* Don't touch ident, or things will break! */
		s->zzz_proc->p_wmesg = sh_dst->sh_wmesg;

		LL_UNLINK_WAIT_INSERT_TAIL(sleepyhead, sh_dst, s);
		LL_RELEASE(sleepyhead, sh_dst, s);
	}
	return i;
}

/*
 * Sleep for a sleepable.
 *
 * Note: the predicate is run with sched_lock held.
 *
 * If the predicate returns a non-zero value, this function will not sleep
 * and return the predicate error, unless an interuption (EINTR/ERESTART)
 * occured.
 */
int
zzz(struct sleepyhead *sh, int priority, int timo,
    sleepyhead_predicate pred, void *pred_arg,
    struct sleepyhead **sh_wakeup)
{
	struct sleep_state	 sls;
	struct snore		 s;
	int			 error, time_err, pred_error = 0, unlink;

	assertwaitok();

	/* Initialize snore data. */
	LL_INIT_ENTRY(&s.zzz_link);
	s.zzz_proc = curproc;
	atomic_init(&s.zzz_sh, sh);

	/* Setup sleep state. */
	sleep_setup(&sls, sh, priority, sh->sh_wmesg);
	sleep_setup_timeout(&sls, timo);
	sleep_setup_signal(&sls, priority);

	/* Test if the predicate holds. */
	if (pred && (pred_error = pred(pred_arg)))
		sls.sls_do_sleep = 0;

	/* Put us on the queue (only if we intend to sleep). */
	if (sls.sls_do_sleep)
		LL_PUSH_BACK(sleepyhead, sh, &s);

	/* Actual sleep. */
	sleep_finish(&sls, 1);

	/* Figure out which queue we awoke on. */
	sh = atomic_exchange_explicit(&s.zzz_sh, NULL, memory_order_relaxed);
	if (sh_wakeup)
		*sh_wakeup = sh;

	/* Unlink from sleepyhead. */
	LL_REF(sleepyhead, sh, &s);
	unlink = (LL_UNLINK_ROBUST(sleepyhead, sh, &s) != NULL);
	/*
	 * Technically, we should release after unlink,
	 * but unlink_robust is special, since it is always the final
	 * unlink that an object can have and we are guaranteed to hold
	 * the only reference.
	 */

	/* XXX clear s.zzz_proc->p_wmesg? */

	/* Collect result. */
	time_err = sleep_finish_timeout(&sls);
	error = sleep_finish_signal(&sls);

	/*
	 * Signals take precedence over everything.
	 * Predicate failure takes precedence over the rest.
	 *
	 * If unlink succeeded, we were woken up, otherwise we were not.
	 * If an unlink succeeded while a timeout also ticked, the succesful
	 * wakeup takes precedence.
	 */
	if (error != 0)
		return error;
	if (pred_error != 0)
		return pred_error;
	if (!unlink)
		return 0;
	if (time_err != 0)
		return time_err;

	/* UNREACHABLE */
	panic("zzz: completed without errors, but was not woken up "
	    "(did someone call wakeup?)");
}
