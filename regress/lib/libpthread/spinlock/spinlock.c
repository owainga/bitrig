/*	$OpenBSD: spinlock.c,v 1.1 2012/05/03 09:07:17 pirofti Exp $	*/
/* Paul Irofti <pirofti@openbsd.org>, 2012. Public Domain. */
/* Ariane van der Steldt <ariane@stack.nl>, 2012. Public Domain. */

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "test.h"

void *
foo(void *arg)
{
	int rc, loop;
	pthread_spinlock_t *l = (pthread_spinlock_t*)arg;

	/* First acquire the lock once, to validate it actually works. */
	CHECKr(pthread_spin_lock(l));
	printf("foo(): Acquired spinlock\n");
	CHECKr(pthread_spin_unlock(l));

	for (loop = 0; loop < 100; loop++) {
		rc = pthread_spin_trylock(l);
		if (rc != 0 && rc != EBUSY) {
			PANIC("pthread_trylock returned %d", rc);
		}
		if (rc == 0) {
			CHECKr(pthread_spin_unlock(l));
		}
		CHECKr(pthread_spin_lock(l));
		CHECKr(pthread_spin_unlock(l));
	}
	return NULL;
}

pid_t children[10];

int main()
{
	int i;
	pthread_t thr[10];
	pthread_spinlock_t l, *pl;
	int status;
	extern int _rthread_ncpu;

	printf("Spinlock test, using 10 threads/processes on %d cpus.\n",
	    _rthread_ncpu);

	/*
	 * Test process private spinlocks.
	 */
	CHECKr(pthread_spin_init(&l, PTHREAD_PROCESS_PRIVATE));
	CHECKr(pthread_spin_lock(&l));
	for (i = 0; i < 10; i++) {
		printf("Thread %d started\n", i);
		CHECKr(pthread_create(&thr[i], NULL, foo, (void *)&l));
	}
	CHECKr(pthread_spin_unlock(&l));
	for (i = 0; i < 10; i++) {
		CHECKr(pthread_join(thr[i], NULL));
	}
	CHECKr(pthread_spin_destroy(&l));

	/*
	 * Create process shared mutex.
	 */
	_CHECK(pl = mmap(NULL, sizeof(*pl), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_INHERIT | MAP_SHARED, -1, 0),
	    != MAP_FAILED, strerror(errno));

	CHECKr(pthread_spin_init(pl, PTHREAD_PROCESS_SHARED));
	CHECKr(pthread_spin_lock(pl));
	for (i = 0; i < 10; i++) {
		switch (children[i] = fork()) {
		case 0:
			foo(pl);
			_exit(0);
		case -1:
			PANIC("fork failed: %s", strerror(errno));
			break;
		default:
			printf("Process %d started\n", i);
		}
	}
	CHECKr(pthread_spin_unlock(pl));
	for (i = 0; i < 10; i++) {
		int wpid;

		while ((wpid = waitpid(children[i], &status, 0)) == -1) {
			if (errno == EINTR)
				continue;
			perror("waitpid");
		}

		if (WIFSIGNALED(status)) {
			PANIC("child %d died due to signal %d%s",
			    children[i], WTERMSIG(signal),
			    (WCOREDUMP(status) ? " core dumped" : ""));
		}
		if (!WIFEXITED(status))
			PANIC("waitpid returned for non-exited child");
		if (WEXITSTATUS(status) != 0) {
			PANIC("child %d exited with return %d",
			    children[i], WEXITSTATUS(status));
		}
	}
	CHECKr(pthread_spin_destroy(pl));

	SUCCEED;
}
