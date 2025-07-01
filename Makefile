# Makefile for Writer as target

CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -Wextra -std=c11 -O2

TARGET = writer

SRCS = finder-app/writer.c

all: $(TARGET)

$(TARGET): writer.o
	$(CC) writer.o -o $(TARGET)

writer.o: $(SRCS)
	$(CC) $(CFLAGS) -c $(SRCS) -o writer.o

clean:
	rm -f *.o $(TARGET)


