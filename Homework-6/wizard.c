#include "client_app.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: ./wizard <server_ip> <tcp_port> <username>\n");
        return 1;
    }
    return run_client_app("WIZARD", argv[1], argv[2], argv[3]);
}
