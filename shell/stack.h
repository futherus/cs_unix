#ifndef STACK_H
#define STACK_H

#include <stdlib.h>

typedef struct
{
    void* data;
    size_t elem_size;
    size_t size;
    size_t cap;
} Stack;

int
stackResize(Stack* stack, size_t new_cap);

int
stackCtor(Stack* stack, size_t elem_size, size_t init_cap);

void
stackDtor(Stack* stack);

int
stackPush(Stack* stack, const void* elem);

void*
stackAt(Stack* stack, size_t index);

void*
stackDetach(Stack* stack);

#endif // STACK_H

