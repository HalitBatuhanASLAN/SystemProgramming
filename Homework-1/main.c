#include<stdio.h> /* for printf, fprintf, stderr */

#include "parse_argv.h" /* for search_criteria, parse_argv */
#include "searching_files.h" /* for search_directory */
#include "print_tree.h" /* for print_root */
#include "stopping_handler.h" /* for setup_stopping_handler */

int main(int argc, char *argv[])
{
    /* set up the stopping handler SIGINT(CTRL^C) for any cases*/
    setup_stopping_handler();

    /* to keep search parameters provided by user*/
    Search_criteria criteria;
    /* parse arguments and keep into criteria*/
    parse_argv(argc, argv, &criteria);

    /* print the root directory */
    print_root(criteria.searching_path);

    /* to keep if any file is found or not*/
    int found_any = 0;

    /* search according to the criteria */
    search_directory(criteria.searching_path, 0, &criteria, &found_any);

    if(!found_any)
        printf("No file found.\n");

    return(0);
}