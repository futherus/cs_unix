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
#include <time.h>

#ifdef DEBUG
    #define $DBG(FMT, ...) fprintf(stderr, "%s: " FMT "\n", __PRETTY_FUNCTION__, ##__VA_ARGS__)
#else
    #define $DBG(FMT, ...)
#endif

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

typedef struct
{
    int* data;
    size_t size;
    size_t offset;
} Buffer;

static void
printBuffer(Buffer* buf)
{
    fprintf(stderr, "size: %zu\n", buf->size);
    fprintf(stderr, "offset: %zu\n", buf->offset);
    for (size_t i = 0; i < buf->size; i++)
    {
        if (i == buf->offset)
            fprintf(stderr, ">%d", buf->data[i]);
        else
            fprintf(stderr, " %d", buf->data[i]);
    }
 
    fprintf(stderr, "\n");
}

static int
compareInts(const void* a, const void* b)
{
    int arg1 = *(const int*)a;
    int arg2 = *(const int*)b;

    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

static int
sorter(Buffer* buf)
{
    $DBG("qsort");
   
    qsort(buf->data, buf->size, sizeof(int), compareInts);
    
    return 0;
}

static void*
sorterStart(void* arg_ptr)
{
    $DBG("sorter started, stack: %p", &arg_ptr);
    Buffer* ptr = (Buffer*) arg_ptr;

    sorter(ptr);

    $DBG("sorter returning");
    return NULL;
}

static int
merger(Buffer* out, Buffer* in_arr, size_t in_arr_sz)
{
    while (out->offset < out->size)
    {
        Buffer* min = in_arr;

        for (Buffer* cur = in_arr + 1; cur < in_arr + in_arr_sz; cur++)
        {
            if (min->offset == min->size)
            {
                min = cur;
                continue;
            }

            if (cur->offset < cur->size &&
                cur->data[cur->offset] < min->data[min->offset])
            {
                min = cur;
            }
        }

        out->data[out->offset] = min->data[min->offset];

        out->offset++;
        min->offset++;
    }

    return 0;
}

static int
checker(Buffer* raw_buf, Buffer* sorted_buf)
{
    assert(raw_buf->size == sorted_buf->size);

    qsort(raw_buf->data, raw_buf->size, sizeof(int), compareInts);

    for (size_t i = 0; i < sorted_buf->size; i++)
    {
        if (raw_buf->data[i] != sorted_buf->data[i])
        {
            return error("%zu element are different: OK: %d, FAIL: %d",
                         i, raw_buf->data[i], sorted_buf->data[i]);
        }
    }

    return 0;
}

static void
genData(Buffer* buf)
{
    srand((unsigned int) time(NULL));

    for (size_t i = 0; i < buf->size; i++)
        buf->data[i] = rand() % (int) buf->size;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    if (argc != 3)
        return error("Wrong amount of parameters\n");

    size_t data_sz = (size_t) atoi(argv[1]);
    size_t n_threads = (size_t) atoi(argv[2]);

    /* generate data */
    Buffer init_data = {
        .data = (int*) malloc(data_sz * sizeof(int)),
        .size = data_sz
    };
    if (!init_data.data)
        return error("cannot allocate memory");

    genData(&init_data);
    $DBG("initial data");
    printBuffer(&init_data);

    /* create sorters */
    pthread_t* sorter_tids = (pthread_t*) malloc(n_threads * sizeof(pthread_t));
    Buffer* sorter_bufs = (Buffer*) malloc(n_threads * sizeof(Buffer));
    if (!sorter_bufs)
        return error("cannot allocate memory");

    size_t sorter_sz = data_sz / n_threads;
    size_t tail_sz = data_sz % n_threads;

    $DBG("starting sorters");
    int* sorter_ptr = init_data.data;
    for (size_t i = 0; i < n_threads; i++)
    {
        size_t tmp_sz = sorter_sz;
        if (i < tail_sz)
            tmp_sz++;
 
        sorter_bufs[i].data = (int*) malloc(tmp_sz * sizeof(int));
        sorter_bufs[i].size = tmp_sz;
        sorter_bufs[i].offset = 0;
        memcpy(sorter_bufs[i].data, sorter_ptr, tmp_sz * sizeof(int));
        sorter_ptr += tmp_sz;

        pthread_create(&sorter_tids[i], NULL, sorterStart, &sorter_bufs[i]); 
    }

    $DBG("joining sorters");
    for (size_t i = 0; i < n_threads; i++)
    {
        void* thread_retval = NULL;
        pthread_join(sorter_tids[i], &thread_retval);
    }

/*
    for (size_t i = 0; i < n_threads; i++)
        printBuffer(&sorter_bufs[i]);
*/

    /* merge sorters' data */
    Buffer merged_data = {
        .data = (int*) malloc(data_sz * sizeof(int)),
        .size = data_sz
    };
    if (!merged_data.data)
        return error("cannot allocate memory");

    $DBG("merger");
    merger(&merged_data, sorter_bufs, n_threads);
    printBuffer(&merged_data);

    /* check data */
    $DBG("checker");
    checker(&init_data, &merged_data);

    /* cleanup */
    free(init_data.data);
    free(merged_data.data);
    for (size_t i = 0; i < n_threads; i++)
        free(sorter_bufs[i].data);

    free(sorter_bufs);
    free(sorter_tids);

    $DBG("main returning");

    return 0;
}
