CC = gcc
CFLAGS = -Wall -Wextra -std=c11

all: hogwarts wizard professor

hogwarts: hogwarts.o server.o server_options.o server_log.o server_clients.o server_protocol.o common.o ingredients.o
	$(CC) $(CFLAGS) -o hogwarts hogwarts.o server.o server_options.o server_log.o server_clients.o server_protocol.o common.o ingredients.o

wizard: wizard.o client_app.o common.o
	$(CC) $(CFLAGS) -o wizard wizard.o client_app.o common.o

professor: professor.o client_app.o common.o
	$(CC) $(CFLAGS) -o professor professor.o client_app.o common.o

hogwarts.o: hogwarts.c server.h
	$(CC) $(CFLAGS) -c hogwarts.c

server.o: server.c server.h server_types.h server_options.h server_log.h server_clients.h server_protocol.h common.h ingredients.h
	$(CC) $(CFLAGS) -c server.c

server_options.o: server_options.c server_options.h server_types.h common.h
	$(CC) $(CFLAGS) -c server_options.c

server_log.o: server_log.c server_log.h server_types.h
	$(CC) $(CFLAGS) -c server_log.c

server_clients.o: server_clients.c server_clients.h server_log.h server_types.h common.h
	$(CC) $(CFLAGS) -c server_clients.c

server_protocol.o: server_protocol.c server_protocol.h server_clients.h server_log.h server_types.h common.h ingredients.h
	$(CC) $(CFLAGS) -c server_protocol.c

wizard.o: wizard.c client_app.h
	$(CC) $(CFLAGS) -c wizard.c

professor.o: professor.c client_app.h
	$(CC) $(CFLAGS) -c professor.c

client_app.o: client_app.c client_app.h common.h
	$(CC) $(CFLAGS) -c client_app.c

common.o: common.c common.h
	$(CC) $(CFLAGS) -c common.c

ingredients.o: ingredients.c ingredients.h common.h
	$(CC) $(CFLAGS) -c ingredients.c

clean:
	rm -f hogwarts wizard professor *.o
