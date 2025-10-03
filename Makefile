CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE
TARGET = main
SRC = main.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
	rm -rf /tmp/container_root

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

.PHONY: clean install