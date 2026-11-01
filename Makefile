# Linux build of the core, the app and the stubs, with their tests.
# The VMS build is build_vms.com (with libssh2, src/net/vf_sftp.c).
# snprintf cutting text to fit the screen is intended: -Wno-format-truncation.
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -Wno-format-truncation -g -Isrc/core -Isrc/app -Isrc/net -Isrc/plat -Isrc/plat/stub
CORE = src/core/vf_util.c src/core/vf_edit.c src/core/vf_screen.c src/core/vf_ansi.c \
       src/core/vf_pane.c src/core/vf_path.c src/core/vf_text.c src/core/vf_ftpcore.c src/core/vf_sites.c
APP = src/app/vf_app.c src/app/vf_draw.c
STUB = src/plat/stub/vf_plat_stub.c src/plat/stub/vf_remote_stub.c
HDRS = $(wildcard src/core/*.h src/app/*.h src/net/*.h src/plat/*.h src/plat/stub/*.h)

all: test

build/test_core: tests/unit/test_core.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_core.c $(CORE)

build/test_app: tests/unit/test_app.c $(CORE) $(APP) $(STUB) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_app.c $(CORE) $(APP) $(STUB)

build/test_ftp: tests/unit/test_ftp.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_ftp.c $(CORE)

# the FTP client and the dispatcher compile here too (sockets are POSIX); vf_sftp.c
# needs libssh2 and is checked on the nodes only
NET = src/net/vf_ftp.c src/net/vf_remote.c
build/net.ok: $(NET) $(HDRS)
	@mkdir -p build
	for f in $(NET); do $(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200112L -fsyntax-only $$f || exit 1; done
	@touch $@

test: build/net.ok build/test_core build/test_ftp build/test_app
	./build/test_core
	./build/test_ftp
	./build/test_app

snapshots: build/test_app
	VF_UPDATE_SNAPSHOTS=1 ./build/test_app

clean:
	rm -rf build
.PHONY: all test snapshots clean
