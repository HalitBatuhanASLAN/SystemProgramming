CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread -D_POSIX_C_SOURCE=200809L
TARGET = cargoGTU
OBJS = main.o cli.o order.o priority_queue.o input_parser.o logger.o cargo_context.o signal_state.o courier.o stats_writer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

main.o: main.c cargo_context.h cli.h courier.h input_parser.h signal_state.h stats_writer.h
cli.o: cli.c cli.h
order.o: order.c order.h
priority_queue.o: priority_queue.c priority_queue.h order.h
input_parser.o: input_parser.c input_parser.h priority_queue.h order.h
logger.o: logger.c logger.h order.h
cargo_context.o: cargo_context.c cargo_context.h logger.h priority_queue.h order.h
signal_state.o: signal_state.c signal_state.h
courier.o: courier.c courier.h cargo_context.h signal_state.h
stats_writer.o: stats_writer.c stats_writer.h cargo_context.h

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
