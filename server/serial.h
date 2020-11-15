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

#ifndef SERIAL_H
#define SERIAL_H

#include "meteoserver.h"

void close_serial(void);
int open_serial(void);
void set_baudrate(const char *arg);
void set_serial_interface(const char *dname);
char *read_serial(ssize_t *len);

#endif /* SERIAL_H */