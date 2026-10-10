#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The site's screenshots (docs/img): the welcome page, and screens of the sibling firmwares sent to
# Mocky as the web page sends them, in the simulator. Needs Pillow, node, and ../fm1-fomni, ../fm1-eno
# next to this repo.
set -e
cd "$(dirname "$0")/.."
G=build/screens
mkdir -p "$G" docs/img
sh host/build_host.sh >/dev/null
for p in ../fm1-fomni/docs/img/screen-strum.png ../fm1-eno/docs/img/screen-garden.png ../fm1-ctl/docs/img/screen-keys.png; do
    n=$(basename "$(dirname "$(dirname "$(dirname "$p")")")")
    python3 -c "from PIL import Image; import sys; open(sys.argv[2],'wb').write(Image.open(sys.argv[1]).convert('RGBA').resize((240,240)).tobytes())" "$p" "$G/$n.rgba"
    node tools/pic2syx.mjs "$G/$n.rgba" "$G/$n.syx"
done
cat > "$G/screens.mock" <<EOS
shot welcome.png
send $G/fm1-fomni.syx
send $G/fm1-eno.syx
send $G/fm1-ctl.syx
tap OCT-
tap OCT-
shot step.png
wait 2000
tap PLAY
wait 3200
shot slideshow.png
EOS
build/host/mock_host "$G/screens.mock" docs/img
ls docs/img
