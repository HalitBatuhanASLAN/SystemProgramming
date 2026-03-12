#include "check_files.h"
#include "regex.h" /* regex_match() — for filename pattern matching */
#include<string.h> /* strcmp() — for permission string comparison */
#include<stdio.h> /* fprintf() — for error messages */

/*
    It checks if the expected file typr(expected) is matcging with the file type of the file which is understoood by mode and it is st_mode field of struct stat
*/
static int check_type(mode_t mode, char expected)
{
    switch(expected)
    {
        case 'f': return S_ISREG(mode); /*regular file*/
        case 'd': return S_ISDIR(mode); /*directory*/
        case 'l': return S_ISLNK(mode); /*symbolic link*/
        case 's': return S_ISSOCK(mode); /*socket*/
        case 'b': return S_ISBLK(mode); /*block device*/
        case 'c': return S_ISCHR(mode); /*character device*/
        case 'p': return S_ISFIFO(mode); /*named pipe(FIFO)*/
        default:  return 0;
    }
}

/*
    It checks if permissions of the file(which is understoood by mode and it is st_mode field of struct stat) and expected permission combination
    @param mode     : The st_mode field from struct stat
    @param expected : The 9-character permission string to compare against
*/
static int check_permissions(mode_t mode, const char *expected)
{
    char actual[10];
    /* r -> read, w -> write, x -> execute */
    /*owner permissions*/
    actual[0] = (mode & S_IRUSR) ? 'r' : '-';
    actual[1] = (mode & S_IWUSR) ? 'w' : '-';
    actual[2] = (mode & S_IXUSR) ? 'x' : '-';
    /*group permissions*/
    actual[3] = (mode & S_IRGRP) ? 'r' : '-';
    actual[4] = (mode & S_IWGRP) ? 'w' : '-';
    actual[5] = (mode & S_IXGRP) ? 'x' : '-';
    /*other permissions*/
    actual[6] = (mode & S_IROTH) ? 'r' : '-';
    actual[7] = (mode & S_IWOTH) ? 'w' : '-';
    actual[8] = (mode & S_IXOTH) ? 'x' : '-';
    actual[9] = '\0';

    return (strcmp(actual, expected) == 0);
}

/*
    According to the given criteria in the Search_criteria struct, it checks if the file at the given path matches all the specified criteria. It returns 1 if all criteria are matched, otherwise returns 0.
*/
int check_matches(const char *file_path, const char *file_name, const Search_criteria *criteria)
{
    struct stat file_stat; /* it keeps the all metadata infos about file */
    /* lstat takes the metadata informations of file*/
    if(lstat(file_path, &file_stat) != 0)
    {
        fprintf(stderr,"Error: Could not stat file '%s'\n", file_path);
        return 0;
    }

    /* firstly it checks if file name is not null then it checks if wanted(expected) name and its names are matching or not */
    if(criteria->file_name != NULL)
    {
        if(!regex_match(file_name, criteria->file_name))
            return 0;
    }

    /* checks if the file size matches the expected size */
    if(criteria->file_size != -1)
    {
        if(file_stat.st_size != criteria->file_size)
            return 0;
    }

    /* checks if the file type matches the expected type */
    if(criteria->file_type != '\0')
    {
        if(!check_type(file_stat.st_mode, criteria->file_type))
            return 0;
    }

    /* checks if the file permissions match the expected permissions */
    if(criteria->permissions != NULL)
    {
        if(!check_permissions(file_stat.st_mode, criteria->permissions))
            return 0;
    }

    /* checks if the link count matches the expected link count */
    if(criteria->link_count != -1)
    {
        if((int)file_stat.st_nlink != criteria->link_count)
            return 0;
    }

    return 1;
}


