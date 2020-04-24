CC = gcc
CFLAGS = -Wall
INSTALL = `which install`

mcp3008hwspi: mcp3008hwspi.c
	$(CC) $(CFLAGS) mcp3008hwspi.c -o mcp3008hwspi

install: mcp3008hwspi
	$(INSTALL) ./mcp3008hwspi /usr/local/bin/mcp3008hwspi

clean:
	rm -f mcp3008hwspi
