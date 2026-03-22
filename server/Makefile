CC      ?= gcc
CFLAGS  ?= -Wall -Werror -g
LDFLAGS ?=

# Build switch: set USE_AESD_CHAR_DEVICE=1 (default) to redirect reads/writes
# to /dev/aesdchar instead of /var/tmp/aesdsocketdata, suppress timestamps,
# and skip deleting the endpoint on exit.
USE_AESD_CHAR_DEVICE ?= 1
ifeq ($(USE_AESD_CHAR_DEVICE),1)
DEVICE_FLAG = -DUSE_AESD_CHAR_DEVICE
endif

TARGET = aesdsocket

.PHONY: all default clean

default: $(TARGET)
all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) $(DEVICE_FLAG) -pthread -o $(TARGET) $< $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o
