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
} circbuffer_t;

int
circbuffer_ctor(circbuffer_t* cbuf, size_t buffer_cap, size_t n_buffer)
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
circbuffer_dtor(circbuffer_t* cbuf)
{
    /* dtor or wait for all releases? */

    pthread_mutex_lock(&cbuf->mutex);
    pthread_mutex_destroy(&cbuf->mutex);

    free(cbuf->buffers_data);

    return 0;
}

size_t
circbuffer_get_cap(circbuffer_t* cbuf)
{
    assert(cbuf->buffer_cap);

    return cbuf->buffer_cap;
}

int
circbuffer_get_write(circbuffer_t* cbuf, char** dest)
{
    pthread_mutex_lock(&cbuf->mutex);

    if (cbuf->size == cbuf->n_buffer)
        pthread_cond_wait(&cbuf->not_full, &cbuf->mutex); /* error handle */

    *dest = cbuf->buffers_data + cbuf->tail * cbuf->buffer_cap;

    pthread_mutex_unlock(&cbuf->mutex);

    return 0;
}

int
circbuffer_release_write(circbuffer_t* cbuf, size_t buf_sz)
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
circbuffer_get_read(circbuffer_t* cbuf, char** dest, size_t* buf_sz)
{
    error("%s: entered\n", __PRETTY_FUNCTION__);
    pthread_mutex_lock(&cbuf->mutex);

    error("%s: got lock, size = %zu\n", __PRETTY_FUNCTION__, cbuf->size);
    if (cbuf->size == 0)
    {
        error("%s: waiting for not_empty\n" "cbuf=%p, &cbuf=%p", __PRETTY_FUNCTION__,
              cbuf, &cbuf);
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
circbuffer_release_read(circbuffer_t* cbuf)
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
read_to_cbuf(circbuffer_t* cbuf, int fd)
{
    char* buf = NULL;
    ssize_t n_read = 0;
    size_t buf_cap = circbuffer_get_cap(cbuf);

    int saved_errno = 0;

    do
    {
        circbuffer_get_write(cbuf, &buf);

        n_read = read(fd, buf, buf_cap - 1);
        if (n_read > 0)
            buf[n_read] = '\0';
        else
            saved_errno = errno;

        circbuffer_release_write(cbuf, (size_t) n_read);
    }
    while (n_read > 0);

    if (n_read < 0)
        return error("read failed: %s\n", strerror(saved_errno));
   
    return 0;
}

static int
cat_files(circbuffer_t* cbuf, const args_t* args)
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

        read_to_cbuf(cbuf, fd);

        error("cat files: closing %s\n", filename);
        close(fd);
        fd = 0;
    }

    return 0;
}

static int
cat_interactive(circbuffer_t* cbuf)
{
    read_to_cbuf(cbuf, STDIN_FILENO);

    return 0;
}

/*****************************************************************************/

typedef struct
{
    circbuffer_t* cbuf;
    const args_t* args;
} reader_args_t;

static void*
reader_start(void* arg_ptr)
{
    error("reader started\n" "stack: %p\n", &arg_ptr);
    reader_args_t* ptr = (reader_args_t*) arg_ptr;

    if (ptr->args->n_files == 0)
        cat_interactive(ptr->cbuf);
    else
        cat_files(ptr->cbuf, ptr->args);

    error("reader returning\n");
    return NULL;
}

typedef struct
{
    int fd;
    circbuffer_t* cbuf;
} writer_args_t;

static int
write_from_cbuf(int fd, circbuffer_t* cbuf)
{
    char* buf = NULL;
    size_t buf_sz = 0;

    /* should be cancelled */
    while (1)
    {
        circbuffer_get_read(cbuf, &buf, &buf_sz);
        write(fd, buf, buf_sz);
        circbuffer_release_read(cbuf);
    }

    /*
    assert(0 && "unreachable");
    */
    return -1;
}

static int
dummy(int fd, circbuffer_t* cbuf)
{
    error("%s: entered\n", __PRETTY_FUNCTION__);

    char* buf = NULL;
    size_t buf_sz = 0;
    while(1)
    {

    }

    return -1;
}

static void*
writer_start(void* arg_ptr)
{
    error("writer started\n" "stack: %p\n", &arg_ptr);
    writer_args_t* ptr = (writer_args_t*) arg_ptr;
/*
    write_from_cbuf(ptr->fd, ptr->cbuf);
    while(1)
        pthread_cond_wait(&ptr->cbuf->not_full, &ptr->cbuf->mutex);
*/
    dummy(ptr->fd, ptr->cbuf);
    error("writer returning\n");
    return NULL;
}

/*****************************************************************************/

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    args_t args = {};
    if (parse_args(argc, argv, &args) != 0)
        return 1;

    int retval = 0;

    circbuffer_t cbuf = {0};
    circbuffer_ctor(&cbuf, 256, 16);

    error("starting reader\n");
    pthread_t reader_tid = 0;
    reader_args_t reader_args = {.args = &args, .cbuf = &cbuf};
    pthread_create(&reader_tid, NULL, reader_start, &reader_args); 

    error("starting writer\n");
    pthread_t writer_tid = 0;
    writer_args_t writer_args = {.fd = STDOUT_FILENO, .cbuf = &cbuf};
    pthread_create(&writer_tid, NULL, writer_start, &writer_args); 

    error("joining reader\n");
    void* thread_retval = NULL;
    retval = pthread_join(reader_tid, &thread_retval);

    error("joining writer\n");
    retval = pthread_cancel(writer_tid);
    if (retval != 0)
        error("writer cancel failed: %s\n", strerror(retval));
    pthread_join(writer_tid, &thread_retval);

    error("main returning\n");
    /* circbuffer_dtor() */

    return retval;
}
