#ifndef PARSE_ARGV_H
#define PARSE_ARGV_H

/*
    This is a structure to keep all researrch criteria which will be provided by user
*/
typedef struct
{
    /* directory path to search and path is providing by using -w flag*/
    char *searching_path;

    /* file name to pattern matching and name is providing by using -f flag*/
    char *file_name;
    
    /* to look for exact file size and providing by using -b flag*/
    long int file_size;
    
    /* to look for exact file type and providing by using -t flag*/
    /*
        'f' -> regular file
        'd' -> directory
        'l' -> symbolic link
        's' -> socket
        'b' -> block device
        'c' -> character device
    */
    char file_type;
    
    /* searching for exact permissions of files and providing by using -p flag
        Format: [rwx-][rwx-][rwx-] (owner, group, other)
    */
    char *permissions;
    
    /* exact number of hard links to search through files and providing by using -l flag*/
    int link_count;
} Search_criteria;

/*
    according to given command-line arguments it fills the struct(search_criteia) and if there is a mistake it gives message and exits program
  @param argc     : The number of command-line arguments (from main)
  @param argv     : The array of command-line argument strings (from main)
  @param criteria : Pointer to the Search_criteria struct to be filled
*/
void parse_argv(int argc, char *argv[], Search_criteria *criteria);

/*
    That function just printing the correct usage of command line arguments and exits the program
    @param program_name : The name of the executable (argv[0]),used to display the correct program name in the usage message
*/
void print_correct_usage(char *program_name);

#endif