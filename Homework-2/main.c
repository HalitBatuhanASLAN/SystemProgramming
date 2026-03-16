#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "argument_parsing.h"
#include "signals_handler.h"
#include "partition_of_workers.h"
#include "searching.h"
#include "worker.h"
#include "print_result.h"

int main(int argc, char *argv[])
{
    ProcSearchArguments arguments;
    parse_arguments(argc, argv, &arguments);

    setup_parent_signals();

    Worker_Partition partitions[MAX_WORKERS];
    Worker_Result    results[MAX_WORKERS];

    int actual_workers = partition_directories(arguments.root_directory,
                                               arguments.num_of_workers,
                                               partitions);

    if (actual_workers == 0)
    {
        Searching_Result result;
        init_searching_result(&result);
        /* verbose=1: parent direkt arıyor, MATCH yazdır */
        search_directory(arguments.root_directory, arguments.pattern,
                         arguments.min_size, &result, 1);

        results[0].pid         = getpid();
        results[0].match_count = result.match_count;

        print_tree(arguments.root_directory, arguments.pattern,
                   arguments.min_size, results, 1);
        print_summary(1, result.scan_count, result.match_count, results);
        return 0;
    }

    launch_workers(partitions, actual_workers, arguments.pattern,
                   arguments.min_size, results);

    wait_for_workers(actual_workers, results);

    if (got_sigint)
    {
        int total_matches = 0;
        for (int i = 0; i < actual_workers; i++)
            total_matches += results[i].match_count;
        print_summary(actual_workers, 0, total_matches, results);
        free_partitions(partitions, actual_workers);
        return 0;
    }

    int total_matches = 0;
    int total_scanned = 0;
    for (int i = 0; i < actual_workers; i++)
        total_matches += results[i].match_count;

    Searching_Result final_result;
    init_searching_result(&final_result);
    /* verbose=0: sadece scan_count için, MATCH yazdırma */
    search_directory(arguments.root_directory, arguments.pattern,
                     0, &final_result, 0);
    total_scanned = final_result.scan_count;

    print_tree(arguments.root_directory, arguments.pattern,
               arguments.min_size, results, actual_workers);
    print_summary(actual_workers, total_scanned, total_matches, results);

    free_partitions(partitions, actual_workers);
    return 0;
}