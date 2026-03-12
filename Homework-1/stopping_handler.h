#ifndef STOPPING_HANDLER_H
#define STOPPING_HANDLER_H

#include <signal.h> /* sig_atomic_t — atomic type for signal-safe flag */

/*
    extern makes it accessable from other files
    volatile tells the compiler that the value of this variable may change at any time without any action being taken by the code the compiler find
*/
extern volatile sig_atomic_t continue_running;

/*
    That functions is for handling SIGINT signal(CTRL^C) and exitting from the program by cleaning(free) the all resources wihch it used
*/
void setup_stopping_handler();

#endif