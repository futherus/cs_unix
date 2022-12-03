#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <assert.h>
#include <poll.h>
#include <stdint.h>

#ifdef DEBUG
    #define $DBG(FMT, ...) fprintf(stderr, "%s: " FMT "\n", __PRETTY_FUNCTION__, ##__VA_ARGS__)
#else
    #define $DBG(FMT, ...)
#endif

typedef struct
{
    int   is_verbose;
    char* prog_name;
    int   n_procs;
} Args;

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
parseArgs(int argc, char* argv[], Args* args)
{
    while (optind < argc)
    {
        int opt = getopt(argc, argv, "+vn:a:");
        switch (opt)
        {
            case -1: break;
            case 'v':
                args->is_verbose = 1;
                continue;
            case 'n':
                args->n_procs = atoi(optarg);
                break;
            case 'a':
                args->prog_name = optarg;
                break;
            case '?':
            default:
                // FIXME
                fprintf(stderr, "Wrong usage\n");
                abort();
        }
    }

    return 0;
}

static void
closePipes(int (*fildes)[2], size_t n_pipes)
{
    for (size_t i = 0; i < n_pipes; i++)
    {
        if (fildes[i][0] != -1)
            close(fildes[i][0]);

        if (fildes[i][1] != -1)
            close(fildes[i][1]);
    }

    free(fildes);
}

static int
initPipes(int (**fildes_ptr)[2], size_t n_pipes)
{
    int (*fildes)[2] = malloc(2 * n_pipes * sizeof(int));
    if (fildes == NULL)
        return error("bad alloc: %s\n", strerror(errno));

    for (size_t i = 0; i < n_pipes; i++)
    {
        fildes[i][0] = -1;
        fildes[i][1] = -1;
    }

    for (size_t indx = 0; indx < n_pipes; indx++)
    {
        if (pipe(fildes[indx]) == -1)
        {
            closePipes(fildes, n_pipes);
            return error("creating pipe failed: %s\n", strerror(errno));
        }

        $DBG("r%d w%d", fildes[indx][0], fildes[indx][1]);
    }

    *fildes_ptr = fildes;

    return 0;
}

static int
startProcs(int (*fildes)[2], const Args* args)
{
    $DBG("entered");
    assert(args->n_procs > 0 && "No cmds to execute");
    size_t n_procs = (size_t) args->n_procs;

    for (size_t i = 0; i < n_procs; i++)
    {
        int pid = fork();
        int* stdin_fd = &fildes[2 * i][0];
        int* stdout_fd = &fildes[2 * i + 1][1];

        if (pid == 0)
        {
            $DBG("%zu-th: stdin=%d, stdout=%d", i, *stdin_fd, *stdout_fd);
            if (dup2(*stdin_fd, STDIN_FILENO) == -1)
            {
                closePipes(fildes, n_procs * 2);
                return error("descriptor duplication failed: %s\n", strerror(errno));
            }

            if (dup2(*stdout_fd, STDOUT_FILENO) == -1)
            {
                closePipes(fildes, n_procs * 2);
                return error("descriptor duplication failed: %s\n", strerror(errno));
            }

            closePipes(fildes, n_procs * 2);

            execlp(args->prog_name, args->prog_name, NULL);

            return error("cannot run <%s>: %s\n", args->prog_name, strerror(errno));
        }
        close(*stdin_fd);
        close(*stdout_fd);
        *stdin_fd = -1;
        *stdout_fd = -1;
    }
 
    $DBG("leaving");
    return 0;
}

static const size_t BUFFER_CAP = 0x100;

typedef struct
{
    struct pollfd* read;
    struct pollfd* write;

    char*  buf;
    size_t buf_sz;
    size_t buf_cap;

    uint64_t control_sum;
} QueryBuffer;

static int
queryBufferCtor(QueryBuffer* qbuf,
                size_t buf_cap,
                struct pollfd* read,
                struct pollfd* write)
{
    char* tmp = (char*) malloc(buf_cap * sizeof(char));
    if (!tmp)
        return error("cannot allocate memory: %s\n", strerror(errno));

    qbuf->buf = tmp;
    qbuf->buf_sz = 0;
    qbuf->buf_cap = buf_cap;
    qbuf->control_sum = 0;

    qbuf->read = read;
    qbuf->write = write;

    return 0;
}

static void
queryBufferDtor(QueryBuffer* qbuf)
{
    free(qbuf->buf);
    memset(qbuf, 0, sizeof(QueryBuffer));
}

static void
queryBufferSetPoll(QueryBuffer* qbuf)
{
    if (qbuf->buf_sz == 0)
    {
        $DBG("POLLIN %d", qbuf->read->fd);
        qbuf->read->events = POLLIN;
        qbuf->write->events = 0;
    }
    else
    {
        $DBG("POLLOUT %d", qbuf->write->fd);
        qbuf->write->events = POLLOUT;
        qbuf->read->events = 0;
    }
}

static int 
queryBufferAction(QueryBuffer* qbuf)
{
    $DBG("entered");
    if ((qbuf->read->revents & POLLIN) != 0)
    {
        $DBG("read %d", qbuf->read->fd);

        assert (qbuf->buf_sz == 0);
        ssize_t n_read = read(qbuf->read->fd, qbuf->buf, qbuf->buf_cap - 1);
        if (n_read < 0)
            return error("read failed: %s\n", strerror(errno));

        if (n_read == 0)
        {
            $DBG("Closing r%d w%d on EOF", qbuf->read->fd, qbuf->write->fd);
            close(qbuf->read->fd);
            close(qbuf->write->fd);
            qbuf->read->fd = -1;
            qbuf->write->fd = -1;
            
            int status = 0;
            wait(&status);
            $DBG("proc returned %d", status);
            return 0;
        }

        qbuf->buf[n_read] = '\0';
        qbuf->buf_sz = (size_t) n_read;
        
        qbuf->control_sum += (uint64_t) n_read;

        $DBG("<%s>", qbuf->buf);
    }
    else if((qbuf->write->revents & POLLOUT) != 0)
    {
        $DBG("write %d", qbuf->write->fd);
        $DBG("<%s>", qbuf->buf);

        ssize_t n_written = write(qbuf->write->fd, qbuf->buf, qbuf->buf_sz);
        if (n_written < 0)
            return error("write failed: %s\n", strerror(errno));

        qbuf->buf_sz -= (size_t) n_written;
        if (qbuf->buf_sz == 0)
            return 0;

        memmove(qbuf->buf, qbuf->buf + n_written, qbuf->buf_sz);
    }
    
    if ((qbuf->read->revents & POLLHUP) != 0)
    {
        $DBG("pipe closed on write end %d, also closing %d", qbuf->read->fd, qbuf->write->fd);
        close(qbuf->read->fd);
        close(qbuf->write->fd);
        qbuf->read->fd = -1;
        qbuf->write->fd = -1;
    }

    $DBG("leaving");
    return 0;
}

static int
dispatcher(int (*pipes)[2], const Args* args)
{
    $DBG("entered");

    size_t n_bufs = (size_t) args->n_procs + 1;
    size_t n_procs = (size_t) args->n_procs;
    QueryBuffer* qbufs = (QueryBuffer*) malloc(n_bufs * sizeof(QueryBuffer));
    if (!qbufs)
        return error("cannot allocate memory: %s\n", strerror(errno));

    struct pollfd* fds = (struct pollfd*) malloc(2 * n_bufs * sizeof(struct pollfd));
    if (!fds)
        return error("cannot allocate memory: %s\n", strerror(errno));

    struct pollfd* read_fds = fds;
    struct pollfd* write_fds = fds + n_bufs;

    for (size_t i = 0; i < n_bufs; i++)
    {
        if (i == 0)
            read_fds[i].fd = STDIN_FILENO;
        else
            read_fds[i].fd = pipes[2 * i - 1][0];

        if (i == n_procs)
            write_fds[i].fd = STDOUT_FILENO;
        else
            write_fds[i].fd = pipes[2 * i][1];
    
        queryBufferCtor(&qbufs[i], BUFFER_CAP, &read_fds[i], &write_fds[i]);
        $DBG("r%d w%d", read_fds[i].fd, write_fds[i].fd);
    }

    int n_connected = 0;
    while (1)
    {
        n_connected = 0;
        /* check how many connections left */
        for (size_t i = 0; i < n_bufs * 2; i++)
        {
            $DBG("%d", fds[i].fd);
            n_connected += (fds[i].fd != -1);
        }

        if (!n_connected)
            break;
        
        $DBG("%d", n_connected);
        for (size_t i = 0; i < n_bufs; i++)
            queryBufferSetPoll(&qbufs[i]);

        //sleep(1);
        poll(fds, n_bufs * 2, -1);

        for (size_t i = 0; i < n_bufs; i++)
            queryBufferAction(&qbufs[i]);
    }

    int retval = 0;

    uint64_t control_sum = qbufs[0].control_sum;
    for (size_t i = 0; i < n_bufs; i++)
    {
        if (control_sum != qbufs[i].control_sum)
            retval = fprintf(stderr, "Control sum between %zu and %zu do not match\n", i - 1, i);
 
        control_sum = qbufs[i].control_sum;

        queryBufferDtor(&qbufs[i]);
    }

    if (retval == 0)
        fprintf(stderr, "All control sums match\n");

    free(fds);
    free(qbufs);

    $DBG("leaving");
    return retval;
}

int
main(int argc, char* argv[])
{
    $DBG("entered");
    PROGNAME = argv[0];
    
    Args args = {0};
    if (parseArgs(argc, argv, &args) != 0)
        return 1;
    
    int retval = 0;

    int (*pipes)[2] = NULL;
    size_t n_procs = (size_t) args.n_procs;

    $DBG("initializing pipes");
    retval = initPipes(&pipes, 2 * n_procs);
    if (retval)
        return retval;

    $DBG("starting procs");
    retval = startProcs(pipes, &args);
    if (retval)
    {
        closePipes(pipes, 2 * n_procs);
        return retval;
    }

    $DBG("calling dispatcher");
    retval = dispatcher(pipes, &args);

    closePipes(pipes, 2 * n_procs);

    $DBG("leaving");
    return retval;
}
