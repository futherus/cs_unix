#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>

typedef struct
{
    int    n_files;
    char** files_arr;
} args_t;

const char* PROGNAME = NULL;

static int
stream_write(FILE* outstream, FILE* instream, char* tmpbuf, size_t size)
{
    if (fread(tmpbuf, sizeof(char), size, instream) != size)
        return 1;

    if (fwrite(tmpbuf, sizeof(char), size, outstream) != size)
        return 1;

    return 0;
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

static int
cat_interactive()
{
    int ret_val = EXIT_SUCCESS;
    char* buffer = malloc(BUFSIZ);

    if (!buffer)
    {        
        errno = ENOMEM;
        goto error;
    }

    while (1)
    {
        fgets(buffer, BUFSIZ, stdin);
        if (ferror(stdin))
            goto error;

        fputs(buffer, stdout);
        if (ferror(stdout))
            goto error;

        buffer[0] = '\0';

        if (feof(stdin))
            goto finally;
    }

error:
    ret_val = EXIT_FAILURE;
    fprintf(stderr, "%s: %s\n", PROGNAME, strerror(errno));

finally:
    free(buffer);

    return ret_val;
}

static int
cat_files(args_t args)
{
    char* buffer   = NULL;
    char* filename = NULL;
    FILE* stream   = NULL;

    int ret_val = EXIT_SUCCESS;
    
    for (int file_num = 0; file_num < args.n_files; file_num++)
    {
        filename = args.files_arr[file_num];

        struct stat statbuf = {};
        if (stat(filename, &statbuf) == -1)
            goto error;

        char* new_buffer = realloc(buffer, (size_t) statbuf.st_blksize);
        if (!new_buffer)
        {
            errno = ENOMEM;
            goto error;
        }
        buffer = new_buffer;

        stream = fopen(filename, "r");
        if (!stream)
            goto error;

        if (setvbuf(stream, NULL, _IONBF, 0) != 0)
            goto error;

        long n_blocks = statbuf.st_size / statbuf.st_blksize;
        long tail_sz  = statbuf.st_size % statbuf.st_blksize;
        
        for (long i = 0; i < n_blocks; i++)
        {
            if (stream_write(stdout, stream, buffer, (size_t) statbuf.st_blksize) != 0)
                goto error;
        }

        if (tail_sz)
        {
            if (stream_write(stdout, stream, buffer, (size_t) tail_sz) != 0)
                goto error;
        }

        fclose(stream);
        stream = NULL;
    }

    goto finally;

error:
    ret_val = EXIT_FAILURE;
    fprintf(stderr, "%s: %s: %s\n", PROGNAME, filename, strerror(errno));

    if(stream)
        fclose(stream);

finally:
    free(buffer);

    return ret_val;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    args_t args = {};
    if (parse_args(argc, argv, &args) != 0)
        return EXIT_FAILURE;

    if (args.n_files == 0)
    {
        return cat_interactive();
    }
    else
    {
        return cat_files(args);
    }

    return EXIT_FAILURE;
}
