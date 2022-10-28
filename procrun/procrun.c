#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

typedef struct
{
    int n_runners; 
} args_t;

const char* PROGNAME = NULL;    

static int
error(char* fmt, ...)
{
    fprintf(stderr, "%s: ", PROGNAME);

	va_list args = {};
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	return 1;
}

static int
parse_args(int argc, char* argv[], args_t* args)
{
    while (optind < argc)
    {
        int opt = getopt(argc, argv, "+n:");
        switch (opt)
        {
            case -1:
                break;
            case 'n':
                args->n_runners = atoi(optarg);
                continue;
            case '?':
            default:
                // FIXME
                fprintf(stderr, "Wrong usage\n");
                abort();
        }
    }

    if (args->n_runners == 0)
        return error("missing runners amount\n");

    return 0;
}

static const long ID_READY = LONG_MAX;

static int
runner(int mqid, long runner_id)
{
    /* stub for message value */
    long dummy = 0;

    /* send empty 'ready' message to judge */
    printf("runner %ld: I am here!\n", runner_id);
    int res = msgsnd(mqid, &ID_READY, 0, 0);
    if (res == -1)
        return error("failed to send 'ready' message: %s\n", strerror(errno));

    /* recieve empty 'baton' message */
    res = (int) msgrcv(mqid, &dummy, 0, runner_id, 0);
    if (res == -1)
        return error("failed to recieve 'baton': %s\n", strerror(errno));

    printf("runner %ld: Running...\n", runner_id);

    /* send empty 'baton' message */
    long next_receiver = runner_id + 1;
    res = msgsnd(mqid, &next_receiver, 0, 0); 
    if (res == -1)
        return error("failed to send 'baton': %s\n", strerror(errno));

    return 0;
}

static int
judge(int mqid, int n_runners)
{
    /* stub for message value */
    long dummy = 0;

    /* recieve empty message from every runner */
    printf("judge: Counting runners\n");
    for (int i = 0; i < n_runners; i++)
    {
        int res = (int) msgrcv(mqid, &dummy, 0, ID_READY, 0);
        if (res == -1)
            return error("failed response from runner: %s\n", strerror(errno));
    }
    printf("judge: All runners have come\n");

    /* send initial 'baton' message */
    long runner_id = 1;
    printf("judge: Passing baton to %ld runner\n", runner_id);
    int res = msgsnd(mqid, &runner_id, 0, 0);
    if (res == -1)
        return error("failed initial 'baton' message passing: %s\n", strerror(errno));

    struct timeval start = {};
    struct timeval end = {};
 
    gettimeofday(&start, NULL);
    
    /* get last 'baton' message */
    res = (int) msgrcv(mqid, &dummy, 0, n_runners + 1, 0);
    if (res == -1)
        return error("failed to receive last 'baton' message: %s\n", strerror(errno));

    gettimeofday(&end, NULL);

    struct timeval diff = {};
    timersub(&end, &start, &diff);

    printf("judge: Received baton from last runner\n");
    printf("judge: time = %ld.%06ld s\n", diff.tv_sec, diff.tv_usec);

    return 0;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];
    
    args_t args = {};
    if (parse_args(argc, argv, &args) != 0)
        return 1;

    int mqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
 
    for (long i = 1; i <= args.n_runners; i++)
    {
        int pid = fork();
        if (pid == 0)
        {
            return runner(mqid, i); 
        }
    }

    int retval = judge(mqid, args.n_runners);
    
    msgctl(mqid, IPC_RMID, 0); 

    return retval;
}
