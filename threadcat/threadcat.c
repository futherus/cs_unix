#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

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

typedef struct
{
    size_t buffer_cap;
    size_t n_buffer;

    char*   buffers_data;
    size_t* buffers_sz;

    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;

    size_t head;
    size_t tail;
    size_t size;
} CircBuffer;

static int
circBufferCtor(CircBuffer* cbuf, size_t buffer_cap, size_t n_buffer)
{
    assert(buffer_cap > 0 && n_buffer > 0 && "circular buffer with 0 size");

    char* tmp_data = (char*) malloc(buffer_cap * n_buffer * sizeof(char));
    if (tmp_data == NULL)
        return -1;

    size_t* tmp_sz = (size_t*) malloc(n_buffer * sizeof(size_t));
    if (tmp_sz == NULL)
        return -1;

    cbuf->buffer_cap = buffer_cap;
    cbuf->n_buffer = n_buffer;

    cbuf->buffers_data = tmp_data;
    cbuf->buffers_sz = tmp_sz;

    pthread_cond_init(&cbuf->not_full,  NULL);
    pthread_cond_init(&cbuf->not_empty, NULL);
    pthread_mutex_init(&cbuf->mutex, NULL);

    cbuf->head = 0;
    cbuf->tail = 0;
    cbuf->size = 0;

    return 0;
}

static int
circBufferDtor(CircBuffer* cbuf)
{
    /* dtor or wait for all releases? */

    pthread_mutex_lock(&cbuf->mutex);
    pthread_mutex_destroy(&cbuf->mutex);

    free(cbuf->buffers_data);

    return 0;
}

static size_t
circBufferGetCap(CircBuffer* cbuf)
{
    assert(cbuf->buffer_cap);

    return cbuf->buffer_cap;
}

static int
circBufferAcquireEmpty(CircBuffer* cbuf, char** dest)
{
    pthread_mutex_lock(&cbuf->mutex);
    $DBG("Entered");

    if (cbuf->size == cbuf->n_buffer)
        pthread_cond_wait(&cbuf->not_full, &cbuf->mutex); /* error handle */

    *dest = cbuf->buffers_data + cbuf->tail * cbuf->buffer_cap;

    $DBG("Leaving");
    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

static int
circBufferReleaseEmpty(CircBuffer* cbuf, size_t buf_sz)
{
    pthread_mutex_lock(&cbuf->mutex);
    $DBG("Entered");

    $DBG("got lock, size = %zu", cbuf->size);
    assert(cbuf->size < cbuf->n_buffer && "write release with full circbuffer");

    cbuf->buffers_sz[cbuf->tail] = buf_sz;
    cbuf->tail = (cbuf->tail + 1) % cbuf->n_buffer;
    cbuf->size++;

    if (cbuf->size == 1)
    {
        $DBG("signal non_empty");
        pthread_cond_signal(&cbuf->not_empty);
    }

    $DBG("Leaving");
    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

static int
circBufferAcquireFull(CircBuffer* cbuf, char** dest, size_t* buf_sz)
{
    pthread_mutex_lock(&cbuf->mutex);
    $DBG("Entered");

    $DBG("got lock, size = %zu", cbuf->size);
    if (cbuf->size == 0)
    {
        $DBG("waiting for not_empty");
        $DBG("cbuf=%p, &cbuf=%p", cbuf, &cbuf);
        pthread_cond_wait(&cbuf->not_empty, &cbuf->mutex);
    }

    $DBG("writing return arguments");
    *dest = cbuf->buffers_data + cbuf->buffer_cap * cbuf->head;
    *buf_sz = cbuf->buffers_sz[cbuf->head];

    $DBG("Leaving");
    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

static int
circBufferReleaseFull(CircBuffer* cbuf)
{
    pthread_mutex_lock(&cbuf->mutex);
    $DBG("Entered");

    assert(cbuf->size > 0 && "read release with zero size");

    cbuf->head = (cbuf->head + 1) % cbuf->n_buffer;
    cbuf->size--;

    if (cbuf->size == cbuf->n_buffer - 1)
        pthread_cond_signal(&cbuf->not_full);

    $DBG("Leaving");
    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

/******************************************************************************/

static int
readToCbuf(CircBuffer* cbuf, int fd)
{
    $DBG("Entered");
    char* buf = NULL;
    ssize_t n_read = 0;
    size_t buf_cap = circBufferGetCap(cbuf);

    int saved_errno = 0;

    do
    {
        $DBG("Acquiring empty");
        circBufferAcquireEmpty(cbuf, &buf);

        n_read = read(fd, buf, buf_cap - 1);
        if (n_read > 0)
            buf[n_read] = '\0';
        else
            saved_errno = errno;

        $DBG("Releasing empty");
        circBufferReleaseEmpty(cbuf, (size_t) n_read);
    }
    while (n_read > 0);

    if (n_read < 0)
        return error("read failed: %s\n", strerror(saved_errno));
   
    $DBG("Leaving");
    return 0;
}

static int
writeFromCbuf(int fd, CircBuffer* cbuf)
{
    $DBG("Entered");
    char* buf = NULL;
    size_t buf_sz = 0;

    /* should be cancelled */
    while (1)
    {
        circBufferAcquireFull(cbuf, &buf, &buf_sz);
#ifndef DEBUG
        write(fd, buf, buf_sz);
#endif
        circBufferReleaseFull(cbuf);
    }

    assert(0 && "unreachable");
    return -1;
}

static int
catFiles(CircBuffer* cbuf, const Args* args)
{
    char* filename = NULL;
    int fd = 0;
 
    for (int file_num = 0; file_num < args->n_files; file_num++)
    {
        filename = args->files_arr[file_num];

        $DBG("cat files: opening %s", filename);
        fd = open(filename, O_RDONLY);
        if (fd == -1)
            return error("cannot open %s: %s\n", filename, strerror(errno));

        readToCbuf(cbuf, fd);

        $DBG("cat files: closing %s", filename);
        close(fd);
        fd = 0;
    }

    return 0;
}

static int
catInteractive(CircBuffer* cbuf)
{
    readToCbuf(cbuf, STDIN_FILENO);

    return 0;
}

/*****************************************************************************/

typedef struct
{
    CircBuffer* cbuf;
    const Args* args;
} ReaderArgs;

static void*
readerStart(void* arg_ptr)
{
    $DBG("reader started, stack: %p", &arg_ptr);
    ReaderArgs* ptr = (ReaderArgs*) arg_ptr;

    $DBG("n_files = %d", ptr->args->n_files);
    for (int i = 0; i < ptr->args->n_files; i++)
        $DBG("\t%s", ptr->args->files_arr[i]);

    if (ptr->args->n_files == 0)
        catInteractive(ptr->cbuf);
    else
        catFiles(ptr->cbuf, ptr->args);

    $DBG("reader returning");
    return NULL;
}

typedef struct
{
    int fd;
    CircBuffer* cbuf;
} WriterArgs;

static void*
writerStart(void* arg_ptr)
{
    $DBG("writer started, stack: %p", &arg_ptr);
    WriterArgs* ptr = (WriterArgs*) arg_ptr;
    writeFromCbuf(ptr->fd, ptr->cbuf);

    $DBG("writer returning");
    return NULL;
}

/*****************************************************************************/

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    Args args = {};
    if (parseArgs(argc, argv, &args) != 0)
        return 1;

    int retval = 0;

    CircBuffer cbuf = {0};
    circBufferCtor(&cbuf, 256, 16);

    $DBG("starting reader");
    pthread_t reader_tid = 0;
    ReaderArgs reader_args = {.args = &args, .cbuf = &cbuf};
    pthread_create(&reader_tid, NULL, readerStart, &reader_args); 

    $DBG("starting writer");
    pthread_t writer_tid = 0;
    WriterArgs writer_args = {.fd = STDOUT_FILENO, .cbuf = &cbuf};
    pthread_create(&writer_tid, NULL, writerStart, &writer_args); 

    $DBG("joining reader");
    void* thread_retval = NULL;
    retval = pthread_join(reader_tid, &thread_retval);

    $DBG("joining writer");
    retval = pthread_cancel(writer_tid);
    if (retval != 0)
        error("writer cancel failed: %s\n", strerror(retval));
    pthread_join(writer_tid, &thread_retval);

    $DBG("main returning");
    circBufferDtor(&cbuf);

    return retval;
}
