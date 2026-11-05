#!/bin/sh
# Print the build version derived from git tags (vX.Y.Z -> X.Y.Z).
# Between releases: X.Y.Z-<commits since tag>-g<hash>[-dirty].
cd "$(dirname "$0")/.." || exit 1
if v=$(git describe --tags --match 'v[0-9]*' --dirty 2>/dev/null); then
    echo "${v#v}"
elif h=$(git describe --always --dirty 2>/dev/null); then
    echo "0.0.0-g$h" # no release tag yet
else
    echo unknown # not a git checkout
fi
