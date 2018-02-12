#include "userapp.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

void register_process(unsigned int pid){
    char command[256];
    memset(command, 0, 256);
    sprintf(command, "echo %u > /proc/mp1/status", pid);
    system(command);
}

int main(int argc, char* argv[]){
    int total = 25;
    time_t start = time(NULL);
    register_process(getpid());
    while (1) {
        if ((int)(time(NULL) - start) > total) {
            break;
        }
    }

	return 0;
}
