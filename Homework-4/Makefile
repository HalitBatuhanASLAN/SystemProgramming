CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -O2 -g
SRCS    = src/main.c src/shm.c src/reader.c src/dispatcher.c \
          src/analyzer.c src/aggregator.c src/watchdog.c
TARGET  = analyzer

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) *.o results.txt results.bin results.bin.tmp
