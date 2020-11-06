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
        {"serial", OPTSERIAL, "serial device", OPTION_ARG_OPTIONAL, "Serial device [default: /dev/ttyAMA0]", 1},
        {"baudrate", OPTBAUDRATE, "baudrate", OPTION_ARG_OPTIONAL, "Serial baudrate [default: 115200]", 1},
        {"group", OPTGROUP, "group id", OPTION_ARG_OPTIONAL, "Websocket group id [default: -1]", 1},
        {"user", OPTUSER, "user id", OPTION_ARG_OPTIONAL, "Websocket user id [default: -1]", 1},
#ifndef LWS_NO_DAEMONIZE
        {"daemon", 'D', 0, OPTION_ARG_OPTIONAL, "Run meteoserver as a daemon", 1},
#endif
        {"debug", 'd', "debug level", OPTION_ARG_OPTIONAL, "Set debug level [default: 0]", 1},
        {"no-gps", OPTNOGPS, 0, OPTION_ARG_OPTIONAL, "Disable GPS support", 1},
        {0}};

#endif /* HELP_H */