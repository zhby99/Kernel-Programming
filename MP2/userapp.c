#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>


long factorial(long n) {
        if(n == 0) return 1;
        else return n * factorial(n - 1);
}
void working(int iteration) {
    long rst = 0;
    for(int i = 0; i < iteration; ++i) {
        rst += factorial(1000);
    }
}
int main(int argc, char* argv[])
{
    int loop_cycle = atoi(argv[3]), i, len, offset;
    unsigned int pid = getpid();
    struct timeval t0, start, end;

    if (argc != 4) {
        puts("error in argc");
        return 1;
    }


    // register process through /proc/mp2/status
    char register_command[100];
    memset(register_command, 0, 100);
    sprintf(register_command, "echo \"R, %u, %s, %s\" > /proc/%s/%s", pid, argv[1], argv[2], "mp2", "status");
    system(register_command);

    gettimeofday(&t0, NULL);
    char yield_command[100];
    memset(yield_command, 0, 100);
    sprintf(yield_command, "echo \"Y, %u\" > /proc/%s/%s", pid, "mp2", "status");
    system(yield_command);

    for (i = 0; i < loop_cycle; ++i) {
        gettimeofday(&start, NULL);
        working(60000);
        gettimeofday(&end, NULL);
        double te = end.tv_sec * 1000.0 + end.tv_usec / 1000.0 - (t0.tv_sec * 1000.0 + t0.tv_usec / 1000.0);
        double ts = start.tv_sec * 1000.0 + start.tv_usec / 1000.0 - (t0.tv_sec * 1000.0 + t0.tv_usec / 1000.0);
        printf("task: %u, start: %f end: %f, the period is: %s \n",pid, ts, te, argv[1]);
        memset(yield_command, 0, 100);
        sprintf(yield_command, "echo \"Y, %u\" > /proc/%s/%s", pid, "mp2", "status");
        system(yield_command);
    }

    char de_regis_command[100];
    memset(de_regis_command, 0, 100);
    sprintf(de_regis_command, "echo \"D, %u\" > /proc/%s/%s", pid, "mp2", "status");
    system(de_regis_command);
    return 0;
}
