/*!
 * \file yloop_example.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <libvapi/vloop.h>

int main(int argc, char* argv[])
{
    //do loop initialistion, like adding timers, initialising database access, ...

    //if you don't add anything to the loop, the loop will have nothing todo, and will exit

    printf("loop init done, return 0\n");

    return VAPI_SUCCESS;
}
