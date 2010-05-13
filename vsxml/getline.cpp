#include "getline.h"
#define CHUNKSIZE 32

size_t getline (char **buf, size_t *len, FILE *fp)
{
  if (fp == NULL)
    return 0;
  size_t stAllocated = *len;
  size_t ret = 0;

  if ((stAllocated < CHUNKSIZE) || (*buf == NULL))
  {
    stAllocated = CHUNKSIZE;
    *buf = (char *)realloc(*buf, stAllocated * sizeof(char));
  }
  char *ptr = *buf;
  *ptr = '\0';
  
  char c;
  while (true)
  {
    c = fgetc(fp);
    if ((c == EOF) || (feof(fp)))
      break;
    *ptr = c;
    ret++;
    if (*ptr == '\n')
      break;
    ptr++;
    if ((stAllocated - ret) < 2)
    {
      stAllocated += CHUNKSIZE;
      *buf = (char *)realloc(*buf, stAllocated * sizeof(char));
      ptr = *buf;
      ptr += ret * sizeof(char);
    }
  }
  if (ret)
  { // null-terminate
    ptr++;
    *ptr = '\0';
  }
  *len = stAllocated;
  return ret;
}
