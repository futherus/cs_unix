#include <stdio.h>
#include <string.h>
#include <stdbool.h>
			
int
main(int argc, char** argv)
{

	int iter = 1;
	bool no_newline = false;

	if (argc == 1)
	{
		printf("\n");
		return 0;
	}

	if (strcmp(argv[iter], "-n") == 0)
	{
		iter++;
		no_newline = true;
	}

	for (; iter < argc; iter++)
	{
		printf("%s", argv[iter]);
		if (iter < argc - 1)
			putchar(' ');
	}

	if (!no_newline)
		printf("\n");
	
	return 0;
}

