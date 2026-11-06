#!/bin/sh
# Update the station: git pull, build the .deb as the current user, install it with sudo.
# With overlayroot active the package is installed into the real root, so it survives a
# reboot. Keep the repo on a persistent partition (e.g. /mnt/data/git/meteo).
set -eu

cd "$(dirname "$0")/.."
REPO=$(pwd)

[ "$(id -u)" -ne 0 ] || { echo "run as a normal user, sudo is called where needed" >&2; exit 1; }

git pull --ff-only --tags
dpkg-buildpackage -b -uc

VER=$(dpkg-parsechangelog -SVersion)
DEB=$(ls -t "$REPO"/../meteo_"${VER}"_*.deb | head -n1)
echo "Built $DEB"

if [ "$(findmnt -no FSTYPE /)" != overlay ]; then
    sudo dpkg -i "$DEB"
else
    # The chroot sees the lower root, where the data partition's mount point is empty.
    MNT=$(findmnt -no TARGET -T "$DEB")
    [ "$MNT" != / ] || { echo "$DEB is on the volatile overlay, keep the repo on /mnt/data" >&2; exit 1; }
    sudo mount --bind "$MNT" "/media/root-ro$MNT"
    trap 'sudo umount "/media/root-ro$MNT"' EXIT
    sudo overlayroot-chroot dpkg -i "$DEB"
fi

# Changing the lower layer under a mounted overlay is undefined, so reboot to pick it up.
echo "Installed. Reboot to activate."
