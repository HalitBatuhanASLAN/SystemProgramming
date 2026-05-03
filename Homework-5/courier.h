#ifndef COURIER_H
#define COURIER_H

#include "cargo_context.h"

typedef struct courier_args
{
    cargo_context_t *context;
    int courier_id;
    int courier_index;
} courier_args_t;

void *courier_thread_main(void *arg);

#endif
