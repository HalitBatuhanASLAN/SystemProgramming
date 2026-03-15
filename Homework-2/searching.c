#include "searching.h"

#include <stdio.h>    /* printf(), fprintf()                */
#include <string.h>   /* strcmp()                           */
#include <errno.h>    /* errno                              */
#include <dirent.h>   /* opendir(), readdir(), closedir()   */
#include <sys/stat.h> /* lstat(), S_ISDIR(), S_ISREG()      */
#include <unistd.h>   /* getpid()                           */

#define MAX_PATH_LENGTH 4096

void init_searching_result(Searching_Result *result)
{
    result->match_count = 0;
    result->scan_count = 0;
}

/* Kaynak: TLPI Listing 18-2 (PDF sayfa 400) — opendir/readdir kalıbı
 *         TLPI PDF sayfa 324-326 — lstat() ve S_ISDIR/S_ISREG kullanımı */

void search_directory(const char *directory_path, const char *pattern, long min_size, Searching_Result *result)
{
    DIR *directory;
    struct dirent *dp;
    struct stat st;
    char full_path[MAX_PATH_LENGTH];

    directory = opendir(directory_path);
    if(directory == NULL)
    {
        fprintf(stderr, "Error: cannot open directory '%s'\n", directory_path);
        return;
    }
    /* --- Listing 18-2'deki döngü kalıbı --- */
    for (;;) {
        errno = 0;              /* Hata ile sonu ayırt et (TLPI sayfa 398) */
        dp = readdir(directory);
        if (dp == NULL) break;

        if (strcmp(dp->d_name, ".")  == 0) continue;
        if (strcmp(dp->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path),
                 "%s/%s", directory_path, dp->d_name);

        if (lstat(full_path, &st) == -1) {
            fprintf(stderr, "Error: lstat failed for '%s'\n", full_path);
            continue;
        }

        if (S_ISDIR(st.st_mode))
            search_directory(full_path, pattern, min_size, result);
        else if (S_ISREG(st.st_mode))
        {
            result->scan_count++;

            if (!matches_pattern(dp->d_name, pattern))
                continue;

            if (min_size > 0 && st.st_size < min_size)
                continue;

            result->match_count++;
            printf("[Worker PID:%d] MATCH: %s (%lld bytes)\n",
                   (int)getpid(),
                   full_path,
                   (long long)st.st_size);
        }
    }

    if(errno != 0)
        fprintf(stderr, "Error: readdir failed for '%s'\n", directory_path);

    closedir(directory);
}



/*

for(;;) + errno=0 + readdir döngüsüTLPI Listing 18-2, PDF sayfa 400
. ve .. atlama kalıbıTLPI Listing 18-2, PDF sayfa 400
lstat() + S_ISDIR/S_ISREGTLPI PDF sayfa 324–326
closedir döngü dışındaTLPI Listing 18-2, PDF sayfa 400

*/