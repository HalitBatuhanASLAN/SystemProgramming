#include "parse_argv.h"

#include <stdio.h>   /* fprintf() — printing error and usage messages  */
#include <string.h>  /* strlen()  — used in permission length check     */
#include <stdlib.h>  /* exit(), atoi(), atol() — exit and conversions   */
#include <unistd.h>  /* getopt(), optarg — command-line argument parser */

/* that functions is just checking if the file type is valid */
static int is_valid_file_type(const char t)
{
    int valid = (t == 'b' || t == 'c' || t == 'd' || t == 'f' || t == 'l' || t == 'p' || t == 's');
    return valid;
}

/* this function is just checking if the permissions are valid */
static int is_valid_permission(const char *p)
{
    if(strlen(p) != 9) return 0;

    const char valid_characters[3][2] = {{'r','-'},{'w','-'},{'x','-'}};
    
    int k;
    for(int i = 0; i<9; i++)
    {
        k = i % 3;
        if(p[i] != valid_characters[k][0] && p[i] != valid_characters[k][1])
            return 0;
    }
    return 1;
}

/*
    by using getopt it parses the command line argument into a form like this:w:f:b:t:p:l: and according to the given arguments it fills the struct(search_criteia) and if there is a mistake it gives message and exits program
   @param argc     : Number of command-line arguments
   @param argv     : Array of command-line argument strings
   @param criteria : Pointer to the struct to fill with parsed values
*/
void parse_argv(int argc, char *argv[], Search_criteria *criteria)
{
    /* initialize with default values */
    criteria->searching_path = NULL;
    criteria->file_name = NULL;
    criteria->file_size = -1;
    criteria->file_type = '\0';
    criteria->permissions = NULL;
    criteria->link_count = -1;
    
    int opt;
    /*  opt stores the current option character returned by getopt()
        optarg points directly into argv at the value of the current option, so you can use it directly without copying.
    */
    while((opt = getopt(argc,argv,"w:f:b:t:p:l:")) != -1)
    {
        switch(opt)
        {
            case 'w':
                criteria->searching_path = optarg;
                break;
            case 'f':
                criteria->file_name = optarg;
                break;
            case 'b':
                /* for converting into integel ascii to long int*/
                criteria->file_size = atol(optarg);
                if(criteria->file_size < 0)
                {
                    fprintf(stderr, "Error: -b must be nonnegative number\n");
                    print_correct_usage(argv[0]);
                    exit(1);
                }
                break;
            case 't':
                if(is_valid_file_type(optarg[0]))
                    criteria->file_type = optarg[0];
                else
                {
                    fprintf(stderr,"Invalid file type: %c\n",optarg[0]);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                if(is_valid_permission(optarg))
                    criteria->permissions = optarg;
                else
                {
                    fprintf(stderr,"Invalid permissions: %s\n",optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'l':
                criteria->link_count = atoi(optarg);
                break;
            
            default:
                /* getopt() returns '?' for unrecognized options */
                fprintf(stderr,"Usage: %s -w <searching_path> -f <file_name> -b <file_size> -t <file_type> -p <permissions> -l <link_count>\n",argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    /* user must give path for searching*/
    if(criteria->searching_path == NULL)
    {
        fprintf(stderr,"Error: -w must be given\n");
        print_correct_usage(argv[0]);
        exit(1);
    }

    /* at least one criteria must be given */
    if(criteria->file_name == NULL && criteria->file_size == -1 && criteria->file_type == '\0' && criteria->permissions == NULL && criteria->link_count == -1)
    {
        fprintf(stderr,"Error: At leeast one of the criterias must be given\n");
        print_correct_usage(argv[0]);
        exit(1);
    }
}

/* it is for printing correct usge of command-line options 
    AI is give that directly.
*/
void print_correct_usage(char *program_name)
{
    fprintf(stderr,"Correct usage:\n");
    fprintf(stderr," %s -w <searching_path> [other options]\n\n",program_name);

    fprintf(stderr,"Must be given:\n");
    fprintf(stderr," -w <searching_path>: The path to search for files\n\n");
    fprintf(stderr,"               like: -w /home/user/documents\n\n");

    fprintf(stderr,"Options (at least one must be used):\n");

    fprintf(stderr," -f <file_name>: File name pattern (Case-insensitive)\n");
    fprintf(stderr,"                 '+' operator is supported (1+ of previous char)\n\n");
    fprintf(stderr,"               like: -f \"report\" -> report, REPORT\n");
    fprintf(stderr,"               like: -f \"los+t\"  -> lost, losst, lossst\n\n");

    fprintf(stderr," -b <byte_size>: File size (exact match, in bytes)\n\n");
    fprintf(stderr,"               like: -b 1024 -> exactly 1024 bytes\n");
    fprintf(stderr,"               like: -b 0    -> empty files\n\n");

    fprintf(stderr," -t <type>: The type of the file\n");
    fprintf(stderr,"            f -> regular file\n");
    fprintf(stderr,"            d -> directory\n");
    fprintf(stderr,"            l -> symbolic link\n");
    fprintf(stderr,"            s -> socket\n");
    fprintf(stderr,"            b -> block device\n");
    fprintf(stderr,"            c -> character device\n");
    fprintf(stderr,"            p -> pipe\n\n");
    fprintf(stderr,"               like: -t f\n\n");

    fprintf(stderr," -p <permissions>: File permissions (9 characters)\n");
    fprintf(stderr,"                   Format: [rwx-][rwx-][rwx-]\n");
    fprintf(stderr,"                           owner  group  other\n\n");
    fprintf(stderr,"               like: -p \"rwxr-xr-x\"\n");
    fprintf(stderr,"               like: -p \"rw-r--r--\"\n\n");

    fprintf(stderr," -l <count>: Number of hard links\n\n");
    fprintf(stderr,"               like: -l 1\n\n");
}