#include "print_result.h"

#include <stdio.h>    /* printf(), fprintf()                */
#include <string.h>   /* strcmp()                           */
#include <errno.h>    /* errno                              */
#include <dirent.h>   /* opendir(), readdir(), closedir()   */
#include <sys/stat.h> /* lstat(), S_ISDIR(), S_ISREG()      */

#define MAX_PATH_LENGTH 4096

static void print_indents(int level)
{
    printf('|');
    for(int i = 0; i< level*6;i++)
        printf('-');
}

/*
 * Recursive ağaç yazdırma
 * Kaynak: TLPI Listing 18-2 (PDF sayfa 400) — opendir/readdir kalıbı
 * Dönüş: bu klasörde bulunan toplam eşleşme sayısı
 */
static int print_tree_recursive(const char  *dir_path,
                                const char  *pattern,
                                long         min_size,
                                int          depth)
{
    DIR           *dirp;
    struct dirent *dp;
    struct stat    st;
    char           full_path[MAX_PATH_LENGTH];
    int            found = 0;

    dirp = opendir(dir_path);
    if (dirp == NULL) {
        fprintf(stderr, "Error: cannot open directory '%s'\n", dir_path);
        return 0;
    }

    for (;;) {
        errno = 0;
        dp = readdir(dirp);
        if (dp == NULL) break;

        if (strcmp(dp->d_name, ".")  == 0) continue;
        if (strcmp(dp->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path),
                 "%s/%s", dir_path, dp->d_name);

        if (lstat(full_path, &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Klasör adını yaz, sonra içine gir */
            print_indent(depth);
            printf(" %s\n", dp->d_name);
            found += print_tree_recursive(full_path, pattern,
                                          min_size, depth + 1);

        } else if (S_ISREG(st.st_mode)) {
            /* Pattern ve boyut kontrolü */
            if (!matches_pattern(dp->d_name, pattern)) continue;
            if (min_size > 0 && st.st_size < min_size) continue;

            print_indent(depth);
            printf(" %s (%lld bytes)\n",
                   dp->d_name,
                   (long long)st.st_size);
            found++;
        }
    }

    if (errno != 0)
        fprintf(stderr, "Error: readdir failed for '%s'\n", dir_path);

    closedir(dirp);
    return found;
}

void print_tree(const char *root_directory, const char *pattern, long min_size, Worker_Result worker_results[MAX_WORKERS], int num_of_workers)
{
    printf("%s\n", root_directory);
    int total = print_tree_recursive(root_directory, pattern, min_size, 1);

    if(total == 0)
        printf("No matching files found.\n");
}

void print_summary(int num_of_workers, int total_scanned, int total_matched, Worker_Result worker_results[MAX_WORKERS])
{
    printf("--- Summary ---\n");
    printf("Total workers used   : %d\n", num_of_workers);
    printf("Total files scanned  : %d\n", total_scanned);
    printf("Total matches found  : %d\n", total_matched);

    for(int i = 0; i < num_of_workers; i++)
    {
        printf("Worker PID %-8d : %d %s\n",
               worker_results[i].pid,
               worker_results[i].match_count,
               worker_results[i].match_count == 1 ? "match" : "matches");
    }
}


