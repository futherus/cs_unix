#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

typedef struct
{
    int    n_file;
    char** file_arr;

    int is_long;
    int is_dir_as_file;
    int is_all;
    int is_recursive;
    int is_print_inode;
    int is_numeric_uid_gid;
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
    memset(args, 0, sizeof(args_t));

    int file_arr_sz = 0;
    char** file_arr = calloc(sizeof(char**), (size_t) argc);
    if (!file_arr)
        return error("%s\n", strerror(errno));

    while (optind < argc)
    {
        int opt = getopt(argc, argv, "+ldaRin");
        switch (opt)
        {
            case -1:
                break;
            case 'l':
                args->is_long = 1;
                continue;
            case 'd':
                args->is_dir_as_file = 1;
                continue;
            case 'a':
                args->is_all= 1;
                continue;
            case 'R':
                args->is_recursive= 1;
                continue;
            case 'i':
                args->is_print_inode = 1;
                continue;
            case 'n':
                args->is_numeric_uid_gid = 1;
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
        file_arr_sz = 1;
        file_arr[0] = ".";
    }

    char** tmp = realloc(file_arr, (size_t) file_arr_sz * sizeof(char*));
    if (!tmp)
    {
        free(file_arr);

        return error("%s\n", strerror(errno));
    }
    file_arr = tmp;

    args->n_file = file_arr_sz;
    args->file_arr = file_arr;

    return 0;
}

static int
print_short(const char* filename)
{
    printf("%s ", filename);

    return 0;
}

static int
print_long(const args_t* args, const char* filename, struct stat* stbuf)
{
    switch(stbuf->st_mode & S_IFMT)
    {
        case S_IFDIR: 
            printf("d");
            break;
        case S_IFREG: 
            printf("-");
            break;
        case S_IFLNK: 
            printf("l");
            break;
        default:
            printf("?");
            break;
    }

    printf(stbuf->st_mode & S_IRUSR ? "r" : "-");
    printf(stbuf->st_mode & S_IWUSR ? "w" : "-");
    printf(stbuf->st_mode & S_IXUSR ? "x" : "-");

    printf(stbuf->st_mode & S_IRGRP ? "r" : "-");
    printf(stbuf->st_mode & S_IWGRP ? "w" : "-");
    printf(stbuf->st_mode & S_IXGRP ? "x" : "-");

    printf(stbuf->st_mode & S_IROTH ? "r" : "-");
    printf(stbuf->st_mode & S_IWOTH ? "w" : "-");
    printf(stbuf->st_mode & S_IXOTH ? "x" : "-");

    printf(" %lu", stbuf->st_nlink);


    if (!args->is_numeric_uid_gid)
    {
        struct passwd* user_info = getpwuid(stbuf->st_uid);
        if (user_info)
            printf(" %s", user_info->pw_name);
        else
            printf(" %u", stbuf->st_uid);
       
        struct group* group_info = getgrgid(stbuf->st_gid);
        if (group_info)
            printf(" %s", group_info->gr_name);
        else
            printf(" %u", stbuf->st_gid);
    }
    else
    {
        printf(" %u %u", stbuf->st_uid, stbuf->st_gid);
    }

    printf(" %ld", stbuf->st_size);

    struct tm* curtime = localtime(&stbuf->st_mtim.tv_sec);
    printf(" %02d/%02d/%d %02d:%02d",
           curtime->tm_mday, curtime->tm_mon, curtime->tm_year + 1900,
           curtime->tm_hour, curtime->tm_min);

    printf(" %s\n", filename);

    return 0;
}

static int
print_file(const args_t* args, const char* filename, struct stat* stbuf)
{
    if (args->is_long)
        print_long(args, filename, stbuf);
    else
        print_short(filename);
    
    return 0;
}

static int
filter_hidden(const struct dirent* ent)
{
    if (ent->d_name[0] == '.')
        return 0;

    return 1;
}

static int
print_dir(const args_t* args, char* filename_buf, struct stat* stbuf)
{
    if (args->is_recursive || args->n_file > 1)
        printf("%s:\n", filename_buf);
    if (args->is_long)
        printf("total: %ld\n", stbuf->st_blocks);

    size_t filename_sz = strlen(filename_buf);
    struct dirent** entlist  = NULL;
    struct stat*    statlist = NULL;

    int retval = 0;

    int (*filter)(const struct dirent*) = NULL;
    if (!args->is_all)
        filter = &filter_hidden;

    int n_ent = scandir(filename_buf, &entlist, filter, alphasort);
    if (n_ent == -1)
    {
        retval = error("%s\n", strerror(errno));
        goto cleanup;
    }

    statlist = (struct stat*) malloc((size_t) n_ent * sizeof(struct stat));
    if (!statlist)
    {
        retval = error("%s\n", strerror(errno));
        goto cleanup;
    }

    for (int i = 0; i < n_ent; i++)
    {
        sprintf(filename_buf + filename_sz, "/%s", entlist[i]->d_name);
        if (lstat(filename_buf, &statlist[i]) == -1)
        {
            retval = error("cannot stat %s: %s\n", entlist[i]->d_name, strerror(errno));
            goto cleanup;
        }
        filename_buf[filename_sz] = '\0';
    }

    for (int i = 0; i < n_ent; i++)
        retval |= print_file(args, entlist[i]->d_name, &statlist[i]);
 
    printf("\n");

    if (args->is_recursive)
    {
        for (int i = 0; i < n_ent; i++)
        {
            if (S_ISDIR(statlist[i].st_mode) &&
               (strcmp(entlist[i]->d_name, ".") != 0) &&
               (strcmp(entlist[i]->d_name, "..") != 0))
            {
                sprintf(filename_buf + filename_sz, "/%s", entlist[i]->d_name);
                retval |= print_dir(args, filename_buf, &statlist[i]);
                filename_buf[filename_sz] = '\0';
            }
        }
    }

cleanup:
    if (entlist)
    {
        for (int i = 0; i < n_ent; i++)
            free(entlist[i]);

        free(entlist);
    }

    if (statlist)
        free(statlist);

    return retval;
}

static int
print_entry(const args_t* args, char* filename_buf)
{
    struct stat stbuf;
    if (lstat(filename_buf, &stbuf) == -1)
        return error("Cannot stat %s: %s\n", filename_buf, strerror(errno));

    if (S_ISDIR(stbuf.st_mode) && !args->is_dir_as_file)
        return print_dir(args, filename_buf, &stbuf);
    else
        return print_file(args, filename_buf, &stbuf);
    
    return error("Unreachable\n");
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];
    
    args_t args = {};
    if (parse_args(argc, argv, &args) != 0)
        return 1;
    
    int retval = 0;

    long filename_sz = pathconf(".", _PC_PATH_MAX);
    if (filename_sz == -1)
    {
        if (errno)
            return error("%s\n", strerror(errno));
        else
            filename_sz = FILENAME_MAX;
    }

    char* filename_buf = (char*) malloc((size_t) filename_sz * sizeof(char));
    if (!filename_buf)
        return error("allocation failed: %s\n", strerror(errno));

    for (int i = 0; i < args.n_file; i++)
    {
        sprintf(filename_buf, "%s", args.file_arr[i]);
        print_entry(&args, filename_buf);
    }

    free(args.file_arr);
    free(filename_buf);

    return retval;
}

