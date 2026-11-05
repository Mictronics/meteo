# Meteo

Websocket server and web client to visualize data from a Vaisalla weather station during EASA acoustic compliance tests.

## Debian/Raspbian packages

It is designed to build as a Debian package.

### Dependencies

- libpthread-stubs0-dev
- libwebsockets-dev
- libgps-dev

For GPS support gpsd daemon needs to be installed and a GPS device connected.

### Actually building it

Nothing special, just build it `dpkg-buildpackage -b -uc`.

### Installation

Install package `sudo dpkg -i meteo_<version>_armhf.deb`.

### Removal

Remove the package `sudo apt-get purge meteo`.

### Configuration

To configure the service add commandline options to `/etc/default/meteo` as required.

The websocket server has no authentication of its own and listens on all interfaces unless
`-i <address>` is given. Every client may operate the UI (recording, runway data, MAWS time sync).
It is meant to run on an isolated Wi-Fi access point, where the Wi-Fi credentials are the access
control. Do not connect it to the internet or to an untrusted network.

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
(libwebsockets 4.0 or newer). `make test` runs the self-check of the moving average.
Binaries are built in the source directory; you will need to arrange to
install them (and a method for starting them) yourself.
