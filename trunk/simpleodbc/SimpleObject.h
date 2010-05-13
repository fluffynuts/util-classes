/*
 * Simple "starting block" for an object, including some common functionality like re-usable
 * string buffers and a static error buffer
 */
#ifndef __SIMPLEOBJECT_H
#define __SIMPLEOBJECT_H

#ifdef WIN32
#include <windows.h>
#else
#define CHAR char
#endif

#include <stdio.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "Log.h"    // cut out if you don't have it or don't need it -- log calls should be ifdeffed in inheriting classes

class CSimpleObject
{
  public:
    CSimpleObject(void);
    ~CSimpleObject(void);
  
    const CHAR *GetLastError() {return (const CHAR *)(this->mszLastError);};
    void ClearLastError();
    void FreeBuffers();   // called as part of Deinit wind-down; frees internal buffers; can be called if the caller wants to free some mem after MANY gets

  protected:

    void SetLastError(const CHAR *szErr,...);  
    void SetStr(CHAR **szDest, const CHAR *szSrc, size_t stPad = 0, bool fCheckAllocation = true, size_t *stAllocated = NULL);
    void FreeStr(CHAR **sz);
    CHAR *GetBuffer(size_t stBuflen, const CHAR *szFill = NULL);
    void FreeBuffer(CHAR *sz);
    void ReleaseBuffer(CHAR *sz);
    void ReleaseBuffers();
    CHAR *ResizeBuffer(CHAR *sz, size_t stNewSize);
    bool AppendStr(CHAR **szDest, const CHAR *szSrc, size_t *stBufferLen);
    bool DirExists(const CHAR *szDirName);
    void RegisterBuffer(CHAR *sz);  // register a char * for auto-cleaning at destruction time
  private:
    struct SBuffer
    {
      size_t Len;
      bool InUse;
      CHAR *Buffer;
      SBuffer()
      {
        this->Len = 0;
        this->InUse = false;
        this->Buffer = NULL;
      }
      SBuffer(size_t stReqLen)
      {
        this->Len = stReqLen;
        this->Buffer = (CHAR *)malloc((stReqLen + 1) * sizeof(CHAR));
        this->InUse = true;
      }
      SBuffer(const CHAR *szCopy, size_t stPadChars = 0)
      {
        if (szCopy)
        {
          this->Len = (strlen(szCopy) + stPadChars);
          this->Buffer = (CHAR *)malloc((this->Len + 1) * sizeof(CHAR));
          strcpy(this->Buffer, szCopy);
          this->InUse = true;
        }
        else if (stPadChars)
        {
          this->Buffer = (CHAR *)malloc(stPadChars * sizeof(CHAR));
          this->Buffer[0] = '\0';   // make safe for string operations later
          this->Len = 0;
          this->InUse = true;
        }
        else
        { // paranoia
          this->Len = 1;
          this->InUse = true;
          this->Buffer = (CHAR *)malloc(sizeof(CHAR));
          this->Buffer[0] = '\0';
        }
      }
      ~SBuffer()
      {
        if (this->Buffer)
          free(this->Buffer);
      }
    };
    CHAR *mszLastError;
    struct SBuffer **masBuffers;
    size_t mstBuffers;
};

#endif
