# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0

"""components_etesian -- the Etesian shell (merged into core's registry when this repository is a layer: PHOREPOC_ADAPTATIONS=<...>:<path to
etesian-ui>, see the pdk's tools/emitter/paths.py). Paths are relative to the merged OS tree.

  etesianlaunch  the shell's launcher exe (named in shell/etesian.txt): starts Home, rendezvouses, lives as long as Home
  etesianhome    the Etesian Home application (Applications view, rung E1), hidden from the app lists

Build:  phe component resources etesianhome && phe component build etesianlaunch etesianhome      Package: recipes/sis.sh
Both are `sis`: laid out RAM-loadable, installed to the package's drive by the SIS (not in any image).
"""
import os

from paths import OS_ROOT

_SRC = os.path.join(OS_ROOT, "src", "etesianhome")

COMPONENTS = {
    "etesianlaunch": dict(mmp=os.path.join(_SRC, "etesianlaunch.mmp"), sis=True),
    "etesianhome": dict(mmp=os.path.join(_SRC, "etesianhome.mmp"), sis=True),
}
