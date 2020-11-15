CC = gcc
CPPFLAGS += -D_GNU_SOURCE

DIALECT = -std=c18
CFLAGS += $(DIALECT) -od -g -W -D_DEFAULT_SOURCE -Wall -fno-common -Wmissing-declarations
LIBS = -lpthread -lwebsockets -lgps -lm
LDFLAGS =

all: meteoserver

%.o: server/%.c server/*.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

meteoserver: server/meteoserver.o server/serial.o server/timer.o
	$(CC) -g -o server/$@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -f server/*.o server/meteoserver
