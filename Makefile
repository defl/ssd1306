src = $(wildcard *.c)
obj = $(src:.c=.o)

CFLAGS=`pkg-config --cflags glib-2.0` -std=c11
CFLAGS+=-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

LDFLAGS=`pkg-config --libs glib-2.0`

%.o: %.c
	$(CC) $(CFLAGS) -c $<

all: onewire_server_state_display

install: onewire_server_state_display
	install -m 755 onewire_server_state_display $(DESTDIR)/bin/

onewire_server_state_display: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) onewire_server_state_display
