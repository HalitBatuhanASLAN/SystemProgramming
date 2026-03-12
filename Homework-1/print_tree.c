#include "print_tree.h"

#include<stdio.h>
#include<string.h>

/*
    That function just printing the root of tree
    Example output: /etc
    @param tree : The path of the root search directory
*/
void print_root(const char *tree)
{
    printf("%s\n", tree);
}


/*
    That function for printing the leaf of tree
    Example output: |--child1
    @param name : The name of the file or directory
    @param level : The depth level of the file or directory in the tree
    At each level according to the Hypen per level constant, it prints that many hypens after the | character.
*/
void print_leaf(const char *name, int level)
{
    if(level <= 0) return;

    printf("|");
    
    for(int i = 0; i < level * HYPHEN_PER_LEVEL; i++)
        printf("-");
    printf("%s\n", name);
}