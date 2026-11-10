#!/bin/sh
set -eu

directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=${CC:-cc}
output=${TMPDIR:-/tmp}/vkb_test

"$compiler" \
	-std=c99 -Wall -Wextra -Werror -pedantic \
	$(pkg-config --cflags xkbcommon) \
	-I"$directory/.." \
	"$directory/test_vkb.c" \
	$(pkg-config --libs xkbcommon) \
	-o "$output"

"$output"
