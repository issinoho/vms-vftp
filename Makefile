# Linux build of the core (and later the app and the stub platform), with tests.
# The VMS build is build_vms.com.
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -g -Isrc/core -Isrc/app -Isrc/plat -Isrc/plat/stub
CORE = src/core/vf_util.c src/core/vf_edit.c src/core/vf_screen.c src/core/vf_ansi.c
HDRS = $(wildcard src/core/*.h src/app/*.h src/plat/*.h src/plat/stub/*.h)

all: test

build/test_core: tests/unit/test_core.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_core.c $(CORE)

test: build/test_core
	./build/test_core

clean:
	rm -rf build
.PHONY: all test clean
