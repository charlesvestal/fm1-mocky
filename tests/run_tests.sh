#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Mocky host tests: the platform pieces kept from Felucca / FoMni, the web page's picture coder and
# protocol (web/mocky.js), and the whole firmware app in the simulator (tests/scenarios/*.mock:
# pictures sent as the page sends them, the gallery, the controls, persistence), with screenshots in
# build/scenarios/.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FAIL=0
OUT=build/host
mkdir -p "$OUT" build/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run storage sh -c "$CC -O2 -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
run flash-ok sh -c "$CC -std=c99 -Wall -Wextra -Werror -o $OUT/flash_ok_test tests/host/flash_ok_test.c && $OUT/flash_ok_test"   # firmware/hal/fm1_flash_ok.h: the flash parts the storage may use
# the M-UPGRADE handshake (firmware/src/ota.c): a release's identity cut to NAME_ and three digits for the plain
# query (M-UPGRADE 3.11), the whole one for the query with body 01; a dev build's; the update loader's, whole
for m in long short loader; do
    case $m in long) f="" ;; short) f="-DTEST_ID='\"FM-1_100\"'" ;; loader) f="-DFELUCCA_LOADER=1" ;; esac
    run "ota-hs-$m" sh -c "$CC -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function $f -o $OUT/ota_hs_$m tests/ota_hs_test.c && $OUT/ota_hs_$m"
done
# the web installer's handshake (web/fm1ota.js): a firmware that answers M-UPGRADE 3.11's query short and its whole
# identity to the one with 01, older builds, M-VAVE's firmware, the loader
if command -v node >/dev/null 2>&1; then
    run installer-hs node tests/host/installer_hs_test.mjs
fi
# the update path, against the firmware package (Felucca's tests; needs ./build.sh)
if [ -f build/mocky.fwsc ]; then
    run ota-entry sh -c "$CC -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test build/mocky.fwsc"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip update-loader (needs AC79_SDK, as the build)"
    else
    run update-loader sh -c "head -c 60000 build/mocky.bin > $OUT/old_app.bin && \
        python3 tools/fm1pkg_make.py $OUT/old_app.bin build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc build/mocky.fwsc"
    fi
    run installer python3 tests/install_test.py
else
    echo "  skip update-path tests (no build/mocky.fwsc: run ./build.sh)"
fi
run web node tests/host/mock_test.mjs $OUT/pics
run host-build sh host/build_host.sh
for s in tests/scenarios/*.mock; do
    n=$(basename "$s" .mock)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/mock_host "$s" "build/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
