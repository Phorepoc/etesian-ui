# Copyright The Phorepoc Authors
# SPDX-License-Identifier: AGPL-3.0-only

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
    # "Etesian shell": the visible app that chooses the shell for the next start (CenRep key 10), no Control Panel plug-in needed
    "etesianshell": dict(mmp=os.path.join(os.path.dirname(_SRC), "etesianshell", "etesianshell.mmp"), sis=True),
    # the dialler: pilot of the shell-tagged apps
    "etesianphone": dict(mmp=os.path.join(os.path.dirname(_SRC), "etesianphone", "etesianphone.mmp"), sis=True),
    # a test utility (set the active theme by name from the Terminal); not in the package
    "etesiantheme": dict(mmp=os.path.join(os.path.dirname(_SRC), "etesiantheme", "etesiantheme.mmp"), sis=True),
}
