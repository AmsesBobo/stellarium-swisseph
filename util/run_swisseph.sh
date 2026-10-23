#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified 2026-10-23: persistent, isolated Swiss Ephemeris user profile.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
profile=${SWISSEPH_USER_DIR:-"$HOME/.stellarium-swisseph"}
mkdir -p "$profile"
# Keep downloads and calculations in the same profile even if the shell has
# environment overrides intended for another Stellarium build.
unset SWEPH_PATH STEL_USERDIR
cd "$root"
exec "$root/build/src/stellarium" --user-dir "$profile" "$@"
