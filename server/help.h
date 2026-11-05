// Part of WebMeteo, a Vaisalla weather data visualization.
//
// Copyright (c) 2020 Michael Wolf <michael@mictronics.de>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef HELP_H
#define HELP_H

#include <argp.h>
const char *argp_program_bug_address = "Michael Wolf <michael@mictronics.de>";
static error_t parse_opt(int key, char *arg, struct argp_state *state);

enum
{
        OPTSERIAL = 1000,
        OPTBAUDRATE,
        OPTGROUP,
        OPTUSER,
        OPTNOGPS
};

static struct argp_option options[] =
    {
        {0, 0, 0, 0, "Options:", 1},
        {"ip", 'i', "IP", 0, "Listen IP address or host [default: all interfaces]", 1},
        {"port", 'p', "port", 0, "Listen port [default: 8080]", 1},
        {"serial", OPTSERIAL, "serial device", 0, "Serial device [default: /dev/ttyUSB0]", 1},
        {"baudrate", OPTBAUDRATE, "baudrate", 0, "Serial baudrate [default: 9600]", 1},
        {"group", OPTGROUP, "group id", 0, "Websocket group id [default: -1]", 1},
        {"user", OPTUSER, "user id", 0, "Websocket user id [default: -1]", 1},
#ifndef LWS_NO_DAEMONIZE
        {"daemon", 'D', 0, 0, "Run meteoserver as a daemon", 1},
#endif
        {"debug", 'd', "debug level", 0, "libwebsockets log level bitmask [default: 7 (errors, warnings, notices)]", 1},
        {"no-gps", OPTNOGPS, 0, 0, "Disable GPS support", 1},
        {0}};

#endif /* HELP_H */