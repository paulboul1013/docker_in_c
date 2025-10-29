CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE
TARGET = main
SRCS = main.c cgroup.c namespace.c rootfs.c
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
	rm -rf /tmp/container_root_*

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

.PHONY: clean install