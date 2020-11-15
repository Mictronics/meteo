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
        {"ip", 'i', "IP", OPTION_ARG_OPTIONAL, "Listen IP address or host [default: 127.0.0.1]", 1},
        {"port", 'p', "port", OPTION_ARG_OPTIONAL, "Listen port [default: 8080]", 1},
        {"serial", OPTSERIAL, "serial device", OPTION_ARG_OPTIONAL, "Serial device [default: /dev/ttyUSB0]", 1},
        {"baudrate", OPTBAUDRATE, "baudrate", OPTION_ARG_OPTIONAL, "Serial baudrate [default: 9600]", 1},
        {"group", OPTGROUP, "group id", OPTION_ARG_OPTIONAL, "Websocket group id [default: -1]", 1},
        {"user", OPTUSER, "user id", OPTION_ARG_OPTIONAL, "Websocket user id [default: -1]", 1},
#ifndef LWS_NO_DAEMONIZE
        {"daemon", 'D', 0, OPTION_ARG_OPTIONAL, "Run meteoserver as a daemon", 1},
#endif
        {"debug", 'd', "debug level", OPTION_ARG_OPTIONAL, "Set debug level [default: 0]", 1},
        {"no-gps", OPTNOGPS, 0, OPTION_ARG_OPTIONAL, "Disable GPS support", 1},
        {0}};

#endif /* HELP_H */