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

#include "stack.h"

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

#define N_THREADS (4)
#define DATA_SZ (N_THREADS * 10)

typedef struct
{
    int* data;
    size_t size;
} Buffer;

static void
printData(Buffer* data_buf)
{
    for (size_t i = 0; i < data_buf->size; i++)
        printf("%d ", data_buf->data[i]);
    
    printf("\n");
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
print_indxes(size_t* indxes, size_t size)
{
    for (size_t i = 0; i < size; i++)
        fprintf(stderr, "%zu ", indxes[i]);

    fprintf(stderr, "\n");
}

static int
merger(Buffer* out, Buffer* in_arr, size_t in_arr_sz)
{
    size_t out_indx = 0;
    size_t* in_indxes = (size_t*) calloc(in_arr_sz, sizeof(size_t));
    if (!in_indxes)
        return error("cannot allocate memory");

    while (out_indx < out->size)
    {
        size_t min_indx = 0;

        for (size_t i = 1; i < in_arr_sz; i++)
        {
            if (in_indxes[i] == in_arr[i].size)
                continue;

            if (in_indxes[min_indx] == in_arr[min_indx].size)
            {
                min_indx = i;
                continue;
            }

            if (in_arr[i].data[in_indxes[i]] < in_arr[min_indx].data[in_indxes[min_indx]])
                min_indx = i;
        }
        if (in_arr[min_indx].size == in_indxes[min_indx])
            $DBG("fail");

//        print_indxes(in_indxes, in_arr_sz);
        out->data[out_indx] = in_arr[min_indx].data[in_indxes[min_indx]];
        $DBG("%zu: val = %d", min_indx, out->data[out_indx]);
        in_indxes[min_indx]++;
        out_indx++;

//        printData(out);
        $DBG("size: %zu", out_indx);
    }

    free(in_indxes);
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
genData(Buffer* data_buf)
{
    srand(time(NULL));

    for (size_t i = 0; i < data_buf->size; i++)
        data_buf->data[i] = rand() % DATA_SZ;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    Buffer init_data = {
        .data = (int*) malloc(DATA_SZ * sizeof(int)),
        .size = DATA_SZ
    };
    if (!init_data.data)
        return error("cannot allocate memory");

    Buffer merged_data = {
        .data = (int*) malloc(DATA_SZ * sizeof(int)),
        .size = DATA_SZ
    };
    if (!merged_data.data)
        return error("cannot allocate memory");

    genData(&init_data);
    $DBG("initial data");
    printData(&init_data);

    pthread_t sorter_tids[N_THREADS] = {0};
    Buffer* sorter_bufs = (Buffer*) malloc(N_THREADS * sizeof(Buffer));
    if (!sorter_bufs)
        return error("cannot allocate memory");

    $DBG("starting sorters");
    int* sorter_ptr = init_data.data;
    size_t tail_sz = DATA_SZ;
    for (size_t i = 0; i < N_THREADS; i++)
    {
        size_t sorter_sz = DATA_SZ / N_THREADS;
        if (sorter_sz + sorter_ptr

        sorter_bufs[i].data = (int*) malloc(sorter_sz * sizeof(int));
        sorter_bufs[i].size = sorter_sz;
        memcpy(sorter_bufs[i].data, sorter_ptr, sorter_sz * sizeof(int));
        sorter_ptr += sorter_sz;

        $DBG("before pthread");

        pthread_create(&sorter_tids[i], NULL, sorterStart, &sorter_bufs[i]); 
    }

    $DBG("joining sorters");
    for (size_t i = 0; i < N_THREADS; i++)
    {
        void* thread_retval = NULL;
        pthread_join(sorter_tids[i], &thread_retval);
    }

    $DBG("merger");
    merger(&merged_data, sorter_bufs, N_THREADS);
    printData(&merged_data);

    $DBG("checker");
    checker(&init_data, &merged_data);

    free(init_data.data);
    free(merged_data.data);
    for (size_t i = 0; i < N_THREADS; i++)
        free(sorter_bufs[i].data);

    free(sorter_bufs);

    $DBG("main returning");

    return 0;
}
