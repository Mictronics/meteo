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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <poll.h>
#include "serial.h"

static char serial_name[255] = "";
static const char *serial_interface = "/dev/ttyUSB0";
static int baudrate = B9600;
static int serial_fd = -1;
static char serial_buffer[1024];

void set_baudrate(const char *arg)
{
    long br;
    char *end;

    br = strtol(arg, &end, 0);
    if (*end || !*arg)
    {
        fprintf(stderr, "Baudrate is not a number!\n");
        return;
    }

    switch (br)
    {
    case 1200:
        baudrate = B1200;
        break;
    case 2400:
        baudrate = B2400;
        break;
    case 4800:
        baudrate = B4800;
        break;
    case 9600:
        baudrate = B9600;
        break;
    case 19200:
        baudrate = B19200;
        break;
    case 38400:
        baudrate = B38400;
        break;
    case 57600:
        baudrate = B57600;
        break;
    case 115200:
        baudrate = B115200;
        break;
    default:
        baudrate = B9600;
        fprintf(stderr, "Baudrate %ld not supported. Set by default 9600 Baud.\n", br);
        break;
    }
}

void set_serial_interface(const char *dname)
{
    strncpy(serial_name, dname, sizeof serial_name);
    serial_name[(sizeof serial_name) - 1] = '\0';
    serial_interface = serial_name;
}

int open_serial(void)
{
    struct termios tios;

    serial_fd = open(serial_interface, O_RDWR | O_NOCTTY);
    if (serial_fd < 0)
    {
        fprintf(stderr, "Failed to open serial device %s: %s\n",
                serial_interface, strerror(errno));
        return (EXIT_FAILURE);
    }

    if (tcgetattr(serial_fd, &tios) < 0)
    {
        fprintf(stderr, "tcgetattr(%s): %s\n", serial_interface, strerror(errno));
        return (EXIT_FAILURE);
    }

    memset(&tios, 0, sizeof(tios));
    tios.c_iflag = 0;
    tios.c_oflag = 0;
    tios.c_cflag |= CS8 | CREAD | CLOCAL; // 8N1 no handshake
    tios.c_lflag = ICANON;                // Read line by line
    tios.c_cc[VMIN] = 1;
    tios.c_cc[VTIME] = 1;

    if (cfsetispeed(&tios, baudrate) < 0)
    {
        fprintf(stderr, "Serial cfsetispeed(%s): %s\n",
                serial_interface, strerror(errno));
        return (EXIT_FAILURE);
    }

    if (cfsetospeed(&tios, baudrate) < 0)
    {
        fprintf(stderr, "Serial cfsetospeed(%s): %s\n",
                serial_interface, strerror(errno));
        return (EXIT_FAILURE);
    }

    tcflush(serial_fd, TCIFLUSH);

    if (tcsetattr(serial_fd, TCSANOW, &tios) < 0)
    {
        fprintf(stderr, "Serial tcsetattr(%s): %s\n",
                serial_interface, strerror(errno));
        return (EXIT_FAILURE);
    }

    // Kick on handshake and start reception
    //int RTSDTR_flag = TIOCM_RTS | TIOCM_DTR;
    //ioctl(Modes.beast_fd, TIOCMBIS, &RTSDTR_flag); //Set RTS&DTR pin

    return EXIT_SUCCESS;
}

void close_serial(void)
{
    close(serial_fd);
}

char *read_serial(ssize_t *len)
{
    *len = read(serial_fd, serial_buffer, sizeof(serial_buffer) - 1);
    return serial_buffer;
}