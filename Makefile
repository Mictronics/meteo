CC = gcc
CPPFLAGS += -D_GNU_SOURCE

DIALECT = -std=c18
CFLAGS += $(DIALECT) -O2 -g -W -D_DEFAULT_SOURCE -Wall -fno-common -Wmissing-declarations -MMD -MP
LIBS = -lpthread -lwebsockets -lgps -lm
LDFLAGS +=

OBJ = server/meteoserver.o server/serial.o server/average.o

# Version comes from the git tag, see tools/version.sh. debian/rules sets VERSION
# from debian/changelog, because a package build may have no git checkout.
ifndef VERSION
VERSION := $(shell tools/version.sh)
endif

all: meteoserver client/scripts/version.js

# Rewrite generated version files only when the version changed, so builds stay incremental.
server/version.h: FORCE
	@printf '#define METEO_VERSION "%s"\n' '$(VERSION)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

client/scripts/version.js: FORCE
	@printf "const APP_VERSION = '%s';\n" '$(VERSION)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

server/meteoserver.o: server/version.h

# Usage: make release V=1.2.0
release:
	tools/release.sh $(V)

meteoserver: $(OBJ)
	$(CC) -g -o server/$@ $^ $(LDFLAGS) $(LIBS)

# Moving-average self-check, needs no external libraries.
test: server/test_average.o server/average.o
	$(CC) -o server/test_average $^ -lm
	server/test_average

clean:
	rm -f server/*.o server/*.d server/meteoserver server/test_average server/version.h client/scripts/version.js

-include $(OBJ:.o=.d) server/test_average.d

.PHONY: all test clean release FORCE
