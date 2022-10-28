#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

static int
error(char* fmt, ...)
{
	va_list args = {};
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	return 1;
}

typedef struct
{
    ssize_t n_lines;
    ssize_t n_words; 
    ssize_t n_bytes;
} fileinfo;

static int
gather_info(fileinfo* info)
{
    char* buf = (char*) malloc(BUFSIZ);
    if (!buf)
        return error("Allocation failed: %s\n", strerror(errno));

    fileinfo tmp_info = {0};

    int in_word = 0;
    while (1)
    {
        ssize_t n_read = read(0, buf, BUFSIZ);
        if (!n_read)
            break;
        
        tmp_info.n_bytes += n_read;

        for (ssize_t i = 0; i < n_read; i++)
        {
            if (in_word && !isalnum(buf[i]))
                in_word = 0;

            if (!in_word && isalnum(buf[i]))
            {
                tmp_info.n_words++;
                in_word = 1;
            }

            if (buf[i] == '\n')
                tmp_info.n_lines++;
        }
    }

    free(buf);

    *info = tmp_info;

    return 0;
}

int
main(int argc, char** argv)
{
    memmove(argv, argv + 1, (size_t) (argc - 1) * sizeof(char*));
    argv[argc - 1] = NULL;

    int fds[2] = {};
    if (pipe(fds))
        return error("Cannot open pipe: %s\n", strerror(errno));

    int pid = fork();
    if (pid == 0)
    {
        /* set child stdout descriptor to write end of pipe */
        dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
 
        execvp(argv[0], argv);
        return error("Cannot measure '%s': %s\n", argv[0], strerror(errno));
    }

    /* set parent stdin descriptor to read end of pipe */
    dup2(fds[0], 0);
    close(fds[0]);
    close(fds[1]);
 
    fileinfo info = {};
    if (gather_info(&info) != 0)
        return EXIT_FAILURE;

    printf("bytes: %ld, words: %ld, lines: %ld\n", info.n_bytes, info.n_words, info.n_lines);

    return 0;
}

