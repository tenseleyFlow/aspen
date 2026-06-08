#include "pool.h"
#include "util.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

/* The pthread *functions* are resolved lazily — on first asp_pool_create() — so
 * a serial run never pays the thread runtime's startup cost. On Linux/musl/macOS
 * pthread lives in libc, already loaded, so RTLD_DEFAULT finds the symbols for
 * free. On FreeBSD pthread is a separate libthr we deliberately DON'T link; its
 * constructor (signal setup, thread-stack mmaps — ~25 syscalls) would otherwise
 * run at startup for every invocation, making a tiny `-L 1` walk slower than
 * tree. We dlopen it here instead, so it initializes only when a pool is built.
 * (The pthread_t/mutex_t/cond_t *types* need no linking — they're plain structs.) */
typedef int (*pt_create_fn)(pthread_t *, const pthread_attr_t *,
			    void *(*)(void *), void *);
typedef int (*pt_join_fn)(pthread_t, void **);
typedef int (*pt_mutex_fn)(pthread_mutex_t *);
typedef int (*pt_mutex_init_fn)(pthread_mutex_t *, const pthread_mutexattr_t *);
typedef int (*pt_cond_fn)(pthread_cond_t *);
typedef int (*pt_cond_init_fn)(pthread_cond_t *, const pthread_condattr_t *);
typedef int (*pt_cond_wait_fn)(pthread_cond_t *, pthread_mutex_t *);

static struct {
	pt_create_fn create;
	pt_join_fn join;
	pt_mutex_init_fn mutex_init;
	pt_mutex_fn mutex_destroy, mutex_lock, mutex_unlock;
	pt_cond_init_fn cond_init;
	pt_cond_fn cond_destroy, cond_signal, cond_broadcast;
	pt_cond_wait_fn cond_wait;
} P;

static int pthr_state = -1; /* -1 unprobed, 0 unavailable, 1 loaded */

/* POSIX guarantees dlsym yields a callable pointer; the cast through void** is
 * the standard way to dodge the ISO "object vs function pointer" -Wpedantic. */
static int load_sym(void *h, void *slot, const char *name)
{
	void *s = dlsym(h, name);
	if (!s)
		return 0;
	*(void **)slot = s;
	return 1;
}

static int load_pthread(void)
{
	if (pthr_state >= 0)
		return pthr_state;
	pthr_state = 0;

	void *h = RTLD_DEFAULT; /* pthread in libc (Linux >=2.34, musl, macOS) */
	if (!dlsym(h, "pthread_create")) {
		/* FreeBSD / old glibc: load the thread library on demand. */
		static const char *const libs[] = { "libthr.so.3", "libthr.so",
						    "libpthread.so.0", "libpthread.so", NULL };
		h = NULL;
		for (int i = 0; libs[i]; i++)
			if ((h = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL)))
				break;
		if (!h)
			return 0;
	}

	if (load_sym(h, &P.create, "pthread_create") &&
	    load_sym(h, &P.join, "pthread_join") &&
	    load_sym(h, &P.mutex_init, "pthread_mutex_init") &&
	    load_sym(h, &P.mutex_destroy, "pthread_mutex_destroy") &&
	    load_sym(h, &P.mutex_lock, "pthread_mutex_lock") &&
	    load_sym(h, &P.mutex_unlock, "pthread_mutex_unlock") &&
	    load_sym(h, &P.cond_init, "pthread_cond_init") &&
	    load_sym(h, &P.cond_destroy, "pthread_cond_destroy") &&
	    load_sym(h, &P.cond_signal, "pthread_cond_signal") &&
	    load_sym(h, &P.cond_broadcast, "pthread_cond_broadcast") &&
	    load_sym(h, &P.cond_wait, "pthread_cond_wait"))
		pthr_state = 1;
	return pthr_state;
}

struct asp_pool {
	pthread_t *threads;
	int nworkers;

	pthread_mutex_t mtx;
	pthread_cond_t ready; /* workers wait here for a job */
	pthread_cond_t done;  /* submitter waits here for the batch to finish */

	/* current job (published under mtx, consumed lock-free via `next`) */
	void (*fn)(void *, size_t);
	void *arg;
	size_t n;
	atomic_size_t next; /* next index to claim */

	unsigned generation; /* bumped per submit; workers compare to detect work */
	int active;          /* workers still running the current batch */
	int shutdown;
};

static void run_range(struct asp_pool *p)
{
	size_t i;
	while ((i = atomic_fetch_add_explicit(&p->next, 1, memory_order_relaxed)) < p->n)
		p->fn(p->arg, i);
}

static void *worker_main(void *arg)
{
	struct asp_pool *p = arg;
	unsigned last = 0;

	P.mutex_lock(&p->mtx);
	for (;;) {
		while (!p->shutdown && p->generation == last)
			P.cond_wait(&p->ready, &p->mtx);
		if (p->shutdown) {
			P.mutex_unlock(&p->mtx);
			return NULL;
		}
		last = p->generation;
		P.mutex_unlock(&p->mtx);

		run_range(p);

		P.mutex_lock(&p->mtx);
		if (--p->active == 0)
			P.cond_signal(&p->done);
	}
}

int asp_pool_default_workers(void)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	if (n < 1)
		n = 1;
	if (n > 16) /* stat work is kernel-bound; more lanes just contend */
		n = 16;
	return (int)n;
}

struct asp_pool *asp_pool_create(int workers)
{
	if (workers <= 1)
		return NULL; /* caller runs serially */
	if (!load_pthread())
		return NULL; /* no thread runtime: caller runs serially */

	struct asp_pool *p = asp_xmalloc(sizeof *p);
	p->nworkers = workers - 1; /* the submitting thread is one lane */
	p->fn = NULL;
	p->arg = NULL;
	p->n = 0;
	atomic_init(&p->next, 0);
	p->generation = 0;
	p->active = 0;
	p->shutdown = 0;
	P.mutex_init(&p->mtx, NULL);
	P.cond_init(&p->ready, NULL);
	P.cond_init(&p->done, NULL);

	if (p->nworkers <= 0) { /* width 1: just the caller */
		p->threads = NULL;
		p->nworkers = 0;
		return p;
	}

	p->threads = asp_xmalloc((size_t)p->nworkers * sizeof *p->threads);
	int created = 0;
	for (int i = 0; i < p->nworkers; i++) {
		if (P.create(&p->threads[i], NULL, worker_main, p) != 0)
			break;
		created++;
	}
	p->nworkers = created; /* tolerate partial creation */
	return p;
}

void asp_pool_destroy(struct asp_pool *p)
{
	if (!p)
		return;
	P.mutex_lock(&p->mtx);
	p->shutdown = 1;
	P.cond_broadcast(&p->ready);
	P.mutex_unlock(&p->mtx);
	for (int i = 0; i < p->nworkers; i++)
		P.join(p->threads[i], NULL);
	P.mutex_destroy(&p->mtx);
	P.cond_destroy(&p->ready);
	P.cond_destroy(&p->done);
	free(p->threads);
	free(p);
}

void asp_pool_for(struct asp_pool *p, size_t n, void (*fn)(void *, size_t), void *arg)
{
	if (n == 0)
		return;
	if (!p || p->nworkers == 0) { /* serial: run inline */
		for (size_t i = 0; i < n; i++)
			fn(arg, i);
		return;
	}

	P.mutex_lock(&p->mtx);
	p->fn = fn;
	p->arg = arg;
	p->n = n;
	atomic_store_explicit(&p->next, 0, memory_order_relaxed);
	p->active = p->nworkers;
	p->generation++;
	P.cond_broadcast(&p->ready);
	P.mutex_unlock(&p->mtx);

	run_range(p); /* the submitting thread is a lane too */

	P.mutex_lock(&p->mtx);
	while (p->active > 0)
		P.cond_wait(&p->done, &p->mtx);
	P.mutex_unlock(&p->mtx);
}
