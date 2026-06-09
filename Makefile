# aspen — a blazingly-fast, byte-for-byte tree(1) clone.
# Single Makefile, libc only (+ optional liburing). C11. See .docs/ for the design.

# Require GNU make. This Makefile uses GNU-only features ($(wildcard), pattern
# rules, target-specific vars, -include). BSD make would otherwise expand
# $(wildcard ...) to nothing and *silently link an objectless, empty binary*.
# .FEATURES is set by every GNU make (>=3.81) and unknown to BSD make — which
# fatally rejects the `ifeq` line below rather than building garbage. Friendly
# message on GNU; loud parse-error stop on BSD. Use `gmake` on FreeBSD.
ifeq ($(.FEATURES),)
$(error aspen's Makefile requires GNU make. Run 'gmake' instead (FreeBSD: pkg install gmake; on macOS /usr/bin/make is already GNU make).)
endif

-include config.mk

CC      ?= cc
PREFIX  ?= /usr/local
BINDIR   = $(DESTDIR)$(PREFIX)/bin
MANDIR   = $(DESTDIR)$(PREFIX)/share/man/man1

# The version string lives in src/version.h (single source of truth, used by
# --version and packaging); do not duplicate it here.

WARN     = -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wshadow -Wconversion -Wwrite-strings
STD      = -std=c11
# OPT is the optimization level; release/debug override it cleanly (no stale -O2
# left ahead of -O3/-O0). CFLAGS stays free for user-appended flags.
OPT     ?= -O2
# _FILE_OFFSET_BITS=64 matches tree's ABI so off_t/ino_t column widths agree on 32-bit too.
ALL_CFLAGS = $(STD) $(WARN) $(OPT) $(CFLAGS) $(CONF_CFLAGS) -Isrc -I. -D_FILE_OFFSET_BITS=64
LDLIBS  += $(LDLIBS_OPT)

# Explicit source list — deterministic and faster to parse than $(wildcard),
# and it makes a stray/abandoned .c in src/ a deliberate add, not a silent one.
# Keep sorted; mirror tests/run.sh's discovery (it still globs for unit links).
SRC = \
	src/arena.c \
	src/charset.c \
	src/color.c \
	src/dstr.c \
	src/entry.c \
	src/filter.c \
	src/fromfile.c \
	src/glob.c \
	src/hashtab.c \
	src/idcache.c \
	src/info.c \
	src/iouring.c \
	src/main.c \
	src/options.c \
	src/pool.c \
	src/render.c \
	src/sort.c \
	src/traverse.c \
	src/util.c \
	src/verscmp.c \
	src/sys/dir.c \
	src/sys/xstat.c \
	src/render/escape.c \
	src/render/fileinfo.c \
	src/render/html.c \
	src/render/json.c \
	src/render/name.c \
	src/render/outbuf.c \
	src/render/unix.c \
	src/render/xml.c
OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

.PHONY: all clean distclean install uninstall test bench fmt analyze release debug pgo coverage

all: config.h aspen asp

config.h config.mk:
	@./configure

aspen: $(OBJ)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

# asp is the same binary under a second name (behavior is identical to aspen).
asp: aspen
	@cp -f aspen asp

%.o: %.c
	$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

release: OPT = -O3 -flto -DNDEBUG
release: clean all
	@strip aspen asp 2>/dev/null || true   # smaller binary, marginally faster load (SR02-1.5)

debug: OPT = -O0 -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean all

# Opt-in profile-guided build (clang/llvm). Marginal on this workload, so it is
# NOT the default release — packaged builds stay plain for reproducibility.
pgo:
	@sh bench/pgo.sh

test: all
	@sh tests/run.sh

bench: release
	@sh bench/run.sh

# Line coverage of the aspen binary over the whole test suite. clang -> llvm-cov
# source-based report; gcc -> gcovr/gcov. Leaves a normal build afterward.
coverage:
	@sh tests/coverage.sh

fmt:
	@command -v clang-format >/dev/null && clang-format -i $(SRC) src/*.h src/sys/*.h src/render/*.h || echo "clang-format not found"

analyze:
	@$(CC) $(ALL_CFLAGS) --analyze $(SRC) 2>&1 || true

install: all
	@mkdir -p $(BINDIR) $(MANDIR)
	install -m 0755 aspen $(BINDIR)/aspen
	ln -sf aspen $(BINDIR)/asp
	install -m 0644 doc/aspen.1 $(MANDIR)/aspen.1

uninstall:
	rm -f $(BINDIR)/aspen $(BINDIR)/asp $(MANDIR)/aspen.1

clean:
	rm -f $(OBJ) $(DEP) aspen asp
	rm -f src/*.gcno src/*.gcda src/*.gcov src/sys/*.gcno src/sys/*.gcda src/render/*.gcno src/render/*.gcda *.gcov

distclean: clean
	rm -f config.h config.mk

-include $(DEP)
