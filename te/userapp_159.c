#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define LEN        1000
// ./u330 3000 330 10 &
// ./u660 3000 660 10 &
long factorial(long n) {
        if(n == 0) return 1;
        else return n * factorial(n - 1);
}
void do_job(int iteration) {    
    long rst = 0;
    for(int i = 0; i < iteration; ++i) {
        rst += factorial(1000);
    }
}
int main(int argc, char* argv[])
{
    int loop_cycle = atoi(argv[3]), i, len, offset;
    unsigned int pid = getpid();
    char buf[LEN];
    struct timeval t0, start, end;

    if (argc != 4) {
        puts("error in argc");
        return 1;
    }
    

    char cmd_R[100];
    memset(cmd_R, '\0', 100);
    sprintf(cmd_R, "echo \"R, %u, %s, %s\" > /proc/%s/%s", pid, argv[1], argv[2], "mp2", "status");
    system(cmd_R);

    gettimeofday(&t0, NULL);
    char cmd_Y[100];
    memset(cmd_Y, '\0', 100);
    sprintf(cmd_Y, "echo \"Y, %u\" > /proc/%s/%s", pid, "mp2", "status");
    system(cmd_Y);

    for (i = 0; i < loop_cycle; i ++) {
        gettimeofday(&start, NULL);
        do_job(30000);
        gettimeofday(&end, NULL);
        double te = end.tv_sec * 1000.0 + end.tv_usec / 1000.0 - (t0.tv_sec * 1000.0 + t0.tv_usec / 1000.0);
double ts = start.tv_sec * 1000.0 + start.tv_usec / 1000.0 - (t0.tv_sec * 1000.0 + t0.tv_usec / 1000.0);
        printf("task: %u, start: %f end: %f, the period is: %s \n",pid, ts, te, argv[1]);
        char cmd_Y1[100];
        memset(cmd_Y1, '\0', 100);
        sprintf(cmd_Y1, "echo \"Y, %u\" > /proc/%s/%s", pid, "mp2", "status");
        system(cmd_Y1);
    }

    char cmd_D[100];
    memset(cmd_D, '\0', 100);
    sprintf(cmd_D, "echo \"D, %u\" > /proc/%s/%s", pid, "mp2", "status");
    system(cmd_D);
    return 0;
}

