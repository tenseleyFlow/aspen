#include "pool.h"
#include "util.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

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

	pthread_mutex_lock(&p->mtx);
	for (;;) {
		while (!p->shutdown && p->generation == last)
			pthread_cond_wait(&p->ready, &p->mtx);
		if (p->shutdown) {
			pthread_mutex_unlock(&p->mtx);
			return NULL;
		}
		last = p->generation;
		pthread_mutex_unlock(&p->mtx);

		run_range(p);

		pthread_mutex_lock(&p->mtx);
		if (--p->active == 0)
			pthread_cond_signal(&p->done);
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

	struct asp_pool *p = asp_xmalloc(sizeof *p);
	p->nworkers = workers - 1; /* the submitting thread is one lane */
	p->fn = NULL;
	p->arg = NULL;
	p->n = 0;
	atomic_init(&p->next, 0);
	p->generation = 0;
	p->active = 0;
	p->shutdown = 0;
	pthread_mutex_init(&p->mtx, NULL);
	pthread_cond_init(&p->ready, NULL);
	pthread_cond_init(&p->done, NULL);

	if (p->nworkers <= 0) { /* width 1: just the caller */
		p->threads = NULL;
		p->nworkers = 0;
		return p;
	}

	p->threads = asp_xmalloc((size_t)p->nworkers * sizeof *p->threads);
	int created = 0;
	for (int i = 0; i < p->nworkers; i++) {
		if (pthread_create(&p->threads[i], NULL, worker_main, p) != 0)
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
	pthread_mutex_lock(&p->mtx);
	p->shutdown = 1;
	pthread_cond_broadcast(&p->ready);
	pthread_mutex_unlock(&p->mtx);
	for (int i = 0; i < p->nworkers; i++)
		pthread_join(p->threads[i], NULL);
	pthread_mutex_destroy(&p->mtx);
	pthread_cond_destroy(&p->ready);
	pthread_cond_destroy(&p->done);
	free(p->threads);
	free(p);
}

int asp_pool_width(const struct asp_pool *p)
{
	return p ? p->nworkers + 1 : 1;
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

	pthread_mutex_lock(&p->mtx);
	p->fn = fn;
	p->arg = arg;
	p->n = n;
	atomic_store_explicit(&p->next, 0, memory_order_relaxed);
	p->active = p->nworkers;
	p->generation++;
	pthread_cond_broadcast(&p->ready);
	pthread_mutex_unlock(&p->mtx);

	run_range(p); /* the submitting thread is a lane too */

	pthread_mutex_lock(&p->mtx);
	while (p->active > 0)
		pthread_cond_wait(&p->done, &p->mtx);
	pthread_mutex_unlock(&p->mtx);
}
