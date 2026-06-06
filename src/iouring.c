#include "config.h"
#include "iouring.h"

#if ASP_HAS_LIBURING

#include "sys/xstat.h" /* struct asp_statinfo, asp_type_from_mode */
#include "util.h"

#include <fcntl.h>
#include <liburing.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h> /* makedev */

struct asp_ring {
	struct io_uring ring;
	unsigned depth;
	struct statx *sx; /* `depth` statx targets, reused per wave */
};

struct asp_ring *asp_ring_create(unsigned depth)
{
	if (depth < 8)
		depth = 8;
	struct asp_ring *r = asp_xmalloc(sizeof *r);
	r->depth = depth;
	if (io_uring_queue_init(depth, &r->ring, 0) != 0) {
		free(r); /* io_uring unavailable (old kernel, seccomp, container) */
		return NULL;
	}
	r->sx = asp_xmalloc((size_t)depth * sizeof *r->sx);
	return r;
}

void asp_ring_destroy(struct asp_ring *r)
{
	if (!r)
		return;
	io_uring_queue_exit(&r->ring);
	free(r->sx);
	free(r);
}

static int is_exec(mode_t m)
{
	return (m & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

/* Map statx into an entry exactly as asp_stat_at maps struct stat, so the two
 * backends are byte-for-byte interchangeable. */
static void fill_from_statx(struct entry *e, const struct statx *sx, int want_st)
{
	dev_t dev = makedev(sx->stx_dev_major, sx->stx_dev_minor);
	e->flags |= ENT_STATTED;
	e->ino = sx->stx_ino;
	e->dev = dev;
	if (e->type == ASP_REG && is_exec(sx->stx_mode))
		e->flags |= ENT_EXEC;
	if (want_st) {
		struct asp_statinfo *si = (struct asp_statinfo *)e->st;
		si->mode = sx->stx_mode;
		si->ino = sx->stx_ino;
		si->dev = dev;
		si->size = (off_t)sx->stx_size;
		si->uid = sx->stx_uid;
		si->gid = sx->stx_gid;
		si->nlink = sx->stx_nlink;
		si->atime = sx->stx_atime.tv_sec;
		si->mtime = sx->stx_mtime.tv_sec;
		si->ctime = sx->stx_ctime.tv_sec;
	}
}

int asp_ring_stat_batch(struct asp_ring *r, int dirfd, struct entry **ents,
			size_t n, int want_st)
{
	/* STATX_BASIC_STATS covers every field asp_statinfo carries. AT_NO_AUTOMOUNT
	 * matches the kernel's fstatat default; NOFOLLOW gives lstat semantics. */
	const unsigned mask = STATX_BASIC_STATS;
	const int flags = AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT;

	size_t i = 0;
	while (i < n) {
		unsigned batch = 0;
		while (i + batch < n && batch < r->depth) {
			struct io_uring_sqe *sqe = io_uring_get_sqe(&r->ring);
			if (!sqe)
				break;
			struct entry *e = ents[i + batch];
			io_uring_prep_statx(sqe, dirfd, e->name, flags, mask, &r->sx[batch]);
			io_uring_sqe_set_data64(sqe, batch);
			batch++;
		}
		if (batch == 0)
			return -1; /* couldn't queue anything — fall back */

		int sub = io_uring_submit(&r->ring);
		if (sub < 0)
			return -1;

		for (unsigned done = 0; done < batch; done++) {
			struct io_uring_cqe *cqe;
			if (io_uring_wait_cqe(&r->ring, &cqe) < 0)
				return -1;
			unsigned idx = (unsigned)io_uring_cqe_get_data64(cqe);
			struct entry *e = ents[i + idx];
			if (cqe->res < 0)
				e->flags |= ENT_STAT_FAILED;
			else
				fill_from_statx(e, &r->sx[idx], want_st);
			io_uring_cqe_seen(&r->ring, cqe);
		}
		i += batch;
	}
	return 0;
}

#else /* !ASP_HAS_LIBURING — stubs so the caller links everywhere */

struct asp_ring *asp_ring_create(unsigned depth)
{
	(void)depth;
	return NULL;
}
void asp_ring_destroy(struct asp_ring *r) { (void)r; }
int asp_ring_stat_batch(struct asp_ring *r, int dirfd, struct entry **ents,
			size_t n, int want_st)
{
	(void)r; (void)dirfd; (void)ents; (void)n; (void)want_st;
	return -1;
}

#endif
