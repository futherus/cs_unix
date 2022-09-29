#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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
    int file_arr_sz = 0;
    char** file_arr = calloc(sizeof(char**), (size_t) argc);
    if (!file_arr)
        return error("%s\n", strerror(errno));

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
                // FIXME
                fprintf(stderr, "Wrong usage\n");
                abort();
        }

        file_arr[file_arr_sz++] = argv[optind++];
    }

    if (file_arr_sz == 0)
    {
        free(file_arr);

        return error("missing file operand\n");
    }
    else if (file_arr_sz == 1)
    {
        int retval = error("missing destination file operand after '%s'\n", file_arr[0]);
        free(file_arr);

        return retval;
    }

    args->dst = file_arr[file_arr_sz - 1];

    char** tmp = realloc(file_arr, (size_t) (file_arr_sz - 1) * sizeof(char*));
    if (!tmp)
    {
        free(file_arr);

        return error("%s\n", strerror(errno));
    }
    file_arr = tmp;

    args->n_src   = file_arr_sz - 1;
    args->src_arr = file_arr;

    return 0;
}

static int
is_overwrite(args_t* args, char* dst)
{
    if (args->is_interactive)
    {
        fprintf(stderr, "%s: overwrite '%s'? ", PROGNAME, dst);

        int ch = getchar();
        int tmp = 0;
        while ((tmp = getchar()) != '\n' && ch != EOF)
            ;

        return ch == 'y';
    }

    return 1;
}

static int
copy_file(args_t* args,
          char* file_src, struct stat* stat_src,
          char* file_dst, struct stat* stat_dst)
{
    int fd_src = -1;
    int fd_dst = -1;
    char* buffer = NULL;

    int retval = 0;

    if (S_ISDIR(stat_src->st_mode))
        return error("%s: %s\n", file_src, strerror(EISDIR));

    if (stat_dst &&
        stat_dst->st_dev == stat_src->st_dev &&
        stat_dst->st_ino == stat_src->st_ino)
    {
        return error("'%s' and '%s' are the same file\n", file_src, file_dst);
    }

    if (stat_dst)
    {
        if (!is_overwrite(args, file_dst))
            return 0;
        
        if (args->is_force)
            if(unlink(file_dst) == -1)
                return error("cannot remove '%s': %s", file_dst, strerror(errno));
    }

    fd_src = open(file_src, O_RDONLY);
    if (fd_src == -1)
    {
        retval = error("%s: %s\n", file_src, strerror(errno));

        goto finally;
    }

    fd_dst = open(file_dst, O_WRONLY | O_CREAT | O_TRUNC, stat_src->st_mode);
    if (fd_dst == -1)
    {
        retval = error("%s: %s\n", file_dst, strerror(errno));

        goto finally;
    }

    buffer = calloc((size_t) stat_src->st_blksize, sizeof(char));
    if (!buffer)
    {
        retval = error("%s\n", strerror(errno));

        goto finally;
    }

    long n_blocks = stat_src->st_size / stat_src->st_blksize;
    long tail_sz  = stat_src->st_size % stat_src->st_blksize;

    for (long i = 0; i < n_blocks; i++)
    {
        if (read(fd_src, buffer, (size_t) stat_src->st_blksize) != stat_src->st_blksize)
        {
            retval = error("%s: %s\n", file_src, strerror(errno));

            goto finally;
        }

        if (write(fd_dst, buffer, (size_t) stat_src->st_blksize) != stat_src->st_blksize)
        {
            retval = error("%s: %s\n", file_dst, strerror(errno));

            goto finally;
        }
    }

    if (read(fd_src, buffer, (size_t) tail_sz) != tail_sz)
    {
        retval = error("%s: %s\n", file_src, strerror(errno));

        goto finally;
    }

    if (write(fd_dst, buffer, (size_t) tail_sz) != tail_sz)
    {
        retval = error("%s: %s\n", file_dst, strerror(errno));

        goto finally;
    }

finally:

    free(buffer);

    if (fd_src != -1)
        close(fd_src);

    if (fd_dst != -1)
        close(fd_dst);

    if (args->is_verbose && retval == 0)
        fprintf(stderr, "'%s' -> '%s'\n", file_src, file_dst);

    return retval;
}

static int
copy_to_newfile(args_t* args)
{
    struct stat stat_src = {};
    if (stat(args->src_arr[0], &stat_src) == -1)
        return error("%s: %s\n", args->src_arr[0], strerror(errno));

    return copy_file(args, args->src_arr[0], &stat_src, args->dst, NULL);
}

static int
copy_to_file(args_t* args, struct stat* stat_dst)
{
    struct stat stat_src = {};
    if (stat(args->src_arr[0], &stat_src) == -1)
        return error("%s: %s\n", args->src_arr[0], strerror(errno));

    return copy_file(args, args->src_arr[0], &stat_src, args->dst, stat_dst);
}

static int
copy_to_dir(args_t* args)
{
    errno = 0;
    long namebuf_sz = pathconf(".", _PC_PATH_MAX);
    if (namebuf_sz == -1)
    {
        if (errno)
            return error("%s\n", strerror(errno));
        else
            return error("%s\n", "Pathname limit is indeterminate");
    }

    errno = 0;
    long max_filename = pathconf(".", _PC_NAME_MAX);
    if (max_filename == -1)
    {
        if (errno)
            return error("%s\n", strerror(errno));
        else
            return error("%s\n", "Pathname limit is indeterminate");
    }
    
    long dst_sz = (long) strlen(args->dst);
    if (dst_sz + 1 + max_filename + 1 > namebuf_sz)
        return error("%s\n", strerror(ENAMETOOLONG));

    char* namebuf = (char*) calloc((size_t) namebuf_sz, sizeof(char));
    if (!namebuf)
        return error("%s\n", strerror(errno));

    memcpy(namebuf, args->dst, (size_t) (dst_sz + 1));
    namebuf[dst_sz] = '/';
    namebuf[dst_sz + 1] = '\0';
    
    int retval = 0;
    for (int i = 0; i < args->n_src && retval == 0; i++)
    {
        /*
            According to POSIX, basename() can modify it's argument,
            therefore we must duplicate string before using it.
        */
        char* src = strdup(args->src_arr[i]);
        if (!src)
        {
            retval = error("%s\n", strerror(errno));
            break;
        }

        char* src_basename = basename(src);
        strcpy(namebuf + dst_sz + 1, src_basename);
        free(src);

        struct stat stat_src = {};
        if (stat(args->src_arr[i], &stat_src) == -1)
        {
            retval = error("%s: %s\n", args->src_arr[0], strerror(errno));
            
            break;
        }

        struct stat  stat_dst = {};
        struct stat* stat_ptr = &stat_dst;
        if (stat(namebuf, &stat_dst) == -1)
        {
            if (errno != ENOENT)
            {
                retval = error("%s: %s\n", namebuf, strerror(errno));

                break;
            }
            
            stat_ptr = NULL;
        }

        retval = copy_file(args, args->src_arr[i], &stat_src, namebuf, stat_ptr);
    }

    free(namebuf);

    return retval;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];
    
    args_t args = {};
    if (parse_args(argc, argv, &args) != 0)
        return 1;
    
    int retval = 0;
    struct stat stat_dst = {};

    errno = 0;
    if (stat(args.dst, &stat_dst) == -1)
    {
        if (errno != ENOENT)
        {
            retval = error("%s: %s\n", args.dst, strerror(errno));
        }
        else
        {
            if (args.n_src > 1)
                retval = error("target '%s': %s\n", args.dst, strerror(ENOENT));
            else
                retval = copy_to_newfile(&args);
        }
    }
    else if (!S_ISDIR(stat_dst.st_mode))
    {
        if (args.n_src > 1)
            retval = error("target '%s': %s\n", args.dst, strerror(ENOTDIR));
        else
            retval = copy_to_file(&args, &stat_dst);
    }
    else
    {
        retval = copy_to_dir(&args);
    }

    free(args.src_arr);

    return retval;
}
