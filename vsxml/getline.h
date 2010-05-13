#ifndef _GETLINE_H_
#define _GETLINE_H_

#include <stdio.h>
#include <malloc.h>
#include <memory.h>
// re-implementation of GNU getline, which reads one line of text from a file handle

size_t getline(char **buf, size_t *len, FILE *fp);

#endif
