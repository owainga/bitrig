#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mutex.h>
#include <machine/intr.h>


void
mtx_init(struct mutex *mtx, int ipl)
{
	mtx->mtx_wantipl = ipl;
	mtx->mtx_oldipl = IPL_NONE;
	mtx->mtx_owner = NULL;
	atomic_init(&mtx->mtx_ticket, 0);
	atomic_init(&mtx->mtx_cur, 0);
}

/* Store old ipl and current owner in mutex. */
static __inline void
mtx_store(struct mutex *mtx, int ipl)
{
	/* Ensure mutex is ownerless and valid. */
	KASSERT(mtx->mtx_owner == NULL);
	KASSERT(mtx->mtx_oldipl == IPL_NONE);

	mtx->mtx_owner = curcpu();
	mtx->mtx_oldipl = ipl;
}

/* Load old ipl and owner from mutex. */
static __inline int
mtx_clear(struct mutex *mtx)
{
	int oldipl;

	/* Ensure we own the mutex. */
	KASSERT(mtx->mtx_owner == curcpu());

	mtx->mtx_owner = NULL;
	oldipl = mtx->mtx_oldipl;
	mtx->mtx_oldipl = IPL_NONE;
	return oldipl;
}

void
mtx_enter(struct mutex *mtx)
{
	int s = IPL_NONE;
	unsigned int t;

	/* Block interrupts. */
	if (mtx->mtx_wantipl != IPL_NONE)
		s = splraise(mtx->mtx_wantipl);

	/* Acquire ticket. */
	t = atomic_fetch_add_explicit(&mtx->mtx_ticket,
	    1, memory_order_acquire);
	/* Wait for our turn. */
	while (atomic_load_explicit(&mtx->mtx_cur, memory_order_acquire) != t)
		SPINWAIT();

	/* Save old ipl. */
	mtx_store(mtx, s);
}

int
mtx_enter_try(struct mutex *mtx)
{
	int s = IPL_NONE;
	unsigned int t;

	/* Block interrupts. */
	if (mtx->mtx_wantipl != IPL_NONE)
		s = splraise(mtx->mtx_wantipl);

	/* Figure out which ticket is active. */
	t = atomic_load_explicit(&mtx->mtx_cur, memory_order_relaxed);
	/* Take ticket t, if it is available. */
	if (atomic_compare_exchange_strong_explicit(&mtx->mtx_ticket, &t, t + 1,
	    memory_order_acquire, memory_order_relaxed)) {
		/* We hold the lock, save old ipl. */
		mtx_store(mtx, s);
		return 1;
	}

	/* Lock is unavailable. */
	splx(s);
	return 0;
}

void
mtx_leave(struct mutex *mtx)
{
	int s = mtx_clear(mtx);

	/* Pass mutex to next-in-line. */
	atomic_fetch_add_explicit(&mtx->mtx_cur, 1, memory_order_release);
	/* Clear interrupt block. */
	if (mtx->mtx_wantipl != IPL_NONE)
		splx(s);
}
