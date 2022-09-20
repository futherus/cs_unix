#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

typedef struct
{
    int    n_src;
    char** src_arr;
    char*  dst;

    int    is_interactive;
    int    is_verbose;
    int    is_force; 
} args_t;

const char* PROGNAME = NULL;

static int
file_copy(char* file_src, char* file_dst, int mask_dst)
{
    struct stat statbuf = {};
    int fd_src = -1;
    int fd_dst = -1;
    char* buffer = NULL;

    if (stat(file_src, &statbuf) == -1)
    {
        fprintf(stderr, "%s: %s: %s\n", PROGNAME, file_src, strerror(errno));

        return EXIT_FAILURE;
    }

    int fd_src = open(file_src, O_RDONLY);
    if (fd_src == -1)
    {
        fprintf(stderr, "%s: %s: %s\n", PROGNAME, file_src, strerror(errno));

        return EXIT_FAILURE;
    }

    int fd_dst = open(file_dst, O_WRONLY | mask_dst, statbuf.st_mode);
    if (fd_dst == -1)
    {
        fprintf(stderr, "%s: %s: %s\n", PROGNAME, file_dst, strerror(errno));

        return EXIT_FAILURE;
    }

    char* buffer = malloc(statbuf.st_blksize);
    if (!buffer)
    {
        fprintf(stderr, "%s: %s\n", PROGNAME, strerror(ENOMEM));

        return EXIT_FAILURE;
    }

    long n_blocks = statbuf.st_size / statbuf.st_blksize;
    long tail_sz  = statbuf.st_size % statbuf.st_blksize;

    for (long i = 0; i < n_blocks; i++)
    {
        if (read(fd_src, buffer, statbuf.st_blksize) != statbuf.st_blksize)
        {
            return EXIT_FAILURE;
        }

        if (write(fd_dst, buffer, statbuf.st_blksize) != statbuf.st_blksize)
        {
            return EXIT_FAILURE;
        }
    }

    if (read(fd_src, buffer, tail_sz) != tail_sz)
    {
        return EXIT_FAILURE;
    }

    if (write(fd_dst, buffer, tail_sz) != tail_sz)
    {
        return EXIT_FAILURE;
    }

    close(fd_src);
    close(fd_dst);

    return 0;
}

static int
parse_args(int argc, char* argv[], args_t* args)
{
    int file_arr_sz = 0;
    char** file_arr = calloc(sizeof(char**), argc);
    if (!file_arr)
    {
        fprintf(stderr, "%s: %s\n", PROGNAME, strerror(ENOMEM));

        return 1;
    }

    while (optind < argc)
    {
        int opt = getopt(argc, argv, "+ivf");
        switch (opt)
        {
            case -1:
                break;
            case 'i':
                args->is_interactive = 1;
                continue;
            case 'v':
                args->is_verbose = 1;
                continue;
            case 'f':
                args->is_force = 1;
                continue;
            case '?':
            default:
                fprintf(stderr, "Wrong usage\n");
                abort();
        }

        file_arr[file_arr_sz++] = argv[optind++];
    }

    if (file_arr_sz == 0)
    {
        fprintf(stderr, "%s: missing file operand\n", PROGNAME);
        
        return 1;
    }
    else if (file_arr_sz == 1)
    {
        fprintf(stderr, "%s: missing destination file operand after '%s'", PROGNAME, file_arr[0]);

        return 1;
    }

    args->dst     = file_arr[file_arr_sz - 1];
    args->n_src   = file_arr_sz - 1;
    args->src_arr = file_arr;

    return 0;
}

/*
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
*/

static int
copy_files(args_t args)
{
    struct stat statbuf = {};
    if (stat(args.dst, &statbuf) == -1 && errno == ENOENT)
    {
        if (args.n_src > 1)
        {
            fprintf(stderr, "%s: target '%s': %s\n", PROGNAME, args.dst, strerror(ENOENT));

            return EXIT_FAILURE;
        }

        stat(args.src_arr[0], &statbuf);
        int fd_out = open(args.dst, O_CREAT | O_WRONLY | O_EXCL, statbuf.st_mode);
        if (fd_out == -1)
        {
            fprintf(stderr, "%s: %s: %s\n", PROGNAME, args.src_arr[0], strerror(errno));

            return EXIT_FAILURE;
        }

        int fd_in = open(args.src_arr[0], O_RDONLY);
        if (fd_in == -1)
        {
            fprintf(stderr, "%s: %s: %s\n", PROGNAME, args.src_arr[0], strerror(errno));

            return EXIT_FAILURE;
        }

        char* buffer = malloc(statbuf.st_blksize);
    }
    
}
int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    args_t args = {};
    if (parse_args(argc, argv, &args) != 0)
        return EXIT_FAILURE;

    free(args.src_arr);

    return EXIT_FAILURE;
}
