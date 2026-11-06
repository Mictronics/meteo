# Meteo

Websocket server and web client to visualize data from a Vaisala weather station (MAWS) during EASA acoustic compliance tests.

## Overview

- The C server (`server/`) reads the station over a serial port and the position/time from a GPS via gpsd.
  It pushes the data twice a second as a binary websocket packet to all clients.
- The static web client (`client/`) shows wind, temperature, humidity and pressure values, compass pointers
  and charts. The crosswind chart plots the 30 s moving average against the 5 kt limit of ICAO Annex 16.
  Temperature and humidity charts show the limit windows.
- Cross and head wind components are computed from the runway heading. Runway heading and elevation default
  to Manching and can be changed from the UI at runtime.
- Clients can record the data to CSV files and synchronize the MAWS clock to the GPS time.

## Recordings

Recordings are written to `/mnt/data/meteodata/<DDMMYYYY>/F<nnnn>_REC<nnn>_MeteoData_<HHMMSS>.csv`, one row per second.
Folder names, file names and the CSV time are in UTC, matching the MAWS and GPS times. The folder is served
for download at `/download/`. Override the directory at compile time with `-DRECORD_DIR=\"...\"`.

## Debian/Raspbian packages

It is designed to build as a Debian package.

### Dependencies

Build dependencies, installed by `sudo apt-get build-dep .` or `sudo mk-build-deps -i`:

- debhelper-compat 13
- libwebsockets-dev (>= 4.0)
- libgps-dev
- pkg-config

Runtime library dependencies are added automatically. Recommended: `lighttpd` to serve the client and
`gpsd` with a connected GPS device for GPS support.

### Actually building it

Nothing special, just build it `dpkg-buildpackage -b -uc`.

### Installation

Install package `sudo dpkg -i meteo_<version>_<arch>.deb`.

### Removal

Remove the package `sudo apt-get purge meteo`.

### Configuration

To configure the service add commandline options to `/etc/default/meteo` as required
(default `SERVER_OPTIONS="-p10024"`). The client is served by lighttpd at `/meteo/`.

Common options (see `meteoserver --help` or `man meteoserver` for all):

| Option | Meaning |
| --- | --- |
| `-p <port>` | Listen port |
| `-i <address>` | Listen address, default all interfaces |
| `--serial <dev>` | Serial device, default `/dev/ttyUSB0` |
| `--baudrate <rate>` | Serial baudrate, default 9600 |
| `--no-gps` | Disable GPS support |
| `-d <level>` | libwebsockets log level bitmask, default 7 (errors, warnings, notices) |

Logs go to syslog. The server exits with an error if the serial port cannot be opened, so systemd restarts it.

The websocket server has no authentication of its own and listens on all interfaces unless
`-i <address>` is given. Every client may operate the UI (recording, runway data, MAWS time sync).
It is meant to run on an isolated Wi-Fi access point, where the Wi-Fi credentials are the access
control. Do not connect it to the internet or to an untrusted network.

### Read-only root (overlayroot on DietPi)

To protect the SD card against power cuts, the root filesystem can run under a tmpfs overlay.
Recordings are unaffected because `/mnt/data` is a separate partition (`recurse=0` keeps it
a normal mount). Anything else written outside `/mnt/data` is lost at reboot.

1. Keep `/` as `rw` in `/etc/fstab` (do not use the `dietpi-drive_manager` read-only root option,
   it breaks boot). Install `overlayroot` and `busybox`. Without busybox the initramfs has no
   `grep` and overlayroot silently refuses to start.
2. `/etc/overlayroot.conf`:
   ```
   overlayroot_cfgdisk="disabled"
   overlayroot="tmpfs:recurse=0"
   ```
3. Build the initramfs and have the firmware load it, then reboot:
   ```
   sudo apt-mark manual busybox overlayroot
   sudo update-initramfs -c -k $(uname -r)
   echo "auto_initramfs=1" | sudo tee -a /boot/firmware/config.txt
   ```
4. Check: `findmnt /` shows `overlayroot`, and `/run/initramfs/overlayroot.log` ends with `[success]`.

Maintenance: `sudo overlayroot-chroot` gives a shell on the real root for permanent changes
(packages, `/etc/default/meteo`). Edits made on the running system are lost at reboot.

Updating the software: keep the repository on `/mnt/data` (e.g. `/mnt/data/git/meteo`), because
a clone on the overlay disappears at reboot. As a normal user run `tools/update.sh`. It pulls,
builds the package and installs it into the real root through `overlayroot-chroot`. Reboot afterwards.

Recovery if it does not boot: mount the SD card's boot partition on a PC and remove
`auto_initramfs=1` from `config.txt` and any `overlayroot=` from `cmdline.txt`.

## Versioning and releases

Versions follow [semver](https://semver.org). The git tag `vX.Y.Z` is the single source of truth.
Builds between releases are named like `1.1.0-3-g1a2b3c4` (3 commits after v1.1.0).
The version is shown by `meteoserver --version` and in the footer of the web page.
Package builds take it from `debian/changelog` instead.

To release, commit all changes and run `make release V=1.2.0`. This adds a `debian/changelog`
entry from the commit subjects since the last tag, sets `package.json`, commits and creates the
annotated tag `v1.2.0`. Nothing is pushed; publish with `git push origin HEAD v1.2.0`.

## Building manually

You can probably just run "make" after installing the required dependencies
(libwebsockets 4.0 or newer). `make test` runs the self-check of the moving average and needs no
external libraries. `npm install && npm run lint` lints the client code.
Binaries are built in the source directory; you will need to arrange to
install them (and a method for starting them) yourself.
