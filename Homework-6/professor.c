#include "client_app.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: ./professor <server_ip> <tcp_port> <username>\n");
        return 1;
    }
    // Professor is TCP client too, but server checks its permissions.
    return run_client_app("PROFESSOR", argv[1], argv[2], argv[3]);
}
