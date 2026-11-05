CC = gcc
CPPFLAGS += -D_GNU_SOURCE

DIALECT = -std=c18
CFLAGS += $(DIALECT) -O2 -g -W -D_DEFAULT_SOURCE -Wall -fno-common -Wmissing-declarations -MMD -MP
LIBS = -lpthread -lwebsockets -lgps -lm
LDFLAGS =

OBJ = server/meteoserver.o server/serial.o server/average.o

all: meteoserver

meteoserver: $(OBJ)
	$(CC) -g -o server/$@ $^ $(LDFLAGS) $(LIBS)

# Moving-average self-check, needs no external libraries.
test: server/test_average.o server/average.o
	$(CC) -o server/test_average $^ -lm
	server/test_average

clean:
	rm -f server/*.o server/*.d server/meteoserver server/test_average

-include $(OBJ:.o=.d) server/test_average.d

.PHONY: all test clean
