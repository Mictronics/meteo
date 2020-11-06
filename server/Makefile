CC = gcc
CPPFLAGS += -D_GNU_SOURCE

DIALECT = -std=c18
CFLAGS += $(DIALECT) -o2 -g -W -D_DEFAULT_SOURCE
LIBS = -lpthread -lwebsockets -lgps
LDFLAGS =

all: meteoserver

%.o: %.c *.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

meteoserver: meteoserver.o serial.o timer.o $(SDR_OBJ) $(COMPAT)
	$(CC) -g -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o meteoserver
