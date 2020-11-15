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

## Building manually

You can probably just run "make" after installing the required dependencies.
Binaries are built in the source directory; you will need to arrange to
install them (and a method for starting them) yourself.
