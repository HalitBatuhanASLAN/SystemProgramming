#include "searching_files.h"
#include "print_tree.h" /* for print_leaf */
#include "check_files.h" /* for check_matches */
#include "stopping_handler.h" /* continue_running — SIGINT flag */

#include<dirent.h> /* for opendir, readdir, closedir */
#include<sys/stat.h> /* for lstat, struct stat, S_ISDIR */
#include<stdio.h> /* for fprintf, stderr */
#include<string.h> /* for strcmp, snprintf */

/* Maximum length for a file path to prevent buffer overflow*/
#define MAX_PATH_LENGTH 4096

/* as we know that . and .. are not useful for us, we can ignore them
  . is for itself and .. is for parent
*/
static int is_dot_or_dotdot(const char *name)
{
    return (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
}

/*
    builds a full path by concatenating the parent directory and the entry name
    @param dest: buffer to store the resulting path
    @param parent: the parent directory path
    @param name: the name of the file or directory to append to the parent path
*/
static void build_path(char *dest, const char *parent, const char *name)
{
    /* snprintf is mximize according to MAX__path_lenght*/
    snprintf(dest, MAX_PATH_LENGTH, "%s/%s", parent, name);
}

/*
    Recursivly it travers directories and checks if find match files
    @param path: the path of the directory to search
    @param depth: the depth of the current directory in the tree (used for printing)
    @param criteria: the search criteria to match files against
    @param found_any: pointer to an integer that will be set to 1 if any
*/
void search_directory(const char *path, int depth, const Search_criteria *criteria, int *found_any)
{
    DIR *directory = opendir(path);
    if(directory == NULL)
    {
        fprintf(stderr, "Error: %s could not be opened.\n", path);
        return;
    }

    /*entry poinst to directory*/
    struct dirent *entry;
    char child_path[MAX_PATH_LENGTH];
    /* keep metadata informations*/
    struct stat file_stat;

    while((entry = readdir(directory)) != NULL)
    {
        if(!continue_running)
        {
            closedir(directory);
            return;
        }

        /* as every directory contains . and .. files we ignore them*/
        if(is_dot_or_dotdot(entry->d_name))
            continue;

        /* by using current directory name and previous path build full path */
        build_path(child_path, path, entry->d_name);

        /* take metadata informations of the file/directory*/
        /* we use lstat instead of stat because it doesn't follow symbolic links */
        if(lstat(child_path, &file_stat) != 0)
        {
            fprintf(stderr, "Error: Could not stat %s.\n", child_path);
            continue;
        }

        /* check if the entry is a directory */
        if(S_ISDIR(file_stat.st_mode))
        {
            if(check_matches(child_path, entry->d_name, criteria))
            {
                print_leaf(entry->d_name, depth + 1);
                *found_any = 1;
            }
            /* recursive part*/
            search_directory(child_path, depth + 1, criteria, found_any);
        }
        else
        {
            /* ,f it is not directory then continue with it is siblings*/
            if(check_matches(child_path, entry->d_name, criteria))
            {
                print_leaf(entry->d_name, depth + 1);
                *found_any = 1;
            }   
        }
    }
    if(closedir(directory) != 0)
        fprintf(stderr, "Error: Could not close directory %s.\n", path);
}

