#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "stack.h"

int
stackResize(Stack* stack, size_t new_cap)
{
    assert(stack->elem_size);

    void* tmp = realloc(stack->data, new_cap * stack->elem_size);
    if (!tmp)
    {
        fprintf(stderr, "resize failed\n");
        return 1;
    }

    stack->cap = new_cap;
    stack->data = tmp;

    return 0;
} 

int
stackCtor(Stack* stack, size_t elem_size, size_t init_cap)
{
    assert(elem_size);

    stack->cap = 0;
    stack->data = NULL;
    stack->size = 0;
    stack->elem_size = elem_size;

    if (init_cap)
        if (stackResize(stack, init_cap))
            return 1;

    return 0;
}

void
stackDtor(Stack* stack)
{
    free(stack->data);

    stack->size = 0;
    stack->cap = 0;
    stack->data = NULL;
    stack->elem_size = 0;
}

int
stackPush(Stack* stack, const void* elem)
{
    assert(stack->elem_size);

    if (stack->size == stack->cap)
    {
        size_t new_cap = stack->cap ? stack->cap * 2 : 8; 
        if (stackResize(stack, new_cap))
            return 1;
    }

    memcpy((char*) stack->data + stack->size * stack->elem_size,
           elem,
           stack->elem_size);

    stack->size++;

    return 0;
}

void*
stackAt(Stack* stack, size_t index)
{
    return (char*) stack->data + index * stack->elem_size;
}

void*
stackDetach(Stack* stack)
{
    void* ptr = stack->data;

    stack->data = NULL;
    stack->cap  = 0;
    stack->size = 0;

    return ptr;
}

