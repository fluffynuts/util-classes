#include "SimpleObject.h"

CSimpleObject::CSimpleObject(void)
{
  this->mszLastError = NULL;
  this->mstBuffers = 0;
  this->masBuffers = NULL;
}

CSimpleObject::~CSimpleObject(void)
{
  this->FreeStr(&(this->mszLastError));
  this->FreeBuffers();
}

void CSimpleObject::SetLastError(const CHAR *szErr,...)
{
  char *pos; 
  char *szFSCopy = (char *)malloc((1 + strlen(szErr)) * sizeof(char)); 
  strcpy(szFSCopy, szErr); 
  char *szArg, *szBuf; 
  size_t stReqLen = strlen(szErr); 
  va_list args; 
  va_start(args, szErr); 
  while ((pos = strstr(szFSCopy, "%"))) 
  { 
    ++pos; 
    if (*pos == '%') 
    { 
      strcpy(szFSCopy, ++pos); 
      continue; 
    } 
    while (strchr("0123456789", *pos)) 
      pos++; 
    switch (*pos) 
    { 
      case '.': 
      case 'f': 
      { 
        va_arg(args, double); 
        stReqLen += NUMERIC_FORMAT_MAXSIZE; 
        break; 
      } 
      case 's': 
      { 
        szArg = va_arg(args, char*); 
        if (szArg) 
          stReqLen += strlen(szArg); 
        else 
          stReqLen += 128; 
        break; 
      } 
      case 'u': 
      { 
        va_arg(args, unsigned int); 
        stReqLen += NUMERIC_FORMAT_MAXSIZE;
        break; 
      } 
      case 'l': 
      { 
        char *pos2 = pos; 
        pos2++; 
        switch (*pos2) 
        { 
          case 'l': 
          { 
            pos2++; 
            switch (*pos2) 
            { 
              case 'u': 
              { 
                va_arg(args, unsigned long long); 
                break; 
              } 
              case 'i': 
              default: 
              { 
                va_arg(args, long long); 
              } 
            } 
            break; 
          } 
          case 'u': 
          { 
            va_arg(args, unsigned long); 
            break; 
          } 
          case 'i': 
          case 'd': 
          default: 
          { 
            va_arg(args, long); 
          } 
        } 
        stReqLen += NUMERIC_FORMAT_MAXSIZE; 
        break; 
      } 
      case 'i': 
      default: 
      { 
        va_arg(args, int); 
        stReqLen += NUMERIC_FORMAT_MAXSIZE; 
      } 
    } 
    strcpy(szFSCopy, pos); 
  } 
  va_end(args); 
  va_start(args, szErr); 
  szBuf = (char *)malloc((1 + stReqLen) * sizeof(char)); 
  szBuf[0] = '0';
  vsprintf(szBuf, szErr, args); 
  va_end(args); 
  free(szFSCopy); 
  
  this->SetStr(&(this->mszLastError), szBuf);
  
  free(szBuf);
}

void CSimpleObject::SetStr(CHAR **szDest, const CHAR *szSrc, size_t stPad, bool fCheckAllocation, size_t *stAllocated)
{
  if (szSrc)
  {
    bool fCopy = true;
		if (*szDest == szSrc)
			fCopy = false;
    size_t stReq = strlen(szSrc) + stPad + 1; // don't forget them thar nulls!
    if (fCheckAllocation && *szDest)
    {
      size_t stAlloc = 0;
      if (stAllocated)
        stAlloc = *stAllocated;
      if (stAlloc == 0)
        stAlloc = strlen(*szDest);      // assumes that last allocation was the same as the last string size
      if (stReq > stAlloc)
      {
        *szDest = (CHAR *)realloc(*szDest, (stReq * sizeof(CHAR)));
        if (stAllocated)
          *stAllocated = stReq;
      }
    }
    else
      *szDest = (CHAR *)malloc(stReq * sizeof(CHAR));
    if (fCopy)
      strcpy(*szDest, szSrc);
  }
  else
  {
    if (*szDest)
      free(*szDest);
    *szDest = NULL;
  }
}

void CSimpleObject::FreeStr(CHAR **sz)
{
  if (*sz)
    free(*sz);
  *sz = NULL;
}

void CSimpleObject::RegisterBuffer(CHAR *sz)
{
  size_t idx = this->mstBuffers++;
  this->masBuffers = (struct SBuffer **)realloc(this->masBuffers, this->mstBuffers * sizeof(struct SBuffer *));
  SBuffer *b = new SBuffer();
  b->Buffer = sz;
  if (sz)
    b->Len = strlen(sz);
  b->InUse = true;
  this->masBuffers[idx] = b;
}

CHAR *CSimpleObject::GetBuffer(size_t stBufLen, const CHAR *szFill)
{
  CHAR *ret = NULL;
  if ((stBufLen == 0) && (szFill))
    stBufLen = strlen(szFill);
  struct SBuffer *b;
  long long llNullIDX = -1;
  for (size_t i = 0; i < this->mstBuffers; i++)
  {
    b = this->masBuffers[i];
    if (b == NULL)
    {
      if (llNullIDX == -1)
        llNullIDX = (long long)i;
      continue;
    }
    if (!b->InUse)
    {
      if (b->Len < stBufLen)
        b->Buffer = (CHAR *)realloc(b->Buffer, (stBufLen + 1) * sizeof(CHAR));
      b->InUse = true;
      ret = b->Buffer;
      break;
    }
  }
  if (ret == NULL)
  {
    size_t idx;
    if (llNullIDX == -1)
    {
      idx = this->mstBuffers++;
      this->masBuffers = (struct SBuffer **)realloc(this->masBuffers, this->mstBuffers * sizeof(struct SBuffer *));
    }
    else
      idx = (size_t)llNullIDX;
    this->masBuffers[idx] = new SBuffer(stBufLen + 1);
    ret = this->masBuffers[idx]->Buffer;
  }
  if (szFill)
    strcpy(ret, szFill);
  return ret;
}

void CSimpleObject::FreeBuffers()
{
  if (this->masBuffers)
  {
    for (size_t i = 0; i < this->mstBuffers; i++)
      if (this->masBuffers[i])
        delete this->masBuffers[i];
    free(this->masBuffers);
  }
  this->masBuffers = NULL;
  this->mstBuffers = 0;
}

void CSimpleObject::FreeBuffer(CHAR *sz)
{
  for (size_t i = 0; i < this->mstBuffers; i++)
  {
    struct SBuffer *b = this->masBuffers[i];
    if (b->Buffer == sz)
    {
      delete b;
      this->masBuffers[i] = NULL;
      return;
    }
  }
}

void CSimpleObject::ReleaseBuffers()
{
  for (size_t i = 0; i < this->mstBuffers; i++)
    this->masBuffers[i]->InUse = false;
}

void CSimpleObject::ReleaseBuffer(CHAR *sz)
{ // releases buffer for re-use without freeing up the allocated buffer mem
  for (size_t i = 0; i < this->mstBuffers; i++)
  {
    struct SBuffer *b = this->masBuffers[i];
    if (b->Buffer == sz)
    {
      b->InUse = false;
      return;
    }
  }
}

CHAR *CSimpleObject::ResizeBuffer(CHAR *sz, size_t stNewSize)
{
  for (size_t i = 0; i < this->mstBuffers; i++)
  {
    struct SBuffer *b = this->masBuffers[i];
    if (b->Buffer == sz)
    {
      b->Buffer = (CHAR *)realloc(b->Buffer, (stNewSize + 1) * sizeof(CHAR));
      b->Len = stNewSize;
      return b->Buffer;
    }
  }
  return NULL;
}

void CSimpleObject::ClearLastError()
{
  if (this->mszLastError)
  {
    free(this->mszLastError);
    this->mszLastError = NULL;
  }
}

bool CSimpleObject::AppendStr(CHAR **szDest, const CHAR *szSrc, size_t *stBufferLen)
{
  if (*szDest == NULL)
    *stBufferLen = 0;
  else if (*stBufferLen == 0)
    *stBufferLen = strlen(*szDest);
    
  size_t stReq = strlen(*szDest) + strlen(szSrc) + 1;
  if (*stBufferLen < stReq)
    *szDest = (CHAR *)realloc(*szDest, stReq * sizeof(CHAR));
  bool fRet = true;
  if (*szDest)
    strcat(*szDest, szSrc);
  else
    fRet = false;
    
  return fRet;
}

bool CSimpleObject::DirExists(const CHAR *szDirName)
{
  struct stat st;
  if ((stat(szDirName, &st) == 0) && (st.st_mode & S_IFDIR))
      return true;
  return false;
}

