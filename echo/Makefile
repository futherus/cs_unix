CFLAGS := -Wall -Wextra -g
CC := gcc

SRC := echo.c
OBJ := $(SRC:.c=.o)
TARGET := myecho

all: $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

*.o: *.c
	$(CC) -c $^ -o $@ $(CFLAGS)

distclean:
	rm -rf $(TARGET) $(OBJ)

