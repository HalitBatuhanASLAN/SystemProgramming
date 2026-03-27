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

    int actual_workers = partition_directories(arguments.root_directory,arguments.num_of_workers,partitions);

    if (actual_workers == 0)
    {
        Searching_Result result;
        init_searching_result(&result);
        search_directory(arguments.root_directory, arguments.pattern,
                         arguments.min_size, &result, 1, NULL);

        results[0].pid         = getpid();
        results[0].match_count = result.match_count;

        printf("%s\n", arguments.root_directory);
        if (result.match_count == 0)
            printf("No matching files found.\n");
        print_summary(1, result.scan_count, result.match_count, results);
        return 0;
    }

    char parent_tmp_name[256];
    snprintf(parent_tmp_name, sizeof(parent_tmp_name),
             "/tmp/worker_%d_tmp.txt", (int)getpid());

    FILE *parent_tmp = fopen(parent_tmp_name, "w");
    Searching_Result parent_result;
    init_searching_result(&parent_result);
    search_root_files(arguments.root_directory, arguments.pattern,
                      arguments.min_size, &parent_result, parent_tmp);
    if (parent_tmp != NULL)
    {
        fflush(parent_tmp);
        fclose(parent_tmp);
    }

    char parent_file_name[256];
    snprintf(parent_file_name, sizeof(parent_file_name),
             "/tmp/worker_%d.txt", (int)getpid());
    {
        FILE *final_f = fopen(parent_file_name, "w");
        if (final_f != NULL)
        {
            fprintf(final_f, "Scanned:%d\n", parent_result.scan_count);
            FILE *tmp = fopen(parent_tmp_name, "r");
            if (tmp != NULL)
            {
                char line[4096];
                while (fgets(line, sizeof(line), tmp))
                    fputs(line, final_f);
                fclose(tmp);
            }
            fclose(final_f);
        }
    }
    remove(parent_tmp_name);

    launch_workers(partitions, actual_workers, arguments.pattern,arguments.min_size, results);

    wait_for_workers(actual_workers, results);

    if (got_sigint)
    {
        Worker_Result all_results[MAX_WORKERS + 1];
        for (int i = 0; i < actual_workers; i++)
            all_results[i] = results[i];
            
        all_results[actual_workers].pid = getpid();
        all_results[actual_workers].match_count = 0; 
        int total_result_count = actual_workers + 1;

        int tree_total_scanned = 0;
        print_tree_with_scanned(arguments.root_directory, all_results, total_result_count, &tree_total_scanned);

        int total_matches = 0;
        for (int i = 0; i < actual_workers; i++)
        {
            results[i].match_count = all_results[i].match_count;
            total_matches += results[i].match_count;
        }

        remove(parent_file_name);
        
        print_partial_summary(actual_workers, tree_total_scanned, total_matches, results);
        
        free_partitions(partitions, actual_workers);
        return 0;
    }

    Worker_Result all_results[MAX_WORKERS + 1];
    for (int i = 0; i < actual_workers; i++)
        all_results[i] = results[i];
    all_results[actual_workers].pid = getpid();
    all_results[actual_workers].match_count = 0;
    int total_result_count = actual_workers + 1;

    int tree_total_scanned = 0;
    print_tree_with_scanned(arguments.root_directory, all_results, total_result_count, &tree_total_scanned);

    int total_matches = 0;
    for (int i = 0; i < actual_workers; i++)
    {
        results[i].match_count = all_results[i].match_count;
        total_matches += results[i].match_count;
    }

    print_summary(actual_workers, tree_total_scanned, total_matches, results);

    free_partitions(partitions, actual_workers);
    return 0;
}