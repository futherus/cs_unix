CC = gcc
CFLAGS = -O2 -mavx -mavx2 -g -fmax-errors=100 -Wall -Wextra  	    \
	-Waggressive-loop-optimizations 	   					\
	-Wcast-align -Wcast-qual 	   					\
	-Wchar-subscripts -Wconversion        				\
	-Wempty-body -Wfloat-equal 		   						\
	-Wformat-nonliteral -Wformat-security -Wformat-signedness       				\
	-Wformat=2 -Winline -Wlarger-than=8192 -Wlogical-op 	           				\
	-Wmissing-declarations -Wopenmp-simd 	   					\
	-Wpacked -Wpointer-arith -Wredundant-decls 				\
	-Wshadow -Wsign-conversion -Wstack-usage=8192      				\
	-Wstrict-overflow=2 			   						\
	-Wsuggest-attribute=noreturn -Wsuggest-final-methods 	   					\
	-Wsuggest-final-types -Wswitch-default 	   				\
	-Wswitch-enum -Wsync-nand -Wundef -Wunreachable-code -Wunused   				\
	-Wvariadic-macros 	   					\
	-Wno-missing-field-initializers -Wno-narrowing 	           					\
	-Wno-varargs -fcheck-new 		   						\
	-fstack-check -fstack-protector            				\
	-fstrict-overflow -flto-odr-type-merging 	   		   						\
	-fno-omit-frame-pointer                                         				\
	-fsanitize=address 	                                           				\
	-fsanitize=alignment                                            				\
	-fsanitize=bool                                                 				\
	-fsanitize=bounds                                               				\
	-fsanitize=enum                                                 				\
	-fsanitize=float-cast-overflow 	                           					\
	-fsanitize=float-divide-by-zero 			           							\
	-fsanitize=integer-divide-by-zero                               				\
	-fsanitize=leak 	                                           					\
	-fsanitize=nonnull-attribute                                    				\
	-fsanitize=null 	                                           					\
	-fsanitize=object-size                                          				\
	-fsanitize=return 		                                   					\
	-fsanitize=returns-nonnull-attribute                            				\
	-fsanitize=shift                                                				\
	-fsanitize=signed-integer-overflow                              				\
	-fsanitize=undefined                                            				\
	-fsanitize=unreachable                                          				\
	-fsanitize=vla-bound                                            				\
	-fsanitize=vptr                                                 				\
	-fPIE                                                           				\
	-lm -pie

SRC = eagle.c
TARGET = eagle

all:
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

distclean:
	rm -rf $(TARGET)

