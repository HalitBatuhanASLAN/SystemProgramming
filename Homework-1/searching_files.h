#ifndef SEARCHING_FILES_H
#define SEARCHING_FILES_H

#include "parse_argv.h" /* for search_criteria*/

/*
    recursivly it travers the tree/directory and checks if find match files
    @param path: the path of the directory to search
    @param depth: the depth of the current directory in the tree (used for printing)
    @param criteria: the search criteria to match files against
    @param found: pointer to an integer that will be set to 1 if any matching
*/
void search_directory(const char *path, int depth, const Search_criteria *criteria, int *found);

#endif