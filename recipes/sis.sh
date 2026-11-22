#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: AGPL-3.0-only
#
# sis.sh -- the pdk's package.yml runs it after the components (package.conf): the Etesian shell as $PHOREPOC_BUILD/sis/etesian-ui-<arch>.sis (arm or x32).
# Contents: Home (etesianhome.exe + its resources and registration), the launcher (etesianlaunch.exe), the dialler and the shell chooser,
# the shell description file that core's shell selector reads (resource/phe/shells/etesian.txt), the licences. NOT in it: the Etesian theme and its
# typeface (Fira Sans): they are in the Phorepoc images (the etesian-theme repository, built into the ROM), or come from the "Etesian theme"
# package. Choosing the shell activates the theme by name and switching back restores the previous one. Nothing in an image; nothing that replaces a
# stock file (so no eclipse list: the safe-boot guard has nothing to disable). Removing the package removes the shell file; a choice that is not
# there falls back to S^3.
set -eu
ARCH=${PHOREPOC_ARCH:-x32}
case "$ARCH" in arm) AFL=--arm;; x32) AFL=;; *) echo "ARCH must be arm or x32 (got $ARCH)"; exit 1;; esac
REPO=$(cd "$(dirname "$0")/.." && pwd)
FILES=()
FILES+=(--file "$REPO/shell/etesian.txt=!:/resource/phe/shells/etesian.txt")
# the shell app icon: a vector .mif from its SVG-Tiny source (the pdk tool: the launcher renders it sharp at any grid size)
mkdir -p "$PHOREPOC_BUILD/appicons"
python3 "$PHOREPOC_PDK/tools/app_icon_mif.py" "$REPO/os/src/etesianshell/data/etesianshell_icon_tiny.svg" "$PHOREPOC_BUILD/appicons/etesianshell_icon.mif"
FILES+=(--file "$PHOREPOC_BUILD/appicons/etesianshell_icon.mif=!:/resource/apps/etesianshell_icon.mif")
for t in apps hides assoc; do
  FILES+=(--file "$REPO/shell/etesian.$t=!:/resource/phe/shells/etesian.$t")
done
for f in "$REPO/LICENSES/AGPL-3.0-only.txt" "$REPO/NOTICE"; do
  FILES+=(--file "$f=!:/resource/etesian/ui/licences/$(basename "$f")")
done
mkdir -p "$PHOREPOC_BUILD/sis"
# shellcheck disable=SC2086
python3 "$PHOREPOC_PDK/tools/make_sis.py" etesianhome etesianlaunch etesianshell etesianphone $AFL --unsigned --name "Etesian" --vendor Phorepoc \
  --version "${PHOREPOC_PACKAGE_VERSION:-0.1.0}" "${FILES[@]}" --out "$PHOREPOC_BUILD/sis/etesian-ui-$ARCH.sis"
