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

typedef struct
{
    int    n_files;
    char** files_arr;
} Args;

const char* PROGNAME = NULL;

static int
error(char* fmt, ...)
{
//    fprintf(stderr, "%s: ", PROGNAME);

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

int
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

int
circBufferDtor(CircBuffer* cbuf)
{
    /* dtor or wait for all releases? */

    pthread_mutex_lock(&cbuf->mutex);
    pthread_mutex_destroy(&cbuf->mutex);

    free(cbuf->buffers_data);

    return 0;
}

size_t
circBufferGetCap(CircBuffer* cbuf)
{
    assert(cbuf->buffer_cap);

    return cbuf->buffer_cap;
}

int
circBufferAcquireEmpty(CircBuffer* cbuf, char** dest)
{
    pthread_mutex_lock(&cbuf->mutex);

    /* we have to re-check after cond_wait, because other thread on this
     * condition can acquire lock before us */
    while (cbuf->size == cbuf->n_buffer)
        pthread_cond_wait(&cbuf->not_full, &cbuf->mutex); /* error handle */

    *dest = cbuf->buffers_data + cbuf->tail * cbuf->buffer_cap;

    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

int
circBufferReleaseEmpty(CircBuffer* cbuf, size_t buf_sz)
{
    pthread_mutex_lock(&cbuf->mutex);

    assert(cbuf->size < cbuf->n_buffer && "write release with full circbuffer");

    cbuf->buffers_sz[cbuf->tail] = buf_sz;
    cbuf->tail = (cbuf->tail + 1) % cbuf->n_buffer;
    cbuf->size++;

    if (cbuf->size == 1)
    {
        error("%s: signal non_empty\n", __PRETTY_FUNCTION__);
        pthread_cond_signal(&cbuf->not_empty);
    }

    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

int
circBufferAcquireFull(CircBuffer* cbuf, char** dest, size_t* buf_sz)
{
    error("%s: entered\n", __PRETTY_FUNCTION__);
    pthread_mutex_lock(&cbuf->mutex);

    error("%s: got lock, size = %zu\n", __PRETTY_FUNCTION__, cbuf->size);
    /* we have to re-check after cond_wait, because other thread on this
     * condition can acquire lock before us */
    while (cbuf->size == 0)
    {
        error("%s: waiting for not_empty\n" "cbuf=%p, &cbuf=%p\n", __PRETTY_FUNCTION__, cbuf, &cbuf);
        pthread_cond_wait(&cbuf->not_empty, &cbuf->mutex);
    }

    error("%s: writing return arguments\n", __PRETTY_FUNCTION__);
    *dest = cbuf->buffers_data + cbuf->buffer_cap * cbuf->head;
    *buf_sz = cbuf->buffers_sz[cbuf->head];

    pthread_mutex_unlock(&cbuf->mutex);

    error("%s: unlocked, returning\n", __PRETTY_FUNCTION__);
    return 0;
}

int
circBufferReleaseFull(CircBuffer* cbuf)
{
    pthread_mutex_lock(&cbuf->mutex);

    assert(cbuf->size > 0 && "read release with zero size");

    cbuf->head = (cbuf->head + 1) % cbuf->n_buffer;
    cbuf->size--;

    if (cbuf->size == cbuf->n_buffer - 1)
        pthread_cond_signal(&cbuf->not_full);

    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

/******************************************************************************/

static int
readToCbuf(CircBuffer* cbuf, int fd)
{
    char* buf = NULL;
    ssize_t n_read = 0;
    size_t buf_cap = circBufferGetCap(cbuf);

    int saved_errno = 0;

    do
    {
        error("Acquiring empty\n");
        circBufferAcquireEmpty(cbuf, &buf);

        n_read = read(fd, buf, buf_cap - 1);
        if (n_read > 0)
            buf[n_read] = '\0';
        else
            saved_errno = errno;

        error("Releasing empty\n");
        circBufferReleaseEmpty(cbuf, (size_t) n_read);
    }
    while (n_read > 0);

    if (n_read < 0)
        return error("read failed: %s\n", strerror(saved_errno));
   
    return 0;
}

static int
catFiles(CircBuffer* cbuf, const Args* args)
{
    char* filename = NULL;
    int fd = 0;
 
    for (int file_num = 0; file_num < args->n_files; file_num++)
    {
        filename = args->files_arr[file_num];

        error("cat files: opening %s\n", filename);
        fd = open(filename, O_RDONLY);
        if (fd == -1)
        {
            return error("cannot open %s: %s\n", filename, strerror(errno));
        }

        readToCbuf(cbuf, fd);

        error("cat files: closing %s\n", filename);
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
    error("reader started, stack: %p\n", &arg_ptr);
    ReaderArgs* ptr = (ReaderArgs*) arg_ptr;

    error("n_files = %d\n", ptr->args->n_files);
    for (int i = 0; i < ptr->args->n_files; i++)
        error("\t%s\n", ptr->args->files_arr[i]);

    if (ptr->args->n_files == 0)
        catInteractive(ptr->cbuf);
    else
        catFiles(ptr->cbuf, ptr->args);

    error("reader returning\n");
    return NULL;
}

typedef struct
{
    int fd;
    CircBuffer* cbuf;
} WriterArgs;

static int
writeFromCbuf(int fd, CircBuffer* cbuf)
{
    char* buf = NULL;
    size_t buf_sz = 0;

    /* should be cancelled */
    while (1)
    {
        circBufferAcquireFull(cbuf, &buf, &buf_sz);
        write(fd, buf, buf_sz);
        circBufferReleaseFull(cbuf);
    }

    /*
    assert(0 && "unreachable");
    */
    return -1;
}

/*
static int
dummy(int fd, CircBuffer* cbuf)
{
    error("%s: entered\n", __PRETTY_FUNCTION__);

    char* buf = NULL;
    size_t buf_sz = 0;
    while(1)
    {

    }

    return -1;
}
*/

static void*
writerStart(void* arg_ptr)
{
    error("writer started\n" "stack: %p\n", &arg_ptr);
    WriterArgs* ptr = (WriterArgs*) arg_ptr;
    writeFromCbuf(ptr->fd, ptr->cbuf);
/*
    while(1)
        pthread_cond_wait(&ptr->cbuf->not_full, &ptr->cbuf->mutex);
    dummy(ptr->fd, ptr->cbuf);
*/
    error("writer returning\n");
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

    error("starting reader\n");
    pthread_t reader_tid = 0;
    ReaderArgs reader_args = {.args = &args, .cbuf = &cbuf};
    pthread_create(&reader_tid, NULL, readerStart, &reader_args); 

    error("starting writer\n");
    pthread_t writer_tid = 0;
    WriterArgs writer_args = {.fd = STDOUT_FILENO, .cbuf = &cbuf};
    pthread_create(&writer_tid, NULL, writerStart, &writer_args); 

    error("joining reader\n");
    void* thread_retval = NULL;
    retval = pthread_join(reader_tid, &thread_retval);

    error("joining writer\n");
    retval = pthread_cancel(writer_tid);
    if (retval != 0)
        error("writer cancel failed: %s\n", strerror(retval));
    pthread_join(writer_tid, &thread_retval);

    error("main returning\n");
    /* circBufferDtor() */

    return retval;
}
