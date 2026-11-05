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

Install package `sudo dpkg -i meteo_1.0.0_armhf.deb`.

### Removal

Remove the package `sudo apt-get purge meteo`.

### Configuration

To configure the service add commandline options to `/etc/default/meteo` as required.

The websocket server has no authentication and listens on all interfaces unless
`-i <address>` is given. Restrict access to the port (default in the package: 10024) with a firewall.

## Building manually

You can probably just run "make" after installing the required dependencies
(libwebsockets 4.0 or newer). `make test` runs the self-check of the moving average.
Binaries are built in the source directory; you will need to arrange to
install them (and a method for starting them) yourself.
