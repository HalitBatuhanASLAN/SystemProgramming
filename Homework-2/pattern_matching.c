#include "pattern_matching.h"

#include<string.h>
#include<stdio.h>
#include<ctype.h>

static int is_same(char a, char b)
{
    return tolower(a) == tolower(b);
}

int is_match_pattern(const char *file_name, const char *pattern)
{
    int len_of_file_name = strlen(file_name);
    int len_of_pattern = strlen(pattern);

    int i = 0, j = 0, k = 0;
    for(; i < len_of_file_name && j < len_of_pattern; i++, j++)
    {
        if(pattern[j] == '+')
        {
            k = 0;
            while(i + k < len_of_file_name && is_same(file_name[i + k], file_name[i-1]))
                k++;
            if(j + 1 < len_of_pattern && is_same(pattern[j + 1], file_name[i - 1]) && k > 0)
                k--;
            i = i + k - 1;
        }
        else if(!is_same(pattern[j], file_name[i]))
            return 0;
    }
    while(j < len_of_pattern && pattern[j] == '+')
        j++;
    return (i == len_of_file_name && j == len_of_pattern);
}