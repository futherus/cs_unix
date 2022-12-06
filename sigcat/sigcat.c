#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>

#ifdef DEBUG
    #define $DBG(FMT, ...) fprintf(stderr, "%s: " FMT "\n", __PRETTY_FUNCTION__, ##__VA_ARGS__)
#else
    #define $DBG(FMT, ...)
#endif

typedef struct
{
    int    n_files;
    char** files_arr;
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
    if (getopt(argc, argv, "") != -1)
        return 1;
    
    args->n_files   = argc - optind;
    args->files_arr = argv + optind;

    return 0;
}

#define BUFFER_CAP 0x100

static volatile sig_atomic_t WRITER_PID;

//static volatile sig_atomic_t READER_CALLS_COUNT;
static volatile sig_atomic_t READER_BUFFER[BUFFER_CAP];
static volatile sig_atomic_t READER_BITS_READ;
static volatile sig_atomic_t READER_BITS_POS;

static void
sighandlerReader(int signo)
{
    $DBG("entered");
    /* restore handler in case of oneshot handler */
    signal(SIGUSR1, sighandlerReader);
    
    /* all bits are sent, handler is set, returning */
    if (READER_BITS_POS == READER_BITS_READ)
    {
        /* allow reader to proceed */
        READER_BITS_POS = -1;

        $DBG("last leaving %d", READER_CALLS_COUNT--);
        return;
    }

    volatile char* buf = (volatile char*) READER_BUFFER;
    char bit = buf[READER_BITS_POS / CHAR_BIT] & (1 << READER_BITS_POS % CHAR_BIT);
    READER_BITS_POS++;

    $DBG("bit = %d", bit);
 
    int sig = bit ? SIGUSR2 : SIGUSR1;

    /* until this point another SIGUSRX won't appear */
    $DBG("signal to writer %d", WRITER_PID);
    kill(WRITER_PID, sig);

    $DBG("leaving");

    return;
}

static int
reader(int fd)
{
    $DBG("entered");
    ssize_t n_read = 0;

    volatile char* buf = (volatile char*) READER_BUFFER;
    while ((n_read = read(fd, (void*) buf, BUFFER_CAP - 1)) > 0)
    {
        buf[n_read] = '\0';

        READER_BITS_READ = (sig_atomic_t) n_read * CHAR_BIT;
        READER_BITS_POS = 0;

        $DBG("n_read = %zd (* 8)", n_read);
//        sleep(3);

        kill(getpid(), SIGUSR1);

        $DBG("fallthrough");
        /* waiting for all bits to transfer */
        while (READER_BITS_POS != -1)
            ;

    }

    if (n_read < 0)
        return error("read failed: %s\n", strerror(errno));
   
    $DBG("leaving");
    return 0;
}

static int
catFiles(const Args* args)
{
    $DBG("entered");
    char* filename = NULL;
    int fd = -1;

    for (int file_num = 0; file_num < args->n_files; file_num++)
    {
        filename = args->files_arr[file_num];

        $DBG("opening %s", filename);
        fd = open(filename, O_RDONLY);
        if (fd == -1)
            return error("cannot open %s: %s\n", filename, strerror(errno));

        reader(fd);

        $DBG("closing %s", filename);
        close(fd);
        fd = -1;
    }

    $DBG("leaving");
    return 0;
}

static int
catInteractive()
{
    reader(STDIN_FILENO);

    return 0;
}

static volatile sig_atomic_t READER_PID;

static volatile sig_atomic_t WRITER_CALLS_COUNT;
static volatile sig_atomic_t WRITER_BUFFER[BUFFER_CAP];
static volatile sig_atomic_t WRITER_BITS_POS;
static volatile sig_atomic_t WRITER_FD;

static void
sighandlerWriter(int signo)
{
    $DBG("entered %d", ++WRITER_CALLS_COUNT);
    /* handle 0 */
    signal(SIGUSR1, sighandlerWriter);
    signal(SIGUSR2, sighandlerWriter);

    volatile char* buf = (volatile char*) WRITER_BUFFER;
    if (signo == SIGUSR1)
    {
        $DBG("bit = 0");
        buf[WRITER_BITS_POS / CHAR_BIT] &= ~(1 << WRITER_BITS_POS % CHAR_BIT); /* bit = 0 */
    }
    else
    {
        $DBG("bit = 1");
        buf[WRITER_BITS_POS / CHAR_BIT] |=  (1 << WRITER_BITS_POS % CHAR_BIT); /* bit = 1 */
    }

    WRITER_BITS_POS++;

    if (WRITER_BITS_POS == BUFFER_CAP * sizeof(sig_atomic_t) * CHAR_BIT)
    {
        write(WRITER_FD, (void*) buf, BUFFER_CAP * sizeof(sig_atomic_t));
        WRITER_BITS_POS = 0;
    }

    /* until this point another SIGUSRX won't appear */
    $DBG("signal to reader %d", READER_PID);
//    sleep(2);
    kill(READER_PID, SIGUSR1);
 
    $DBG("leaving %d", WRITER_CALLS_COUNT--);
    return;
}

static int
writer(int fd)
{
    $DBG("Entered");

    WRITER_FD = fd;

    /* should be terminated */
    while (1)
        ; /* waiting for signal */

    assert(0 && "unreachable");
    return -1;
}

static volatile sig_atomic_t READER_CAN_START = 0;

static void
sighandlerReaderStart(int signo)
{
    READER_CAN_START = 1;
    
    return;
}

static volatile sig_atomic_t READER_READY = 0;

static void
sighandlerReaderReady(int signo)
{
    READER_READY = 1;
    return;
}

int
main(int argc, char* argv[])
{
    $DBG("entered");
    PROGNAME = argv[0];

    Args args = {};
    if (parseArgs(argc, argv, &args) != 0)
        return 1;

    int retval = 0;
    int status = 0;

    /* start processes */
    $DBG("starting writer");
    pid_t writer_pid = fork();
    if (writer_pid == 0)
    {
        WRITER_PID = getpid();

        /* prepare to catch reader ready */
        signal(SIGUSR2, sighandlerReaderReady);
       
        $DBG("starting reader");
        pid_t reader_pid = fork();
        if (reader_pid == 0)
        {
            signal(SIGUSR1, sighandlerReader);
            signal(SIGUSR2, sighandlerReaderStart);

            /* reader is ready */
            $DBG("reader is ready");
            kill(WRITER_PID, SIGUSR2);

            /* waiting for signal allowing to proceed */
            while (!READER_CAN_START)
                ;

            signal(SIGUSR2, SIG_DFL);
            
            if (args.n_files == 0)
                catInteractive();
            else
                catFiles(&args);
       
            $DBG("reader returning");
            return 0;
        }

        $DBG("reader pid = %d", reader_pid);
        READER_PID = reader_pid; 

        /* waiting for reader ready */
        while (!READER_READY)
            ;

        /* set up writer */
        $DBG("setting up writer");
        signal(SIGUSR1, sighandlerWriter);
        signal(SIGUSR2, sighandlerWriter); 
        WRITER_FD = STDOUT_FILENO;

        /* allow reader to proceed */
        $DBG("allowing reader to proceed");
        kill(READER_PID, SIGUSR2);

        status = 0;
        waitpid(READER_PID, &status, 0);
        $DBG("writer returning");

        assert(WRITER_BITS_POS % 8 == 0);
        write(WRITER_FD, (void*) WRITER_BUFFER, WRITER_BITS_POS / 8);
        WRITER_BITS_POS = 0;
 
        return 0;
    }

    $DBG("writer pid = %d", writer_pid);

    /* waiting for writer to terminate */
    status = 0;
    waitpid(writer_pid, &status, 0);

    $DBG("main returning");

    return retval;
}
