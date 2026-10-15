#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the Mocky host simulator (the whole app on this machine) into build/host/mock_host.
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
python3 tools/gen_words.py firmware/words.tsv web/words build/gen/ann_words.h >/dev/null
$CC -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -DOM_HOST -Ifirmware/src -Ibuild/gen -o build/host/mock_host host/mock_host.c
echo "host: build/host/mock_host"
