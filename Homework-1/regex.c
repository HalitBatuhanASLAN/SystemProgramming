#include "regex.h"

#include<ctype.h>
#include<string.h>

/*
    It is just for helping, diretly check for lower and upper characters. It switchees into lower for both characters and then compares them.
    It is static because we just need it in this file.
*/
static int char_equal(char a, char b)
{
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

/*
    It checks if the given string matches with the given pattern.
*/
int regex_match(const char *string, const char *pattern)
{
    /*take length of both string and pattern*/
    int len_of_string = strlen(string);
    int len_of_pattern = strlen(pattern);
    
    /*outer loop is for iterating through the string*/
    for(int i = 0; i<len_of_string; i++)
    {
        /* if first charecter of pattern matches with current(i'th) character of string*/
        if(char_equal(string[i], pattern[0]))
        {
            int k = 1; /* it is for keeping index for the case + operator usage */
            
            /*check if next characters are also equal or not*/
            for(int j = 1; j<len_of_pattern; j++)
            {
                /*if current character is + operator then by using inner while loop continue to control next characters with previous(current) character of pattern*/
                if(pattern[j] == '+')
                {
                    while(i + k < len_of_string && char_equal(string[i + k], pattern[j - 1]))
                        k++;
                }
                /*if not equal then break inner loop*/
                else if(!char_equal(string[i + k], pattern[j]))
                    break;
                else
                    k++;
                /*if you reach the end of the pattern then you get the match*/ 
                if(j == len_of_pattern - 1)
                    return 1;
            }
            /*if pattern has only one character and it matches with current character of string*/
            if(len_of_pattern == 1)
                return 1;
        }
    }
    return 0;
}