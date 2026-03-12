#ifndef REGEX_H
#define REGEX_H

/*
    That function checks if the given string matches with the given pattern.
    But there are some rules like upper and lower characters are not important, it means that
    if you look for 'A' and you catch 'a' it is okey.
    Another rule and much more important one is if you use + at some point of pattern, it means that you can search for all strings which have at least one of the previous character. For example if you look for "a+b" you can find "ab", "aab", "aaab" and so goes like that.
*/
int regex_match(const char *string, const char *pattern);

#endif