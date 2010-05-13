#ifndef _STRINGPOOL_H_
#define _STRINGPOOL_H_
#include <stdio.h>
#include <malloc.h>
#include <windows.h>
#include <stdarg.h>
#include "xthreads.h"

/*
* simple class to manage / marshall string allocations and mem management. Supposed to be
*   used as a scoped variable, so that when the var goes out of scope, all relevant memory is cleared
*/
#define DEFAULT_MAX_RESERVE_BUFFERS   16

class CStringPool
{
  public:
    CStringPool(void);
    ~CStringPool(void);
    CHAR *CloneStr(const CHAR *szCopyThis, size_t stPadChars = 0);
    CHAR *ClonePath(const CHAR *szCopyThis);
    CHAR *AllocStr(size_t stLen);
    CHAR *AllocPath();
    void Release(CHAR *sz1, CHAR *sz2 = NULL,...);      // must make last argument in the list NULL
    void FreeUnused();
    void SetMaxReserves(unsigned int uiMaxReserveBuffers) {this->muiMaxReserveBuffers = uiMaxReserveBuffers;};
    void ReleaseAll();
    
  private:
    struct SSZPoolItem {
      CHAR *sz;
      bool fInUse;
      size_t stLen;     // always holds the max length of a string that can be in this item (so, buffer len - 1)
      SSZPoolItem *_next;
      SSZPoolItem()
      {
        this->sz = NULL;
        this->stLen = 0;
        this->fInUse = false;
        this->_next = NULL;
      }
      SSZPoolItem(size_t stLen, bool fSetInUse = true)
      {
        this->sz = (CHAR *)malloc((stLen + 1) * sizeof(CHAR));
        this->stLen = stLen;
        this->fInUse = fSetInUse;
        this->_next = NULL;
      }
      ~SSZPoolItem()
      {
        if (this->sz)
          free(this->sz);
      }
    };
    MUTEX_TYPE mLock;
    
    SSZPoolItem *pool;
    unsigned int muiMaxReserveBuffers;
    void UnrefSZ(CHAR *sz);
};

#endif

