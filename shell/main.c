#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <assert.h>

#include "stack.h"

typedef struct
{
    int    is_verbose;
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
    while (optind < argc)
    {
        int opt = getopt(argc, argv, "+v");
        switch (opt)
        {
            case -1:
                break;
            case 'v':
                args->is_verbose = 1;
                continue;
            case '?':
            default:
                // FIXME
                fprintf(stderr, "Wrong usage\n");
                abort();
        }
    }

    return 0;
}

typedef enum
{
    TOKEN_INVLD,
    TOKEN_END,
    TOKEN_WORD,
    TOKEN_PIPE,
} TokenType;

typedef struct
{
    TokenType type;
    union
    {
        char* word;
    } val;
} Token;

static int
tokenize(Stack* token_stack, char* line)
{
    assert(line);

    const char delim[] = " \t\n";

    char* pos = strtok(line, delim);

    Token tmp = {0};
    while (pos)
    {
        if (strcmp(pos, "|") == 0)
        {
            tmp.type = TOKEN_PIPE;
        }
        else
        {
            tmp.type = TOKEN_WORD;
            tmp.val.word = pos;
        }

        if (stackPush(token_stack, &tmp))
            return 1;

        pos = strtok(NULL, delim);
    }

    tmp.type = TOKEN_END;
    if (stackPush(token_stack, &tmp))
        return 1;

    return 0;
}

typedef struct
{
    int argc;
    char** argv;
} cmd_t;

static int
parseCmds(Stack* cmds_stack, Stack* token_stack)
{
    Stack argv_stack = {0};
    stackCtor(&argv_stack, sizeof(char*), 0);

    for (size_t indx = 0; indx < token_stack->size; indx++)
    {
        Token* tok = (Token*) stackAt(token_stack, indx);
        switch(tok->type)
        {
            case TOKEN_PIPE:
            case TOKEN_END:
                if (argv_stack.size == 0)
                    return error("missing command expression\n");
                
                /* terminate argv[] */
                char* nul = NULL;
                stackPush(&argv_stack, &nul);

                cmd_t tmp = {.argc = (int) argv_stack.size, .argv = argv_stack.data};
                stackPush(cmds_stack, &tmp);

                stackDetach(&argv_stack);
                break;

            case TOKEN_WORD:
                stackPush(&argv_stack, &tok->val.word);
                break;

            case TOKEN_INVLD:
            default:
                assert(0 && "invalid token type");
                break;
        }
    }

    assert(argv_stack.data == NULL && "data is not detached\n");
    /* do not need to cleanup argv_stack
     * because argv_stack.data is detached */
    
    return 0;
}

static void
closePipes(int (*fildes)[2], size_t n_cmds)
{
    for (size_t i = 0; i < n_cmds - 1; i++)
    {
        if (fildes[i][1] != -1)
            close(fildes[i][1]);

        if (fildes[i + 1][0] != -1)
            close(fildes[i + 1][0]);
    }

    free(fildes);
}

static int
initPipes(int (**fildes_ptr)[2], size_t n_cmds)
{
    int (*fildes)[2] = malloc(2 * n_cmds * sizeof(int));
    if (fildes == NULL)
        return error("bad alloc: %s\n", strerror(errno));

    for (size_t i = 0; i < n_cmds; i++)
    {
        fildes[i][0] = -1;
        fildes[i][1] = -1;
    }

    fildes[0][0] = STDIN_FILENO;
    fildes[n_cmds - 1][1] = STDOUT_FILENO;

    int retval = 0;
    for (size_t indx = 0; indx < n_cmds - 1; indx++)
    {
        int fildes_pipe[2] = {0};
        
        if (pipe(fildes_pipe) == -1)
        {
            closePipes(fildes, n_cmds);
            return error("creating pipe failed: %s\n", strerror(errno));
        }

        fildes[indx][1] = fildes_pipe[1];
        fildes[indx + 1][0] = fildes_pipe[0];
    }

    *fildes_ptr = fildes;

    return 0;
}

static int
execute(Stack* cmds_stack)
{
    assert(cmds_stack->size > 0 && "No cmds to execute");

    int retval = 0;

    int (*fildes)[2] = NULL;
    if (initPipes(&fildes, cmds_stack->size))
        return 1;

    for (size_t i = 0; i < cmds_stack->size; i++)
    {
        cmd_t* cmd = (cmd_t*) stackAt(cmds_stack, i);

        int pid = fork();
        if (pid == 0)
        {
            if (dup2(fildes[i][0], STDIN_FILENO) == -1)
            {
                closePipes(fildes, cmds_stack->size);
                return error("descriptor duplication failed: %s\n", strerror(errno));
            }

            if (dup2(fildes[i][1], STDOUT_FILENO) == -1)
            {
                closePipes(fildes, cmds_stack->size);
                return error("descriptor duplication failed: %s\n", strerror(errno));
            }

            closePipes(fildes, cmds_stack->size);

            execvp(cmd->argv[0], cmd->argv);

            return error("cannot run <%s>: %s\n", cmd->argv[0], strerror(errno));
        }
    }

    closePipes(fildes, cmds_stack->size);

    int wstatus = 0;
    for (size_t i = 0; i < cmds_stack->size; i++)
        wait(&wstatus);

    return 0;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];
    
    Args args = {0};
    if (parseArgs(argc, argv, &args) != 0)
        return 1;
    
    int retval = 0;

    Stack token_stack = {0};
    retval = stackCtor(&token_stack, sizeof(Token), 0);
    if (retval)
    {
        error("bad alloc: %s\n", strerror(ENOMEM));
        goto cleanup;
    }

    Stack cmd_stack = {0};
    retval = stackCtor(&cmd_stack, sizeof(cmd_t), 0);
    if (retval)
    {
        error("bad alloc: %s\n", strerror(ENOMEM));
        goto cleanup;
    }

    char* line = NULL;
    while (1)
    {
        line = readline("<^_^> ");
        if (line == NULL)
            goto cleanup;

        retval = tokenize(&token_stack, line);
        if (retval)
            goto cleanup;

        /* input is empty, tokenize put only TOKEN_END */
        if (token_stack.size == 1)
        {
            token_stack.size = 0;
            free(line);
            line = NULL;
            continue;
        }

        retval = parseCmds(&cmd_stack, &token_stack);
        if (retval)
            goto cleanup;

        retval = execute(&cmd_stack);
        if (retval)
            goto cleanup;

        for (size_t i = 0; i < cmd_stack.size; i++)
            free(((cmd_t*) stackAt(&cmd_stack, i))->argv);
        
        cmd_stack.size   = 0;
        token_stack.size = 0;

        free(line);
        line = NULL;
    }

cleanup:
    free(line);
    rl_clear_history();

    for (size_t i = 0; i < cmd_stack.size; i++)
        free(((cmd_t*) stackAt(&cmd_stack, i))->argv);
 
    stackDtor(&token_stack);
    stackDtor(&cmd_stack);

    return retval;
}
