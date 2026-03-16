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

# ── Derleme ──────────────────────────────────────────────────
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# ── Her .c dosyası için .o üret ──────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── Temizlik ─────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET)

# ── Sahte hedefler ───────────────────────────────────────────
.PHONY: clean