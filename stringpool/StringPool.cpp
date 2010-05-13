#include "StringPool.h"


CStringPool::CStringPool(void)
{
  this->pool = NULL;
  this->muiMaxReserveBuffers = DEFAULT_MAX_RESERVE_BUFFERS;
  this->mLock = XT_CreateMutex();
}

CStringPool::~CStringPool(void)
{
  XT_LockMutex(this->mLock);
  while (this->pool)
  {
    SSZPoolItem *i = this->pool->_next;
    delete this->pool;
    this->pool = i;
  }
  XT_ReleaseMutex(this->mLock);
  XT_DestroyMutex(this->mLock);
}

CHAR *CStringPool::CloneStr(const CHAR *szCopyThis, size_t stPadChars)
{
  CHAR *ret = this->AllocStr(strlen(szCopyThis) + stPadChars);
  strcpy(ret, szCopyThis);
  return ret;
}

CHAR *CStringPool::AllocPath()
{
  // convenience function
  return this->AllocStr(MAX_PATH);
}

CHAR *CStringPool::AllocStr(size_t stLen)     // allocates a buffer for a string; the buffer can hold stLen chars
{
  SSZPoolItem *prev, *next;
  size_t stMisses = 0;
  XT_LockMutex(this->mLock);
  if (this->pool)
  {
    prev = this->pool;
    next = prev;
    while (next)
    {
      if (!next->fInUse)
      {
        if ((++stMisses > this->muiMaxReserveBuffers) || (next->stLen > stLen))
        {
          next->fInUse = true;
          if (next->stLen <= stLen)
          {
            next->sz = (CHAR *)realloc(next->sz, (stLen + 1) * sizeof(CHAR)); // SSZPoolItem struct allocates for NULL char; so must we
            next->stLen = stLen;
          }
          CHAR *ret = next->sz;
          XT_ReleaseMutex(this->mLock);
          return ret;
        }
      }
      prev = next;
      next = prev->_next;
    }
    prev->_next = new SSZPoolItem(stLen);
    CHAR *sz = prev->_next->sz;
    sz[0] = '\0';   // make sure buffer is "well-behaved"
    XT_ReleaseMutex(this->mLock);
    return sz;
  }
  this->pool = new SSZPoolItem(stLen);
  CHAR *ret = this->pool->sz;
  XT_ReleaseMutex(this->mLock);
  return ret;
}

void CStringPool::Release(CHAR *sz1, CHAR *sz2,...)
{
  SSZPoolItem *i = this->pool;
  if (sz1)
    sz1[0] = '\0';     // make sure buffer is "well-behaved" for any re-use
  XT_LockMutex(this->mLock);
  while (i)
  {
    if (i->sz == sz1)
    {
      i->fInUse = false;
      XT_ReleaseMutex(this->mLock);
      return;
    }
    i = i->_next;
  }
  XT_ReleaseMutex(this->mLock);
  if (sz2)  // variable list
  {
    this->Release(sz2);
    va_list args;
    va_start(args, sz2);
    CHAR *szArg;
    while (szArg = va_arg(args, CHAR*))
      this->Release(szArg);
  }
}

void CStringPool::ReleaseAll()    // releases all strings in the pool (DOES NOT FREE THEM)
{
  SSZPoolItem *i = this->pool;
  XT_LockMutex(this->mLock);
  while (i)
  {
    i->sz[0] = '\0';
    i->fInUse = false;
    i = i->_next;
  }
  XT_ReleaseMutex(this->mLock);
}

void CStringPool::FreeUnused()
{
  // you should only call this if you are super paranoid about memory usage; strings are freed at destruction time
  XT_LockMutex(this->mLock);
  if (this->pool)
  {
    SSZPoolItem *current = this->pool;
    while (current)
    {
      if (current->_next->fInUse)
      {
        current = current->_next;
        continue;
      }
      SSZPoolItem *skip = current->_next->_next;
      delete current->_next;
      current->_next = skip;
      current = skip;
    }
    if (!this->pool->fInUse)
    {
      delete this->pool;
      this->pool = NULL;
    }
  }
  XT_ReleaseMutex(this->mLock);
}

CHAR *CStringPool::ClonePath(const CHAR *szCopyThis)
{
  // Clones a string which is expected to be a path; as such, allocate at least MAX_PATH chars
  //  so that the buffer can more than likely be reused without re-allocation later
  size_t stAlloc = (strlen(szCopyThis) < (MAX_PATH - 1)) ? MAX_PATH : (strlen(szCopyThis) + 1);
  CHAR *sz = this->AllocStr(stAlloc);
  if (sz)
    strcpy(sz, szCopyThis);
  return sz;
}
