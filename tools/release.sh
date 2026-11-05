#!/bin/sh
# Create release X.Y.Z[-prerelease]: bump package.json, add a debian/changelog
# entry from the commit subjects since the last tag, commit and tag vX.Y.Z.
# Nothing is pushed.
set -eu

V=${1:-}
cd "$(dirname "$0")/.."

if ! echo "$V" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z][0-9A-Za-z.-]*)?$'; then
    echo "usage: $0 X.Y.Z[-prerelease]   (semantic version, e.g. 1.2.0)" >&2
    exit 1
fi
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "error: working tree has uncommitted changes" >&2
    exit 1
fi
if git rev-parse -q --verify "refs/tags/v$V" >/dev/null; then
    echo "error: tag v$V already exists" >&2
    exit 1
fi

LAST=$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true)
if [ -n "$LAST" ] && [ "$(printf '%s\n%s\n' "${LAST#v}" "$V" | sort -V | tail -n1)" != "$V" ]; then
    echo "error: $V is not newer than $LAST" >&2
    exit 1
fi

# Debian sorts a pre-release below the release only with '~'
DEBV=$(echo "$V" | sed 's/-/~/')
RANGE=$LAST..HEAD
NAME=$(git config user.name)
MAIL=$(git config user.email)

{
    echo "meteo ($DEBV) unstable; urgency=medium"
    echo
    if [ -n "$LAST" ]; then
        git log --no-merges --pretty='  * %s' "$RANGE"
    else
        echo "  * First semantic version release."
    fi
    echo
    echo " -- $NAME <$MAIL>  $(date -R)"
    echo
    cat debian/changelog
} > debian/changelog.new
mv debian/changelog.new debian/changelog

sed -i "s/^  \"version\": \".*\",/  \"version\": \"$V\",/" package.json

git add debian/changelog package.json
git commit -q -m "Release v$V"
git tag -a "v$V" -m "Release $V"
echo "Created commit and tag v$V. Publish with: git push origin HEAD v$V"
