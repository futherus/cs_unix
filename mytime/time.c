#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

int main(int argc, char** argv)
{
    memmove(argv, argv + 1, (size_t) (argc - 1) * sizeof(char*));
    argv[argc - 1] = NULL;

    struct timeval start = {};
    struct timeval end = {};
    
    int pid = fork();
    if (pid == 0)
    {
        execvp(argv[0], argv);
        fprintf(stderr, "Cannot measure '%s': %s\n", argv[0], strerror(errno));
        return EXIT_FAILURE;
    }

    gettimeofday(&start, NULL);
     
    int status;
    wait(&status);
    if (WEXITSTATUS(status) == EXIT_FAILURE)
        return EXIT_FAILURE;

    gettimeofday(&end, NULL);

    struct timeval diff = {};
    timersub(&end, &start, &diff);

    printf("%ld.%06ld s\n", diff.tv_sec, diff.tv_usec);

    return WEXITSTATUS(status);
}

