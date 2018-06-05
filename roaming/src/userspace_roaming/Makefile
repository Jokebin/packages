SUBDIRS = src

CFLAGS += -I../include
CC = "$(CROSS_COMPILE)gcc"
AR = "$(CROSS_COMPILE)ar"

LDFLAGS += -L$(STAGING_DIR)/usr/lib/libev -lev -lm
CFLAGS += -I$(STAGING_DIR)/usr/include

all:
	@set -e; \
	for i in $(SUBDIRS); \
		do $(MAKE) $(MFLAGS) -C $$i; done 

install:
	@echo "$(PRO_EXE)"

clean:
	for i in $(SUBDIRS); \
		do $(MAKE) clean -C $$i; done 

.EXPORT_ALL_VARIABLES:
