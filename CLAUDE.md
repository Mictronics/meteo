# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

WebMeteo visualizes data from a Vaisala weather station (MAWS) for EASA acoustic compliance tests. A C websocket server (`server/`) reads the station over serial plus GPS via gpsd and pushes packed binary packets to a static browser client (`client/`).

## Commands

- Build server: `make` (outputs `server/meteoserver`; needs `libwebsockets-dev` >= 4.0, `libgps-dev`, pthread). `make clean` to clean.
- Test: `make test` builds and runs `server/test_average.c`, the moving-average self-check. It needs no external libraries, so it also runs where gpsd headers are missing.
- Debian package: `dpkg-buildpackage -b -uc`
- Release: commit everything, then `make release V=X.Y.Z` (semver). It edits `debian/changelog` and `package.json`, commits and creates the annotated tag `vX.Y.Z`, without pushing.
- Lint client: `npm run lint` (eslint recommended rules, `client/` only, vendored libs ignored)
- Run: `server/meteoserver -p10024 [--serial /dev/ttyUSB0] [--baudrate 9600] [--no-gps] [-d level]`. See `server/help.h` for all options.
- The only automated test is the moving-average self-check above; the rest is checked by hand. `server/vaisalla_log.txt` is a sample of raw serial output, usable to feed a fake serial port.

## Architecture

**Server (C, `server/`)**
- `meteoserver.c` is the main file. It runs several threads, each pinned to a CPU core via `thread_to_core`: the main thread (libwebsockets loop, client commands, recording, timers), a serial read thread (parses MAWS lines, computes cross/head wind components using the runway heading, performs the MAWS time sync on request), and a GPS read thread (gpsd).
- Shared state is one `t_packet_data` struct guarded by `lock_packetdata`; take it with `pthread_mutex_lock` and hold it only to copy. Runway heading and elevation live in that struct too. `record_fp` is only touched by the main thread.
- The packet timer (500 ms, triggers the websocket broadcast) and the record timer (1 s, aligned to the wall-clock second, writes the CSV) are libwebsockets `lws_sul` timers, so they run on the main thread. libwebsockets calls from other threads are not safe.
- `average.c` is the 30 s moving average (ring buffer, vector mean for wind direction), kept separate so it can be tested on its own.
- SIGINT/SIGTERM only set a flag. The main loop exits and does the cleanup. The server exits with a non-zero status if the serial port cannot be opened, so systemd restarts it.
- Default log level is errors, warnings and notices (`-d` takes the libwebsockets bitmask), and logs go to syslog.
- `meteoserver.h` defines the wire format: the packed `t_packet_data` struct sent to clients as a binary websocket frame, plus the client→server commands (`SERVER_CMD_*`: start/stop recording, from/to, elevation, heading, time sync).
- Runway elevation and heading default to Manching constants in `meteoserver.c`; clients can change them at runtime via commands.

**Client (`client/`, static, no build step)**
- `scripts/communication.worker.js` is a Web Worker that owns the websocket and decodes the binary packet. Its field order and sizes must match `t_packet_data`, and its `ServerCmd` constants must match `SERVER_CMD_*` in `meteoserver.h`.
- `scripts/index.js` is the UI and talks to the worker via `postMessage`. `scripts/charts.js` draws charts with d3/d3fc. Vendored libraries are in `scripts/vendors/`. Do not lint or edit them.

**Deployment (`debian/`)**: systemd service `meteo` reads `SERVER_OPTIONS` from `/etc/default/meteo`. lighttpd serves the client from `/usr/share/webmeteo/` at `/meteo/` and recorded data from `/mnt/data/meteodata/` at `/download/`.

## Gotchas

- Changing `t_packet_data` or the `SERVER_CMD_*` values requires matching edits in `communication.worker.js`.
- The version is never edited by hand. The git tag is the truth (`tools/version.sh`); `make` generates the gitignored `server/version.h` and `client/scripts/version.js`, and `debian/rules` takes it from `debian/changelog`.
- The server has no authentication and listens on all interfaces unless `-i` is given. This is deliberate: it runs on an isolated Wi-Fi access point where the Wi-Fi credentials are the access control, and any connected client may operate the UI. Keep security measures minimal; do not add token or Origin checks unless the deployment changes.
- `record_status` is 0 idle, 1 recording, 2 failed (open/write error; cleared by the next start or stop). The client shows it on the Record button.
- Recordings go to `/mnt/data/meteodata` (override with `-DRECORD_DIR=\"...\"` at compile time, handy for tests).
