#ifndef SERIAL_H
#define SERIAL_H

#include "meteoserver.h"

void close_serial(void);
int open_serial(void);
void set_baudrate(const char *arg);
void set_serial_interface(const char *dname);

#endif /* SERIAL_H */