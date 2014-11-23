#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(void)
{	char buf[512];
	fprintf(stderr, "haha:");
    fgets(buf, 511, stdin);
    	// remove '\n'
    buf[strlen(buf) - 1] = '\0';
    int a = 1~/10;
    fprintf(stderr, "%d\n", a);
    return 0;
}