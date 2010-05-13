#ifndef _SIMPLEODBC_H_
#define _SIMPLEODBC_H_

#include <cstdlib>
#include <vector>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <odbcss.h>
#include <stdio.h>
#include <time.h>
#include "mem_accounting.h"
// define CSIMPLEODBC_NOT_THREADSAFE on your project if you don't care about thread safety and don't want to include xthreads
#ifndef SIMPLEODBC_NOT_THREADSAFE
#include "xthreads.h"
#endif
#define DEFAULT_ODBC_DRIVER   "SQL Native Client"  // DB-enabled log.h will require this

#ifdef USE_EXTERNAL_LOGGER
#include "Log.h"
#else
#define LS_DEBUG    10
#define LS_TRIVIA   20
#define LS_NOTICE   30
#define LS_WARNING  40
#define LS_ERROR    50
void Log(unsigned int uiLevel, const CHAR *sz,...);
#endif

#ifndef NUMERIC_FORMAT_MAXSIZE
#define NUMERIC_FORMAT_MAXSIZE  32
#endif
#define MAX_CLONE_ATTEMPTS      25

class CSimpleODBC
{
  public:
    class CDataset
    {
      friend CSimpleODBC;
      public:
        CDataset();
        ~CDataset();
        const CHAR ***Rows();
        const CHAR *Field(size_t stRow, size_t stCol, const CHAR *szNullValue = "");
        const CHAR *Field(size_t stCol, const CHAR *szNullValue = "");
        const CHAR *FieldByName(size_t stRow, const CHAR *szName, const CHAR *szNullValue = "");
        const CHAR *FieldByName(const CHAR *szName, const CHAR *szNulLValue = "");
        const CHAR **Columns();
        const CHAR *ColName(size_t idx);
        int ColumnIndex(const CHAR *szName);
        size_t RowCount();
        size_t ColCount();
        size_t Pos() {return this->mstPos;};
        bool Next();
        bool Prev();
        bool First();
        bool Last();
        void Clear();
        bool EndOfDS(); // will return true after the last Next() call has reached the end of the recordset
        bool BeginningOfDS(); // opposite of EOF: returns true when the last Prev() went before the beginning of the recordset
      private:
        bool mfEOF;
        bool mfBOF;
        void SetRows(CHAR ***mszNewRows, size_t stNewRows, size_t stNewCols);
        CHAR ***maszRows;
        size_t mstRows;
        size_t mstCols;
        size_t mstPos;
        void SetInDS();
        void SetOutOfDS();
    };
    CSimpleODBC(void);
    ~CSimpleODBC(void);
    
    unsigned long mulMaxFieldLength;
    bool Debug;
    
    bool Connect(const CHAR *szConnStr = "");
    bool Connect(const CHAR *szDBHost, const CHAR *szDBName, const CHAR *szDBUser, const CHAR *szDBPass, 
      const CHAR *szDriver = NULL);
    const CHAR *ConnectionString();
    bool CloneConnection(CSimpleODBC *src);
    bool Disconnect();
    bool Connected();

    bool BeginTransaction();
    bool Commit();
    bool Rollback();
    bool Query(const CHAR *szSQL, CHAR ****aszRows = NULL, size_t *stRows = NULL, size_t *stCols = NULL, 
      bool fColNamesInFirstRow = true, size_t stResultSet_Offset = 0);
    CSimpleODBC::CDataset *Qry(const CHAR *szSQL,...);
    bool Exec(const CHAR *szSQL,...);
    bool Exec_NoSub(const CHAR *szSQL);
    const CHAR *Quote(const CHAR *szToQuote, bool fWrapInQuotesForQuery = true, size_t stMaxSizeHint = 0);
    long long GetLongVal(const CHAR *szSQL, long lngDefaultVal);
    long long GetLongVal(const CHAR *szSQL);
    long long GetLongValV(const CHAR *szSQL,...);
    unsigned long long GetULongVal(const CHAR *szSQL, long long lngDefaultVal);
    unsigned long long GetULongVal(const CHAR *szSQL);
    unsigned long long GetULongValV(const CHAR *szSQL,...);

    void GetSQLDate(CHAR *szBuf, time_t ttTimestamp = 0);
    const CHAR *GetSQLDate(bool fQuote = true, time_t ttTimestamp = 0);     // returns a string representation of the time specified (default is to return current datetime)
    const CHAR *GetSQLDateDiff(bool fQuote = true, long lngDiffSecs = 0);   // returns a string representation of the current date/time with lngDiffSecs added to it

    CHAR *AllocSQLStr(const CHAR *format,...);    // works like sprintf, returning a pointer to the char buffer that it creates with malloc()
    void FreeSQLStr(CHAR *sz);                    // frees a string allocated by AllocSQLStr
    void SetDefaultLongVal(long long lngDefaultLongVal) {this->mlngDefaultLongVal = lngDefaultLongVal;};
    long long DefaultLongVal();
    const CHAR *GetLastError() {return (this->mszLastError) ? (const CHAR *)(this->mszLastError) : "";};
    void ClearLastError() {if (this->mszLastError) {free(this->mszLastError); this->mszLastError = NULL;}};
    CHAR *AA_Sprintf(CHAR *format,...);
    void AA_Free(CHAR *sz);
    void FreeBuffer(const CHAR *sz1);
    void FreeBuffers(bool fFreeMem = false);            // free quoted string buffers (ALL OF THEM)
    void FreeBuffers(const CHAR *sz1,...);
    void DisableLogging(bool fDisabled = true);

  private:
    struct SStringBuffer {
      CHAR *Buffer;
      size_t Len;   // length of string which can be held there (buflen - 1)
      bool InUse;
      SStringBuffer()
      {
        this->Buffer = NULL;
        this->Len = 0;
        this->InUse = false;
      }
      SStringBuffer(const CHAR *szCopyThis, size_t stPadChars)
      {
        if (szCopyThis)
        {
          this->Len = 0;
          size_t stLen = strlen(szCopyThis);
          this->Buffer = (CHAR *)malloc((1 + stPadChars + stLen) * sizeof(CHAR));
          if (this->Buffer)
          {
            strcpy(this->Buffer, szCopyThis);
            this->Len = stLen + stPadChars;
          }
        }
        else
        {
          this->Buffer = (CHAR *)malloc((1 + stPadChars ) * sizeof(CHAR));
          this->Buffer[0] = '\0';
          this->Len = stPadChars;
        }
        this->InUse = true;
      }
      SStringBuffer(CHAR *szBuffer)
      {
        this->Buffer = szBuffer;
        if (szBuffer)
          this->Len = strlen(szBuffer);
        else
          this->Len = 0;
        this->InUse = true;
      }
      ~SStringBuffer()
      {
        if (this->Buffer)
          free(this->Buffer);
      }
    };
    #ifdef _XTHREADS_H
    MUTEX_TYPE mLock;
    MUTEX_TYPE mBufferLock;
    #endif
    bool mfInTransaction;
    CHAR *mszConnectionString;
    SQLHENV mhEnv;
    SQLHDBC mhDBC;
    CHAR mszDate[32];    // used to hold a current date string, to pass back via GetSQLDate();
    long long mlngDefaultLongVal;
    CHAR *mszLastError;
    size_t mstBuffers;
    size_t mstBuffersAlloc;
    struct SStringBuffer **masBuffers;
    bool mfLoggingEnabled;
    
    void SetLastError(const CHAR *szErr);
    void FreeRows(CHAR ***asz, size_t stRows, size_t stCols);
    CHAR *CSimpleODBC::GetBuffer(const CHAR *szCopy, size_t stPadChars);
    CHAR *CSimpleODBC::GetBuffer(size_t stLen);
    void CSimpleODBC::RegisterBuffer(CHAR *sz, size_t stBufLen = 0);
    bool SetAutoCommitOn();
    SQLRETURN LogLastSQLError(const CHAR *pre = NULL, HANDLE h = SQL_NULL_HANDLE, SQLSMALLINT HandleType = SQL_HANDLE_DBC,
      const CHAR *szSQL = NULL);
    void LockSelf();
    void UnlockSelf();
    void LockBuffers();
    void UnlockBuffers();
    
};

#endif
