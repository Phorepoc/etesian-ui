#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: AGPL-3.0-only
#
# sis.sh -- the pdk's package.yml runs it after the components (package.conf): the Etesian shell as $PHOREPOC_BUILD/sis/etesian-ui-x32.sis.
# Contents: Home (etesianhome.exe + its resources and registration), the launcher (etesianlaunch.exe), the shell description file that
# core's shell selector reads (\resource\phe\shells\etesian.txt), the Etesian skin, the licences. Nothing in an image; nothing that
# replaces a stock file (so no \resource\phe\eclipse\ list: the safe-boot guard has nothing to disable). Removing the package removes the
# shell file; a choice that is not there falls back to S^3.
# Needs the ghcr.io/phorepoc/base host tools (akn_skin_desc_compiler, mifconv), $PHOREPOC_PDK (make_sis, svgtbinencode) and the S60
# palette (PHE_PALETTE, ThirdPartyBitmap.pal from the baseline layer) for the skin.
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
: "${PHE_PALETTE:?set PHE_PALETTE to ThirdPartyBitmap.pal}"
export PHE_SVGTENC=${PHE_SVGTENC:-$PHOREPOC_PDK/tools/svgtenc/svgtbinencode}
UID_SKIN=E7016B31
OUT=$PHOREPOC_BUILD/skin-gen
python3 "$REPO/tools/build_skin.py" etesian-dark "$OUT"
FILES=()
FILES+=(--file "$REPO/shell/etesian.txt=!:/resource/phe/shells/etesian.txt")
FILES+=(--file "$OUT/etesian-dark.skn=!:/private/10207114/import/$UID_SKIN/etesian-dark.skn")
for ext in mif mbm; do
  [ -f "$OUT/etesian-dark.$ext" ] && FILES+=(--file "$OUT/etesian-dark.$ext=!:/resource/skins/$UID_SKIN/etesian-dark.$ext")
done
for f in "$REPO/LICENSES/AGPL-3.0-only.txt" "$REPO/NOTICE"; do
  FILES+=(--file "$f=!:/resource/etesian/licences/$(basename "$f")")
done
mkdir -p "$PHOREPOC_BUILD/sis"
python3 "$PHOREPOC_PDK/tools/make_sis.py" etesianhome etesianlaunch etesianshell --unsigned --name "Etesian" --vendor Phorepoc \
  --version "${PHOREPOC_PACKAGE_VERSION:-0.1.0}" "${FILES[@]}" --out "$PHOREPOC_BUILD/sis/etesian-ui-x32.sis"
