#include "stats_writer.h"

#include <stdio.h>
#include <stdatomic.h>

int write_stats_file(const char *path, const cargo_context_t *context)
{
    FILE *file;
    int completed = atomic_load(&context->completed_orders);
    int cancelled = atomic_load(&context->cancelled_orders);
    long total_time = atomic_load(&context->total_delivery_time);
    long average = completed > 0 ? total_time / completed : 0;
    int index;

    file = fopen(path, "w");
    if(file == NULL)
    {
        return 0;
    }

    fprintf(file, "SHIFT_SUMMARY\n");
    fprintf(file, "Total orders    : %d\n", completed + cancelled);
    fprintf(file, "Completed       : %d\n", completed);
    fprintf(file, "Cancelled       : %d\n", cancelled);
    fprintf(file, "Total time      : %ldms\n", total_time);
    fprintf(file, "Avg per order   : %ldms\n", average);
    fprintf(file, "\n");
    fprintf(file, "COURIER_STATS\n");

    for(index = 0; index < context->courier_count; index++)
    {
        fprintf(file,
                "Courier-%d  completed=%d  total_time=%ldms\n",
                index + 1,
                context->courier_stats[index].completed_orders,
                context->courier_stats[index].total_delivery_time);
    }

    fclose(file);
    return 1;
}
