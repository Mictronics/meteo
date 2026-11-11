#!/usr/bin/env bash
# prepare-sdcard.sh - step 1 (on the Linux PC, as root)
#
# Prepares a freshly flashed DietPi SD card (Raspberry Pi, Debian Trixie):
#   - The root partition (p2) is NOT modified.
#   - An ext4 data partition (label METEODATA, later /mnt/data) is created. It starts
#     OS_SIZE_GIB GiB after the start of root, so the OS can grow into the free space
#     in between. DietPi only expands root up to the data partition on first boot.
#   - setup.sh is copied to the boot partition as meteo-setup.sh.
#
# Usage:  sudo ./prepare-sdcard.sh /dev/sdX [path/to/setup.sh]
#
# WARNING: This script changes the partition table of the given device.
#          The card must be freshly flashed DietPi (p1 = vfat, p2 = ext4) and
#          must never have been booted.

set -Eeuo pipefail
trap 'echo "ERROR at line $LINENO: $BASH_COMMAND" >&2' ERR

OS_SIZE_GIB=16
DATA_LABEL="METEODATA"
SECTORS_PER_MIB=2048

die() { echo "ERROR: $*" >&2; exit 1; }
log() { echo "==> $*"; }

[[ $EUID -eq 0 ]] || die "Please run as root (sudo)."

DEV="${1:-}"
[[ -n $DEV ]] || die "Usage: sudo $0 /dev/sdX [path/to/setup.sh]"
SETUP_SCRIPT="${2:-$(dirname "$(readlink -f "$0")")/setup.sh}"
[[ -f $SETUP_SCRIPT ]] || die "setup.sh not found: $SETUP_SCRIPT"
[[ -b $DEV ]] || die "$DEV is not a block device."
[[ $(lsblk -dno TYPE "$DEV") == disk ]] || die "$DEV is not a whole disk (do not pass a partition)."

# Partition names: /dev/sdb -> /dev/sdb1, /dev/mmcblk0 -> /dev/mmcblk0p1
part() {
    if [[ $DEV =~ [0-9]$ ]]; then echo "${DEV}p$1"; else echo "${DEV}$1"; fi
}
P1="$(part 1)"
P2="$(part 2)"
P3="$(part 3)"

[[ -b $P1 && -b $P2 ]] || die "Expected partitions $P1 and $P2 (fresh DietPi image)."
[[ ! -b $P3 ]] || die "$P3 already exists - this is not a freshly flashed card."
[[ $(blkid -s TYPE -o value "$P1") == vfat ]] || die "$P1 is not vfat."
[[ $(blkid -s TYPE -o value "$P2") == ext4 ]] || die "$P2 is not ext4."
[[ $(blockdev --getss "$DEV") -eq 512 ]] || die "Only 512-byte sectors are supported."
if lsblk -nro MOUNTPOINTS "$DEV" | grep -q .; then
    die "Partitions of $DEV are mounted. Unmount them first (umount)."
fi

# Everything in 512-byte sectors
P2_NAME="$(basename "$P2")"
P2_START="$(cat "/sys/class/block/$P2_NAME/start")"
P2_SIZE="$(cat "/sys/class/block/$P2_NAME/size")"
DISK_SECTORS="$(blockdev --getsz "$DEV")"

# Data partition starts OS_SIZE_GIB GiB after the start of root (aligned to 1 MiB)
DATA_START=$((P2_START + OS_SIZE_GIB * 1024 * SECTORS_PER_MIB))
DATA_START=$(((DATA_START + SECTORS_PER_MIB - 1) / SECTORS_PER_MIB * SECTORS_PER_MIB))
MIN_DATA=$((1024 * SECTORS_PER_MIB)) # at least 1 GiB for the data partition

((P2_START + P2_SIZE <= DATA_START)) || die "Root partition already extends to ${OS_SIZE_GIB} GiB or more. Flash a fresh image."
((DATA_START + MIN_DATA < DISK_SECTORS)) || die "Card is too small for ${OS_SIZE_GIB} GiB OS + data partition."

DATA_MIB=$(((DISK_SECTORS - DATA_START) / SECTORS_PER_MIB))
OS_FREE_MIB=$(((DATA_START - P2_START - P2_SIZE) / SECTORS_PER_MIB))

echo
lsblk -o NAME,SIZE,FSTYPE,LABEL,MODEL "$DEV"
echo
echo "Planned for $DEV:"
echo "  ${P2}: unchanged; DietPi may grow it by about ${OS_FREE_MIB} MiB on first boot"
echo "  ${P3}: ext4 '${DATA_LABEL}', start sector ${DATA_START}, about ${DATA_MIB} MiB (-> /mnt/data)"
echo
read -r -p "To continue, type the device path ($DEV): " ANSWER
[[ $ANSWER == "$DEV" ]] || die "Aborted."

BACKUP="ptable-backup-$(basename "$DEV")-$(date +%Y%m%d-%H%M%S).sfdisk"
sfdisk -d "$DEV" >"$BACKUP"
log "Partition table saved: $BACKUP"

log "Creating data partition"
echo "${DATA_START},,L" | sfdisk --quiet --append --no-reread --no-tell-kernel "$DEV"
partprobe "$DEV"
udevadm settle
[[ -b $P3 ]] || die "Partition $P3 was not created."
mkfs.ext4 -q -F -L "$DATA_LABEL" -m 0 "$P3"

MNT="$(mktemp -d)"
cleanup() {
    ! mountpoint -q "$MNT" || umount "$MNT"
    rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

log "Copying setup.sh to the boot partition"
mount "$P1" "$MNT"
cp "$SETUP_SCRIPT" "$MNT/meteo-setup.sh"
sync
umount "$MNT"

cat <<EOF

Done. Next steps:
  1. Put the card into the Raspberry Pi 3, boot, and complete the DietPi first-run
     setup (network, passwords, software selection: minimal).
  2. Check:  lsblk   (p2 about ${OS_SIZE_GIB} GiB, p3 about ${DATA_MIB} MiB)
  3. Run as root:  bash /boot/firmware/meteo-setup.sh
EOF
