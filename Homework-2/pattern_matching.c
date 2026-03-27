#include "pattern_matching.h"

#include <string.h>
#include <ctype.h>

/* checks for case-insensitive character rule mentioned as in psf*/
static int is_same(char a, char b)
{
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

/*
    it is recursivly, checks if pattern matches after spesidic index in text, it is main purpose is to
    handle + operator issue when situations like abc+cde pattern
    t -> current index in text
    p -> current index in patetrn
*/
static int match_at_recursive(const char *text, int text_len, int t,
                              const char *pattern, int pat_len, int p)
{
    /*if end of pattern it is successfull*/
    if (p == pat_len) return t;

    /*if nex one is + charecter*/
    if (p + 1 < pat_len && pattern[p + 1] == '+')
    {
        char repeat_char = pattern[p];

        /*if you reached end of or characters are not same no matching*/
        if (t >= text_len || !is_same(text[t], repeat_char))
            return -1;

        /*try to find max number of matching character(like abc+ then count c number ofter that point)*/
        int max_k = 0;
        while (t + max_k < text_len && is_same(text[t + max_k], repeat_char))
            max_k++;

        /*backtracking part, but starting from max, to 1 for preventing abc+c fails in abcccc situaiton*/
        for (int k = max_k; k >= 1; k--)
        {
            int res = match_at_recursive(text, text_len, t + k, pattern, pat_len, p + 2);
            if (res != -1) return res;
        }
        return -1;
    }
    else
    {
        /*just character matches normally*/
        if(t >= text_len || !is_same(text[t], pattern[p]))
            return -1;
        return match_at_recursive(text, text_len, t + 1, pattern, pat_len, p + 1);
    }
}

/*
    checks if pattern can be found any positon of file
*/
int is_match_pattern(const char *file_name, const char *pattern)
{
    int fn_len  = (int)strlen(file_name);
    int pat_len = (int)strlen(pattern);

    if(pat_len == 0)
        return 1;

    /*
        by using recursive controller function checks if characters mathes
    */
    for(int i = 0; i <= fn_len; i++)
    {
        if(match_at_recursive(file_name, fn_len, i, pattern, pat_len, 0) >= 0)
            return 1;
    }
    return 0;
}