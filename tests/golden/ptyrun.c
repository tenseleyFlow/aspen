/*
 * ptyrun — run a command with its stdout attached to a pseudo-terminal slave,
 * copying everything the command writes to ptyrun's own stdout.
 *
 * The golden/color harnesses otherwise redirect stdout to a file, so the only
 * isatty()-gated path in aspen (auto-color on a terminal, color.c) is never
 * exercised. Running both aspen and tree through this helper makes isatty(1)
 * true for the child, so the auto-color decision is tested for real.
 *
 * libc-only: posix_openpt/grantpt/unlockpt/ptsname are POSIX (Linux, *BSD,
 * macOS). Exits 2 if a pty cannot be allocated so the caller can skip cleanly.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: ptyrun cmd [args...]\n");
		return 2;
	}

	int master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0)
		return 2; /* no pty available — caller skips */
	const char *slave_name = ptsname(master);
	if (slave_name == NULL)
		return 2;

	pid_t pid = fork();
	if (pid < 0)
		return 2;

	if (pid == 0) {
		/* Child: stdout -> pty slave (isatty(1) becomes true). stderr is left
		 * pointing at the inherited fd so diagnostics still reach the caller. */
		int slave = open(slave_name, O_RDWR);
		if (slave < 0)
			_exit(127);
		if (dup2(slave, STDOUT_FILENO) < 0)
			_exit(127);
		if (slave > STDERR_FILENO)
			close(slave);
		close(master);
		execvp(argv[1], &argv[1]);
		_exit(127);
	}

	/* Parent: drain the master until the slave closes. On Linux a closed slave
	 * surfaces as EIO; treat that as EOF. */
	char buf[8192];
	for (;;) {
		ssize_t n = read(master, buf, sizeof buf);
		if (n > 0) {
			ssize_t off = 0;
			while (off < n) {
				ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));
				if (w < 0) {
					if (errno == EINTR)
						continue;
					break;
				}
				off += w;
			}
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		break; /* 0 (EOF) or EIO after slave close */
	}

	int status;
	if (waitpid(pid, &status, 0) < 0)
		return 2;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}
