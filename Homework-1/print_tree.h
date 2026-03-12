#ifndef PRINT_TREE_H
#define PRINT_TREE_H
/*
 -> It defines the hypen for each level of the tree.
 -> It should look like this:
    root
    |--child1
    |--child2
    |  |--grandchild1
    |  |--grandchild2
    |--child3
*/
#define HYPHEN_PER_LEVEL 2

/*
    That function for printing only the root of tree
    Example output: /etc
    @param tree : The path of the root search directory
*/
void print_root(const char *tree);

/*
    That function for printing the leaf of tree
    Example output: |--child1
    @param name : The name of the file or directory
    @param level : The depth level of the file or directory in the tree
*/
void print_leaf(const char *name, int level);

#endif