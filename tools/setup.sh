#!/usr/bin/env bash
# setup.sh - step 2 (on the Raspberry Pi 3, as root)
#
# Sets up the weather station server on DietPi (Debian Trixie):
#   gpsd + chrony (GPS NMEA + PPS on GPIO18), lighttpd, busybox, git,
#   avahi (hostname -> weatherstation.local) and a Wi-Fi access point
#   (hostapd + dnsmasq) through which the web client is reached.
#   Builds the Mictronics/meteo package on the data partition (/mnt/data),
#   installs it and finally enables OverlayRoot (root FS read-only via
#   tmpfs overlay, data partition stays writable).
#
# Prerequisite: the card was prepared with prepare-sdcard.sh
#               (data partition with label METEODATA exists).
#
# Usage:  sudo bash setup.sh [--skip-overlay] [--no-reboot] [--no-ap]
#   --skip-overlay  set everything up, but do NOT enable OverlayRoot
#   --no-reboot     enable OverlayRoot, but do not reboot automatically
#   --no-ap         do not set up the Wi-Fi access point (hostname + avahi only)
#
# Configurable via environment variables, e.g.:
#   sudo AP_SSID=Meteo AP_PASSPHRASE='secret-password' bash setup.sh
#   MDNS_HOST (weatherstation)  AP_SSID (Weatherstation)  AP_PASSPHRASE (random)
#   AP_CHANNEL (6)  AP_COUNTRY (DE)  AP_NET (192.168.42)  WLAN_IF (wlan0)  BUILD_USER (dietpi)
#
# The script is idempotent - after a failure it can be started again
# as long as OverlayRoot is not yet active.
#
# IMPORTANT: DietPi tools (dietpi-software, dietpi-config, dietpi-update ...) only
#   work correctly with OverlayRoot DISABLED. Disable it before any installation or
#   configuration via DietPi tools and enable it again afterwards (exact commands
#   are printed at the end of this script).

set -Eeuo pipefail

# ----------------------------------------------------------------- Settings
DATA_LABEL="METEODATA"
DATA_MOUNT="/mnt/data"
GPS_DEV="/dev/ttyACM0"          # USB GPS (for stable names: /dev/serial/by-id/usb-...)
PPS_GPIO=18                     # PPS input (BCM number)
METEO_REPO="https://github.com/Mictronics/meteo.git"
METEO_DIR="${DATA_MOUNT}/git/meteo"
LOGFILE="/var/log/meteo-setup.log"

MDNS_HOST="${MDNS_HOST:-weatherstation}"   # hostname -> <name>.local via avahi
WLAN_IF="${WLAN_IF:-wlan0}"
AP_SSID="${AP_SSID:-Weatherstation}"
AP_PASSPHRASE="${AP_PASSPHRASE:-}"         # empty = generate randomly (8-63 characters)
AP_CHANNEL="${AP_CHANNEL:-6}"              # 2.4 GHz: 1-13
AP_COUNTRY="${AP_COUNTRY:-DE}"             # Wi-Fi regulatory domain
AP_NET="${AP_NET:-192.168.42}"             # /24 network of the access point
AP_IP="${AP_NET}.1"

SKIP_OVERLAY=0
NO_REBOOT=0
NO_AP=0
for arg in "$@"; do
    case "$arg" in
        --skip-overlay) SKIP_OVERLAY=1 ;;
        --no-reboot) NO_REBOOT=1 ;;
        --no-ap) NO_AP=1 ;;
        -h | --help) sed -n '2,/^[^#]/{/^#/p}' "$0"; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

# ----------------------------------------------------------------- Helpers
log() { printf '\n==> %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# Append the line only if it is not already in the file (exact match)
ensure_line() {
    local file="$1" line="$2"
    grep -qxF -- "$line" "$file" 2>/dev/null || printf '%s\n' "$line" >>"$file"
}

# Back up the original once
backup_once() {
    [[ -f $1 && ! -f $1.orig ]] && cp -a "$1" "$1.orig"
    return 0
}

# Install packages without apt starting their services (policy-rc.d, exit 101).
# Needed for hostapd/dnsmasq: they must only run after the reboot, starting them
# during setup would e.g. cut an existing Wi-Fi/SSH connection.
POLICY_RC="/usr/sbin/policy-rc.d"
install_nostart() {
    [[ ! -e $POLICY_RC ]] || die "${POLICY_RC} already exists."
    trap 'rm -f "$POLICY_RC"' EXIT
    printf '#!/bin/sh\nexit 101\n' >"$POLICY_RC"
    chmod +x "$POLICY_RC"
    apt-get install -y "$@"
    rm -f "$POLICY_RC"
    trap - EXIT
}

# Save the setup log to the data partition (contains no Wi-Fi password)
save_log() {
    cp -f "$LOGFILE" "${DATA_MOUNT}/meteo-setup.log" && chmod 600 "${DATA_MOUNT}/meteo-setup.log" || true
}

[[ $EUID -eq 0 ]] || die "Please run as root (sudo bash $0)."

: >>"$LOGFILE"
chmod 600 "$LOGFILE"
exec > >(tee -a "$LOGFILE") 2>&1
trap 'echo "ERROR at line $LINENO: $BASH_COMMAND" >&2' ERR
echo "meteo-setup started: $(date -Is)"

# ----------------------------------------------------------------- Preflight checks
log "Preflight checks"

# shellcheck disable=SC1091
. /etc/os-release
[[ ${VERSION_CODENAME:-} == trixie ]] || warn "Expected Debian Trixie, found: ${VERSION_CODENAME:-unknown}"
[[ -f /boot/dietpi/.version || -d /boot/dietpi ]] || warn "This does not look like DietPi."
grep -q 'Raspberry Pi 3' /proc/device-tree/model 2>/dev/null || warn "Hardware is not a Raspberry Pi 3: $(tr -d '\0' </proc/device-tree/model 2>/dev/null || echo unknown)"

if findmnt -n -o FSTYPE / | grep -q overlay; then
    die "OverlayRoot is already active. Use 'overlayroot-chroot' for changes."
fi

BOOT_DIR="/boot/firmware"
[[ -d $BOOT_DIR ]] || BOOT_DIR="/boot"
CONFIG_TXT="${BOOT_DIR}/config.txt"
[[ -f $CONFIG_TXT ]] || die "${CONFIG_TXT} not found."

getent hosts github.com >/dev/null || die "No name resolution/network (github.com)."

DATA_DEV="$(blkid -L "$DATA_LABEL" || true)"
[[ -n $DATA_DEV ]] || die "No partition with label '${DATA_LABEL}' found. Was prepare-sdcard.sh run?"
ROOT_SRC="$(findmnt -n -o SOURCE /)"
[[ $DATA_DEV != "$ROOT_SRC" ]] || die "Data partition is the same as the root partition."

ROOT_GIB=$(($(blockdev --getsize64 "$ROOT_SRC") / 1073741824))
((ROOT_GIB <= 20)) || warn "Root partition is ${ROOT_GIB} GiB - did the DietPi autoresize run after all?"

# User for git/build. The project's tools/update.sh refuses root and calls sudo
# itself, so this must be a normal user with sudo rights.
BUILD_USER="${BUILD_USER:-dietpi}"
id "$BUILD_USER" >/dev/null 2>&1 || die "User '${BUILD_USER}' does not exist (use another user: BUILD_USER=name bash $0)."
[[ $BUILD_USER != root ]] || die "BUILD_USER must not be root (tools/update.sh refuses root)."
BUILD_HOME="$(getent passwd "$BUILD_USER" | cut -d: -f6)"
as_build() { runuser -u "$BUILD_USER" -- env HOME="$BUILD_HOME" "$@"; }

# Hostname / access point parameters
[[ $MDNS_HOST =~ ^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$ ]] || die "Invalid hostname '${MDNS_HOST}' (only a-z, 0-9 and '-')."
if ((!NO_AP)); then
    [[ $AP_SSID =~ ^.{1,32}$ ]] || die "AP_SSID must be 1-32 characters long."
    [[ $AP_COUNTRY =~ ^[A-Z]{2}$ ]] || die "AP_COUNTRY must be a country code such as DE."
    [[ $AP_CHANNEL =~ ^[0-9]+$ ]] && ((10#$AP_CHANNEL >= 1 && 10#$AP_CHANNEL <= 13)) || die "AP_CHANNEL must be 1-13 (2.4 GHz)."
    [[ $AP_NET =~ ^([0-9]{1,3}\.){2}[0-9]{1,3}$ ]] || die "AP_NET must be the first three octets of a /24 network, e.g. 192.168.42."
    [[ -z $AP_PASSPHRASE || $AP_PASSPHRASE =~ ^.{8,63}$ ]] || die "AP_PASSPHRASE must be 8-63 characters long."
    [[ -d /sys/class/net/${WLAN_IF} ]] || die "Wi-Fi interface ${WLAN_IF} not found (other name: WLAN_IF=..., without AP: --no-ap)."
    [[ -f /etc/network/interfaces ]] || die "/etc/network/interfaces is missing - the AP is only set up for ifupdown (DietPi default). Skip it with --no-ap."
    if systemctl is-active --quiet NetworkManager; then
        die "NetworkManager is active - the AP is only set up for ifupdown (DietPi default). Skip it with --no-ap."
    fi
    if ip -4 route show default | grep -q "dev ${WLAN_IF}"; then
        warn "The default route goes through ${WLAN_IF}. After the reboot ${WLAN_IF} is the access point and this (SSH) connection will drop - continue over Ethernet."
    fi
    if grep -rqE "iface[[:space:]]+${WLAN_IF}[[:space:]]" /etc/network/interfaces.d 2>/dev/null; then
        warn "/etc/network/interfaces.d contains a configuration for ${WLAN_IF} - remove it, it conflicts with the AP."
    fi
fi

# ----------------------------------------------------------------- Data partition
log "Data partition ${DATA_DEV} -> ${DATA_MOUNT}"
mkdir -p "$DATA_MOUNT"
FSTAB_LINE="LABEL=${DATA_LABEL} ${DATA_MOUNT} ext4 defaults,noatime,nofail,x-systemd.device-timeout=15 0 2"
# Only add it if /mnt/data is not in fstab at all (whether by LABEL, UUID or path,
# e.g. from dietpi-drive_manager) - otherwise we get duplicate entries.
if ! awk -v mp="$DATA_MOUNT" '$1 !~ /^#/ && $2 == mp { found = 1 } END { exit !found }' /etc/fstab; then
    backup_once /etc/fstab
    printf '%s\n' "$FSTAB_LINE" >>/etc/fstab
fi
systemctl daemon-reload
mountpoint -q "$DATA_MOUNT" || mount "$DATA_MOUNT"
mountpoint -q "$DATA_MOUNT" || die "${DATA_MOUNT} could not be mounted."

install -d -m 0755 -o "$BUILD_USER" -g "$BUILD_USER" "${DATA_MOUNT}/git"

# ----------------------------------------------------------------- Packages
log "Installing packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y gpsd gpsd-clients pps-tools chrony lighttpd busybox git \
    overlayroot initramfs-tools sudo avahi-daemon libnss-mdns
if ((!NO_AP)); then
    install_nostart hostapd dnsmasq iw rfkill wireless-regdb
fi
apt-get install -y --no-install-recommends build-essential fakeroot devscripts equivs

# Without this mark apt could later remove busybox/overlayroot as unneeded
apt-mark manual busybox overlayroot

# ----------------------------------------------------------------- PPS (GPIO)
log "PPS overlay (GPIO${PPS_GPIO})"
if grep -qE '^\s*dtoverlay=pps-gpio' "$CONFIG_TXT"; then
    echo "pps-gpio overlay already present in ${CONFIG_TXT} - unchanged."
else
    backup_once "$CONFIG_TXT"
    printf '\n# Weather station: PPS input\ndtoverlay=pps-gpio,gpiopin=%s\n' "$PPS_GPIO" >>"$CONFIG_TXT"
fi

# ----------------------------------------------------------------- gpsd
log "Configuring gpsd (${GPS_DEV})"
backup_once /etc/default/gpsd
cat >/etc/default/gpsd <<EOF
# Generated by meteo-setup.sh
START_DAEMON="true"
# -n: read the GPS right away, even without a connected client (required for chrony)
GPSD_OPTIONS="-n"
DEVICES="${GPS_DEV}"
# No USB autodetect: gpsd might otherwise probe the MAWS interface (ttyUSB0) as a GPS
USBAUTO="false"
GPSD_SOCKET="/var/run/gpsd.sock"
EOF
# DietPi disables IPv6 by default, but Debian's gpsd.socket also listens on [::1]:2947
# and then fails completely ("Cannot assign requested address"); gpsd.service depends
# on it. So listen only on IPv4 loopback and the Unix socket.
install -d /etc/systemd/system/gpsd.socket.d
cat >/etc/systemd/system/gpsd.socket.d/10-no-ipv6.conf <<EOF
[Socket]
ListenStream=
ListenStream=/run/gpsd.sock
ListenStream=127.0.0.1:2947
EOF
systemctl enable gpsd.socket gpsd.service

# ----------------------------------------------------------------- chrony
log "Configuring chrony (GPS/NMEA + PPS)"
systemctl disable --now systemd-timesyncd 2>/dev/null || true
install -d -m 0750 -o _chrony -g _chrony "${DATA_MOUNT}/chrony"
backup_once /etc/chrony/chrony.conf
cat >/etc/chrony/chrony.conf <<EOF
# Generated by meteo-setup.sh - time source: GPS (NMEA via gpsd) + PPS (GPIO${PPS_GPIO})
# No internet NTP, no NTP server for other devices (no 'allow').

# Coarse time from gpsd (shared memory). Offset/delay are rough values for a USB GPS.
refclock SHM 0 refid NMEA offset 0.051 delay 0.2
# Precise second edge from the PPS input, locked to the NMEA second
refclock PPS /dev/pps0 refid PPS lock NMEA prefer

# Drift file on the data partition (root FS is volatile later)
driftfile ${DATA_MOUNT}/chrony/chrony.drift

# Clock without RTC: always allow stepping at startup
makestep 1.0 -1

# Local control via chronyc only
bindcmdaddress 127.0.0.1
bindcmdaddress ::1
EOF
install -d /etc/systemd/system/chrony.service.d
cat >/etc/systemd/system/chrony.service.d/10-data-mount.conf <<EOF
[Unit]
RequiresMountsFor=${DATA_MOUNT}
EOF

# ----------------------------------------------------------------- Build meteo
log "meteo: source code to ${METEO_DIR}"
if [[ -d ${METEO_DIR}/.git ]]; then
    as_build git -C "$METEO_DIR" pull --ff-only
else
    as_build git clone "$METEO_REPO" "$METEO_DIR"
fi

log "meteo: build dependencies"
BDTMP="$(mktemp -d)"
(cd "$BDTMP" && mk-build-deps -i -r -t "apt-get -y --no-install-recommends" "${METEO_DIR}/debian/control")
rm -rf "$BDTMP"

log "meteo: building Debian package"
as_build bash -c "cd '${METEO_DIR}' && dpkg-buildpackage -b -uc"

DEB="$(find "${DATA_MOUNT}/git" -maxdepth 1 -name 'meteo_*.deb' -printf '%T@ %p\n' | sort -nr | head -n1 | cut -d' ' -f2-)"
[[ -n $DEB && -f $DEB ]] || die "No meteo_*.deb found."
log "meteo: installing ${DEB}"
apt-get install -y "$DEB"

systemctl daemon-reload
systemctl enable --now lighttpd
systemctl restart chrony || warn "chrony restart failed (the PPS device only appears after the reboot)."
systemctl restart gpsd.socket gpsd || warn "gpsd restart failed (GPS not plugged in?)."

# The package ships meteo.service (incl. RequiresMountsFor=/mnt/data, After=gpsd), the
# user 'meteo', /mnt/data/meteodata (tmpfiles) and enables the lighttpd web client.
systemctl is-enabled --quiet meteo.service || warn "meteo.service is not enabled."
systemctl is-active --quiet meteo.service || warn "meteo.service is not running yet (weather station connected to /dev/ttyUSB0? The service restarts every 30 s)."
[[ -d ${DATA_MOUNT}/meteodata ]] || warn "${DATA_MOUNT}/meteodata is missing - package tmpfiles not applied?"
compgen -G "/etc/lighttpd/conf-enabled/*webmeteo*" >/dev/null || warn "lighttpd configuration webmeteo is not enabled (lighttpd-enable-mod webmeteo)."
systemctl restart lighttpd

# ----------------------------------------------------------------- Hostname + avahi (mDNS)
log "Hostname '${MDNS_HOST}' -> ${MDNS_HOST}.local (avahi)"
backup_once /etc/hosts
hostnamectl set-hostname "$MDNS_HOST"
if grep -q '^127\.0\.1\.1' /etc/hosts; then
    sed -i "s/^127\.0\.1\.1.*/127.0.1.1 ${MDNS_HOST}/" /etc/hosts
else
    printf '127.0.1.1 %s\n' "$MDNS_HOST" >>/etc/hosts
fi
systemctl enable avahi-daemon
systemctl restart avahi-daemon || warn "avahi-daemon restart failed."

# ----------------------------------------------------------------- Wi-Fi access point
# hostapd (Wi-Fi) + dnsmasq (DHCP, DNS answer for <host>.local) + static IP via ifupdown.
# None of it is started now - the AP becomes active after the reboot.
AP_SUMMARY=""
show_ap_credentials() { :; }
if ((NO_AP)); then
    log "Wi-Fi access point skipped (--no-ap)"
else
    log "Wi-Fi access point: ${WLAN_IF}, SSID '${AP_SSID}', ${AP_IP}/24"

    # Passphrase: given > existing one from hostapd.conf (rerun) > random
    if [[ -z $AP_PASSPHRASE && -f /etc/hostapd/hostapd.conf ]]; then
        AP_PASSPHRASE="$(sed -n 's/^wpa_passphrase=//p' /etc/hostapd/hostapd.conf | head -n1)"
    fi
    if [[ -z $AP_PASSPHRASE ]]; then
        AP_PASSPHRASE="$(head -c 48 /dev/urandom | base64 -w0 | tr -dc 'A-Za-z0-9' | cut -c1-16)"
    fi
    [[ $AP_PASSPHRASE =~ ^.{8,63}$ ]] || die "Invalid Wi-Fi passphrase (8-63 characters required)."

    install -d -m 0755 /etc/hostapd
    (
        umask 077
        cat >/etc/hostapd/hostapd.conf <<EOF
# Generated by meteo-setup.sh
interface=${WLAN_IF}
driver=nl80211
ssid=${AP_SSID}
country_code=${AP_COUNTRY}
ieee80211d=1
hw_mode=g
channel=${AP_CHANNEL}
ieee80211n=1
wmm_enabled=1
auth_algs=1
ignore_broadcast_ssid=0
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_passphrase=${AP_PASSPHRASE}
EOF
    )
    chmod 600 /etc/hostapd/hostapd.conf

    backup_once /etc/default/hostapd
    printf '# Generated by meteo-setup.sh\nDAEMON_CONF="/etc/hostapd/hostapd.conf"\n' >/etc/default/hostapd

    install -d /etc/systemd/system/hostapd.service.d
    cat >/etc/systemd/system/hostapd.service.d/10-meteo.conf <<EOF
[Unit]
Wants=sys-subsystem-net-devices-${WLAN_IF}.device
After=sys-subsystem-net-devices-${WLAN_IF}.device

[Service]
# Wi-Fi can be blocked by rfkill until a country is set
ExecStartPre=-/bin/sh -c 'rfkill unblock wlan'
Restart=on-failure
RestartSec=5
EOF

    install -d /etc/dnsmasq.d
    cat >/etc/dnsmasq.d/meteo-ap.conf <<EOF
# Generated by meteo-setup.sh - DHCP/DNS on the access point only
interface=${WLAN_IF}
bind-dynamic
no-resolv
domain-needed
bogus-priv
dhcp-authoritative
dhcp-range=${AP_NET}.10,${AP_NET}.100,255.255.255.0,12h
dhcp-option=option:router,${AP_IP}
dhcp-option=option:dns-server,${AP_IP}
# For devices without mDNS (e.g. some Android versions): resolve the name via DNS
address=/${MDNS_HOST}.local/${AP_IP}
EOF
    dnsmasq --test

    # Static IP for the Wi-Fi interface (ifupdown). An existing configuration of the
    # interface (e.g. Wi-Fi client from dietpi-wifi) is commented out.
    backup_once /etc/network/interfaces
    sed -i '/^# BEGIN meteo-ap$/,/^# END meteo-ap$/d' /etc/network/interfaces
    AWKTMP="$(mktemp)"
    awk -v ifc="$WLAN_IF" '
        /^#/ || /^$/ { print; next }
        /^(auto|allow-hotplug|allow-auto|iface|mapping|source|source-directory|no-auto-down|no-scripts)[[:space:]]/ {
            skip = ($0 ~ ("^(auto|allow-hotplug|allow-auto|iface)[[:space:]]+" ifc "([[:space:]]|$)"))
        }
        skip { print "# (meteo-setup) " $0; next }
        { print }
    ' /etc/network/interfaces >"$AWKTMP"
    cat "$AWKTMP" >/etc/network/interfaces
    rm -f "$AWKTMP"
    cat >>/etc/network/interfaces <<EOF

# BEGIN meteo-ap
allow-hotplug ${WLAN_IF}
iface ${WLAN_IF} inet static
    address ${AP_IP}
    netmask 255.255.255.0
# END meteo-ap
EOF

    # Wi-Fi client services must not claim the interface (effective after the reboot)
    systemctl disable wpa_supplicant 2>/dev/null || true
    systemctl disable dietpi-wifi-monitor 2>/dev/null || true

    systemctl unmask hostapd
    systemctl enable hostapd dnsmasq
    systemctl daemon-reload

    AP_SUMMARY="
Access point (active after the reboot):
  SSID '${AP_SSID}', WPA2, channel ${AP_CHANNEL}, Pi = ${AP_IP}, DHCP ${AP_NET}.10-${AP_NET}.100
  Browser:   http://${MDNS_HOST}.local/   or   http://${AP_IP}/
  Password:  sudo grep wpa_passphrase /etc/hostapd/hostapd.conf   (not in the log)
  Check:     ip -4 addr show ${WLAN_IF} ; iw dev ${WLAN_IF} info
             systemctl status hostapd dnsmasq avahi-daemon
  Note:      ${WLAN_IF} is from now on access point only (no Wi-Fi client, no internet
             over Wi-Fi). Use an Ethernet cable for updates and dietpi-software."

    # Wi-Fi credentials go to the terminal only, never to the log (stdout runs through tee)
    show_ap_credentials() {
        {
            printf '\n*** Wi-Fi credentials (not saved in the log) ***\n'
            printf '    SSID:     %s\n    Password: %s\n\n' "$AP_SSID" "$AP_PASSPHRASE"
        } >/dev/tty 2>/dev/null || warn "No terminal available - retrieve the password with: sudo grep wpa_passphrase /etc/hostapd/hostapd.conf"
    }
fi

# ----------------------------------------------------------------- OverlayRoot
if ((SKIP_OVERLAY)); then
    log "OverlayRoot skipped (--skip-overlay). Setup complete."
    echo "Reboot required (hostname, access point): sudo reboot"
    save_log
    show_ap_credentials
    exit 0
fi

log "Setting up OverlayRoot"
# / stays rw in /etc/fstab (do NOT use the DietPi 'read-only root' option)
grep -qE '^\S+\s+/\s+\S+\s+\S*\brw\b' /etc/fstab || warn "Root entry in /etc/fstab is not clearly 'rw' - please check!"

backup_once /etc/overlayroot.conf
cat >/etc/overlayroot.conf <<'EOF'
overlayroot_cfgdisk="disabled"
overlayroot="tmpfs:recurse=0"
EOF

# -c fails if an initramfs already exists (e.g. from a previous run), so fall back to -u
update-initramfs -c -k "$(uname -r)" || update-initramfs -u -k "$(uname -r)"
ensure_line "$CONFIG_TXT" "auto_initramfs=1"
if ! compgen -G "${BOOT_DIR}/initr*" >/dev/null && ! compgen -G "/boot/initrd.img-*" >/dev/null; then
    warn "No initramfs file found in ${BOOT_DIR} or /boot - OverlayRoot may not become active!"
fi

sync
save_log

cat <<EOF

=====================================================================
Setup complete. After the reboot, check:
  findmnt /                        -> source 'overlayroot'
  tail /run/initramfs/overlayroot.log   -> ends with [success]
  findmnt ${DATA_MOUNT}            -> separate partition, writable
  sudo ppstest /dev/pps0           -> PPS pulses once per second
  cgps -s   (or gpsmon)            -> GPS fix
  chronyc sources -v               -> NMEA (#?) and PPS (#*)
  systemctl status meteo           -> service running
  Browser: http://${MDNS_HOST}.local/meteo/   -> web client, recordings under /download/
${AP_SUMMARY}

Permanent changes (packages, config, e.g. /etc/default/meteo) afterwards only via:
  sudo overlayroot-chroot
Updating meteo (as user ${BUILD_USER}, not root):
  cd ${METEO_DIR} && tools/update.sh   (reboot afterwards)

IMPORTANT - DietPi tools (dietpi-software, dietpi-config, dietpi-update ...):
They ONLY work correctly with OverlayRoot disabled. Procedure:
  1. Disable and reboot:
       sudo sed -i 's/^auto_initramfs=1/#auto_initramfs=1/' ${CONFIG_TXT} && sudo reboot
     (findmnt / then shows NO 'overlayroot' any more; all changes are permanent)
  2. Use the DietPi tools (install, configure, update software).
  3. Enable again and reboot:
       sudo update-initramfs -u -k all
       sudo sed -i 's/^#auto_initramfs=1/auto_initramfs=1/' ${CONFIG_TXT} && sudo reboot
While OverlayRoot is off, the SD card is NOT protected against power loss.

Emergency (Pi does not boot): mount the boot partition on a PC, remove
'auto_initramfs=1' from config.txt and delete 'overlayroot=' from cmdline.txt.
=====================================================================
EOF

show_ap_credentials

if ((NO_REBOOT)); then
    echo "Trigger the reboot manually:  sudo reboot"
else
    echo "Reboot in 1 minute (cancel with: shutdown -c)"
    shutdown -r +1
fi
