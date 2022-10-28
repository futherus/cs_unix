#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

int main(int argc, char** argv)
{
    int pid = 0;

    for (int i = 1; i < argc; i++)
    {
        pid = fork();
        if (pid == 0)
        {
            usleep(10000 * atoi(argv[i]));
            printf("%s ", argv[i]);
            return 0;
        }
    }

    int status;
    while (--argc)
        wait(&status);

    printf("\n");

    return 0;
}

