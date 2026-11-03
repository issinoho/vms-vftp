#!/usr/bin/env bash
# banner.sh - rebuild docs/images/banner.svg: VFTP's two panes from the Linux
# stubs with made-up files (tools/banner_frame.c), drawn by tools/banner.py.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top"
mkdir -p build docs/images
cc -std=c99 -pedantic -Wall -Wextra -Werror -Wno-format-truncation \
   -Isrc/core -Isrc/app -Isrc/net -Isrc/plat -Isrc/plat/stub \
   -o build/banner_frame tools/banner_frame.c src/core/vf_util.c src/core/vf_edit.c \
   src/core/vf_screen.c src/core/vf_ansi.c src/core/vf_pane.c src/core/vf_path.c \
   src/core/vf_text.c src/core/vf_ftpcore.c src/core/vf_sites.c \
   src/app/vf_app.c src/app/vf_draw.c src/plat/stub/vf_plat_stub.c src/plat/stub/vf_remote_stub.c
./build/banner_frame > build/banner.ans
python3 tools/banner.py build/banner.ans docs/images/banner.svg
