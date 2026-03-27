CC      = gcc
CFLAGS  = -Wall -Wextra -g

TARGET  = procSearch

SRCS    = main.c \
          argument_parsing.c \
          pattern_matching.c \
          partition_of_workers.c \
          searching.c \
          print_result.c \
          signals_handler.c \
          worker.c

OBJS    = $(SRCS:.c=.o)

# ── Compile ──────────────────────────────────────────────────
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# ── crete .o for each .c ──────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── clean verything ─────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean

#   rm -f /tmp/worker_*.txt -> to clean workeroutputs if stopped by -9(kill) signal