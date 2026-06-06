# aspen — a blazingly-fast, byte-for-byte tree(1) clone.
# Single Makefile, libc only (+ optional liburing). C11. See .docs/ for the design.

-include config.mk

CC      ?= cc
PREFIX  ?= /usr/local
BINDIR   = $(DESTDIR)$(PREFIX)/bin
MANDIR   = $(DESTDIR)$(PREFIX)/share/man/man1

VERSION  = 0.0.0-dev

WARN     = -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wshadow -Wconversion -Wwrite-strings
STD      = -std=c11
CFLAGS  ?= -O2
# _FILE_OFFSET_BITS=64 matches tree's ABI so off_t/ino_t column widths agree on 32-bit too.
ALL_CFLAGS = $(STD) $(WARN) $(CFLAGS) -Isrc -I. -D_FILE_OFFSET_BITS=64
LDLIBS  += $(LDLIBS_OPT)

SRC = $(wildcard src/*.c src/sys/*.c src/render/*.c)
OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

.PHONY: all clean distclean install uninstall test bench fmt analyze release debug

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

release: ALL_CFLAGS += -O3 -flto -DNDEBUG
release: clean all

debug: ALL_CFLAGS += -O0 -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean all

test: all
	@sh tests/run.sh

bench: release
	@sh bench/run.sh

fmt:
	@command -v clang-format >/dev/null && clang-format -i $(SRC) src/*.h || echo "clang-format not found"

analyze:
	@$(CC) $(ALL_CFLAGS) --analyze $(SRC) 2>&1 || true

install: all
	@mkdir -p $(BINDIR) $(MANDIR)
	install -m 0755 aspen $(BINDIR)/aspen
	ln -sf aspen $(BINDIR)/asp
	@[ -f doc/aspen.1 ] && install -m 0644 doc/aspen.1 $(MANDIR)/aspen.1 || true

uninstall:
	rm -f $(BINDIR)/aspen $(BINDIR)/asp $(MANDIR)/aspen.1

clean:
	rm -f $(OBJ) $(DEP) aspen asp

distclean: clean
	rm -f config.h config.mk

-include $(DEP)
