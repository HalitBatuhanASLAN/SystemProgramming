#ifndef CHECK_FILES_H
#define CHECK_FILES_H

#include<sys/stat.h>/* mode_t, struct stat, lstat(), S_ISREG(), S_ISDIR()
                        * and other file type macros — required for file
                        * information retrieval and type checking */
#include "parse_argv.h" /* Search_criteria struct — needed to access the
                         * search criteria fields (file_name, file_size, etc.) */

/*
    It checks if the file at the given path matches all the specified criteria in the Search_criteria struct.
    @param file_path : The full path to the file being checked
    @param file_name : The name of the file (not the full path, just the
    @param criteria  : Pointer to the Search_criteria struct containing the search criteria to check against

*/
int check_matches(const char *file_path, const char *file_name, const Search_criteria *criteria);

#endif