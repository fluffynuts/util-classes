#include "SimpleODBC.h"

#if defined(WIN32) || defined(WIN64)
#ifndef atoll
#define atoll(x) _atoi64(x)
#endif
#endif

#ifndef LOG
#define LOG(str) (printf("%s\n", str))
#endif

// stolen from simplesqlite (:
#define QUERY_BASE \
  this->ClearLastError(); \
  char *pos; \
  char *szFSCopy = (char *)malloc((1 + strlen(szSQL)) * sizeof(char)); \
  strcpy(szFSCopy, szSQL); \
  char *szArg, *szBuf = NULL; \
  size_t stReqLen = strlen(szSQL); \
  va_list args; \
  va_start(args, szSQL); \
  while (pos = strstr(szFSCopy, "%")) \
  { \
    ++pos; \
    if (*pos == '%') \
    { \
      strcpy(szFSCopy, ++pos); \
      continue; \
    } \
    while (strchr("0123456789", *pos)) \
      pos++; \
    switch (*pos) \
    { \
      case '.': \
      case 'f': \
      { \
        va_arg(args, double); \
        stReqLen += NUMERIC_FORMAT_MAXSIZE; \
        break; \
      } \
      case 's': \
      { \
        szArg = va_arg(args, char*); \
        if (szArg) \
          stReqLen += strlen(szArg); \
        else \
          stReqLen += 128; \
        break; \
      } \
      case 'u': \
      { \
        va_arg(args, unsigned int); \
        stReqLen += NUMERIC_FORMAT_MAXSIZE;\
        break; \
      } \
      case 'l': \
      { \
        char *pos2 = pos; \
        pos2++; \
        switch (*pos2) \
        { \
          case 'l': \
          { \
            pos2++; \
            switch (*pos2) \
            { \
              case 'u': \
              { \
                va_arg(args, unsigned long long); \
                break; \
              } \
              case 'i': \
              default: \
              { \
                va_arg(args, long long); \
              } \
            } \
            break; \
          } \
          case 'u': \
          { \
            va_arg(args, unsigned long); \
            break; \
          } \
          case 'i': \
          case 'd': \
          default: \
          { \
            va_arg(args, long); \
          } \
        } \
        stReqLen += NUMERIC_FORMAT_MAXSIZE; \
        break; \
      } \
      case 'i': \
      default: \
      { \
        va_arg(args, int); \
        stReqLen += NUMERIC_FORMAT_MAXSIZE; \
      } \
    } \
    strcpy(szFSCopy, pos); \
  } \
  va_end(args); \
  va_start(args, szSQL); \
  szBuf = (char *)malloc((1 + stReqLen) * sizeof(char)); \
  szBuf[0] = '\0';\
  vsprintf(szBuf, szSQL, args); \
  va_end(args); \
  free(szFSCopy);
  // at this point, szBuf should contain the full query

CSimpleODBC::CSimpleODBC(void)
{
  this->mfLoggingEnabled = true;
  this->mstBuffers = 0;
  this->mstBuffersAlloc = 0;
  this->masBuffers = NULL;
  this->Debug = false;
  this->mlngDefaultLongVal = 0;
  this->mhEnv = SQL_NULL_HANDLE;
  this->mhDBC = SQL_NULL_HANDLE;
  // default max field length
  this->mulMaxFieldLength = 8196;
  this->mszConnectionString = NULL;
  this->mszLastError = NULL;
  #ifdef _XTHREADS_H
  this->mLock = XT_CreateMutex();
  this->mBufferLock = XT_CreateMutex();
  #endif
  this->mfInTransaction = false;
  
  // get environment handle
  RETCODE ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &(this->mhEnv));
  switch (ret)
  {
    case SQL_SUCCESS:
    case SQL_SUCCESS_WITH_INFO:
    {
      break;
    }
    case SQL_INVALID_HANDLE:
    case SQL_ERROR:
    default:
    {
      this->SetLastError("Unable to allocate SQL environment handle");
      return;
    }
  }
  // set odbc version 3
  ret = SQLSetEnvAttr(this->mhEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, SQL_IS_INTEGER);
  switch (ret)
  {
    case SQL_ERROR:
    case SQL_INVALID_HANDLE:
    {
      this->SetLastError("Unable to set ODNC v3 environment");
      SQLFreeHandle(SQL_HANDLE_ENV, this->mhEnv);
      this->mhEnv = SQL_NULL_HANDLE;
      return;
    }
  }
}

CSimpleODBC::~CSimpleODBC(void)
{
  // auto clean up on destruction
  this->Disconnect();
  if (this->mhDBC != SQL_NULL_HANDLE)
    SQLFreeHandle(SQL_HANDLE_DBC, this->mhDBC);
  if (this->mhEnv != SQL_NULL_HANDLE)
    SQLFreeHandle(SQL_HANDLE_ENV, this->mhEnv);
  if (this->mszConnectionString)
  {
    free(this->mszConnectionString);
    this->mszConnectionString = NULL;
  }
  
  this->ClearLastError();
  for (size_t i = 0; i < this->mstBuffers; i++)
    delete this->masBuffers[i];
  if (this->masBuffers)
    free(this->masBuffers);
  this->mstBuffers = 0;
  #ifdef _XTHREADS_H
  XT_DestroyMutex(this->mLock);
  XT_DestroyMutex(this->mBufferLock);
  #endif
}

bool CSimpleODBC::Connect(const CHAR *szDBHost, 
  const CHAR *szDBName, 
  const CHAR *szDBUser, 
  const CHAR *szDBPass, 
  const CHAR *szDriver)
{
  // simplest way to connect to a database
  CHAR *szDriverCopy;
  if ((szDriver == NULL) || (strlen(szDriver) == 0))
    szDriverCopy = this->AA_Sprintf("%s", DEFAULT_ODBC_DRIVER);
  else
    szDriverCopy = this->AA_Sprintf("%s", szDriver);

  if (this->mszConnectionString)
  {
    free(this->mszConnectionString);
    this->mszConnectionString = NULL;
  }
    
  if (szDBUser && strlen(szDBUser))
    this->mszConnectionString = this->AA_Sprintf("Driver={%s};Server=%s;Database=%s;Uid=%s;Pwd=%s", szDriverCopy, szDBHost, szDBName, szDBUser, szDBPass);
  else
    this->mszConnectionString = this->AA_Sprintf("Driver={%s};Server=%s;Database=%s;Trusted_Connection=yes", szDriverCopy, szDBHost, szDBName);
  
  bool fRet = this->Connect(this->mszConnectionString);
  
  this->AA_Free(szDriverCopy);
  
  return fRet;
}

void CSimpleODBC::DisableLogging(bool fDisabled)
{
  this->mfLoggingEnabled = !fDisabled;
}

CHAR *CSimpleODBC::AA_Sprintf(CHAR *szSQL,...)    // szSQL name abused to re-use QUERY_BASE macro
{
  QUERY_BASE;
  return szBuf;
}

void CSimpleODBC::AA_Free(CHAR *sz)
{
  if (sz)
    free(sz);
}

bool CSimpleODBC::Connect(const CHAR *szConnStr)
{
  // only use this if you actually have a well-formed ODBC connection string
  //  note that "ODBC connection string" != "OLEDB connection string"
  //  -- using the output from the standard windows UDL editor / creator here WILL NOT WORK.
  this->Disconnect();
  this->LockSelf();

  // get db connection handle
  RETCODE ret = SQLAllocHandle(SQL_HANDLE_DBC, this->mhEnv, &(this->mhDBC));
  if (ret == SQL_INVALID_HANDLE || ret == SQL_ERROR)
  {
    this->SetLastError("Unable to allocate DBC handle");
    this->mhDBC = SQL_NULL_HANDLE;
    this->UnlockSelf();
    return false;
  }

  if (strlen(szConnStr) && (szConnStr != this->mszConnectionString))
  {
    this->mszConnectionString = (CHAR *)realloc(this->mszConnectionString, (1 + strlen(szConnStr)) * sizeof(CHAR));
    strcpy(this->mszConnectionString, szConnStr);
  }
  if ((this->mszConnectionString == NULL) || (strlen(this->mszConnectionString) == 0))
  {
    this->UnlockSelf();
    return false;
  }
  if (this->mhEnv == SQL_NULL_HANDLE || this->mhDBC == SQL_NULL_HANDLE)
  {
    this->UnlockSelf();
    return false;
  }
  
  bool fRet = false;
  SQLSMALLINT ssiBufLen = (SQLSMALLINT)strlen(this->mszConnectionString) + 1;
  SQLCHAR *scConnString = (SQLCHAR *)malloc(ssiBufLen * sizeof(SQLCHAR));
  SQLCHAR *scOutString = (SQLCHAR *)malloc(ssiBufLen * sizeof(SQLCHAR));
  strcpy((char *)scConnString, this->mszConnectionString);
  SQLSMALLINT ssiOutLen = 0;
  ret = SQLDriverConnect(this->mhDBC, NULL, scConnString, 
    ssiBufLen, scOutString, ssiBufLen, &(ssiOutLen), SQL_DRIVER_NOPROMPT);
  switch (ret)
  {
    case SQL_SUCCESS:
    case SQL_SUCCESS_WITH_INFO:
    {
      #ifdef _LOG_H_
      if (this->Debug && this->mfLoggingEnabled)
        Log(LS_NOTICE, "Connected to database");
      #endif
      fRet = true;
      break;
    }
    default:
    {
      SQLFreeHandle(SQL_HANDLE_DBC, this->mhDBC);
      this->mhDBC = NULL; // mark as not connected
      this->LogLastSQLError("Unable to connect to database");
      if (this->mfLoggingEnabled)
        Log(LS_NOTICE, "Connection string: %s", (const CHAR *)scConnString);
    }
  }
  this->UnlockSelf();
  free(scConnString);
  free(scOutString);
  return fRet;
}

SQLRETURN CSimpleODBC::LogLastSQLError(const CHAR *pre, HANDLE h, SQLSMALLINT HandleType, const CHAR *szSQL)
{
  unsigned int n = 0;
  RETCODE retcode = 0;
  if (h == SQL_NULL_HANDLE)
    h = this->mhDBC;
  if (pre)
    Log(LS_ERROR, "%s", pre);
  do
  {
    // something isn't working - query failed... log reasoning
    SQLCHAR sql_state[10] = {0};
    SQLINTEGER NativeError = 0;
    SQLCHAR sql_message[255] = {0};
    SQLSMALLINT required = 0;
    retcode = SQLGetDiagRec(HandleType, h, ++n, sql_state, &NativeError, sql_message, 255, &required);
    if (this->mfLoggingEnabled)
    {
      if (strlen((const char *)sql_message))
        Log(LS_ERROR, "\n%s\n\n%s", (const char *)sql_state, (const char *)sql_message);
      if (szSQL)
        Log(LS_ERROR, "SQL was:\n%s", szSQL);
    }
  } while (retcode == SQL_SUCCESS);
  return retcode;
}

bool CSimpleODBC::Disconnect()
{
  bool ret = false;
  this->LockSelf();
  if (this->mhDBC != SQL_NULL_HANDLE)
  {
    switch (SQLDisconnect(this->mhDBC))
    {
      case SQL_SUCCESS:
      case SQL_SUCCESS_WITH_INFO:
      {
        ret = true;
      }
    }
    SQLFreeHandle(SQL_HANDLE_DBC, this->mhDBC);
    this->mhDBC = SQL_NULL_HANDLE;
  }
  else
  {
    this->UnlockSelf();
    return true;    // not connected to start with
  }
  if (this->mfLoggingEnabled)
    Log(LS_DEBUG, "Disconnected from database");
  if (this->mszConnectionString)
  {
    free(this->mszConnectionString);
    this->mszConnectionString = NULL;
  }
  this->UnlockSelf();
  return ret;
}

bool CSimpleODBC::Query(const CHAR *szSQL, CHAR ****aszRows, size_t *stRows, size_t *stCols, 
  bool fColnamesInFirstRow, size_t stResultSet_Offset)
{
  /*
  * Queries the database with a provided SQL string; can also use for inserts etc
  * NOTE: ODBC DOES NOT LIKE PERFORMING TRANSACTION BEGIN/COMMIT/ROLLBACK here. Use the approprate
  *   CSimpleODBC functions rather (BeginTransacion(), Commit(), Rollback())
  * inputs: szSQL:    SQL to execute
  *         aszRows:  pointer to an array of CHAR * arrays; the pointed value will be set to NULL if there is no data
  *                   (optional -- when NULL (default) is passed, no data retrieval is done; nice for insert / update)
  *         stRows:   pointer to unsigned long var to hold number of rows in result set
  *                   (optional -- perhaps you know how many rows you will get FOR SURE?)
  *         stCols:   pointer to unsigned long var to hold number of columns in result set
  *                   (optional -- a well-crafted, specific SQL statement should know the number of cols returned)
  *         stResultSet_Offset: offset of the result set the caller is interested in. This really only applies to multi-return
  *                   statements where the result set of interest IS NOT the first non-empty one
  * return value: true if no error; false when some kind of error arises
  * NB: when you are done with the returned rows, call CSimpleODBC::FreeRows on them, passing the array of CHAR * arrays
  *         as well as the number of rows and columns in the result set. Not doing so will result in a leaky app.
  */
  
  if (!szSQL)
    return false;
  if (this->mfLoggingEnabled)
    Log(LS_DEBUG, "Running sql: %s", szSQL);
  
  if (this->mhDBC == SQL_NULL_HANDLE)
  {
    if (!this->Connect())
    {
      if (this->mfLoggingEnabled)
        Log(LS_ERROR, "Can't perform query: not connected");
      return false;
    }
  }
  
  size_t rows = 0, cols = 0;
  if (aszRows)
    *aszRows = NULL;
  
  bool fDeadLock;
  unsigned int uiDeadlockAttempts = 0;
  do
  {
    fDeadLock = false;
    SQLHSTMT hStatement = SQL_NULL_HSTMT;
    this->LockSelf();
    RETCODE retcode = SQLAllocHandle(SQL_HANDLE_STMT, this->mhDBC, &hStatement);
    
    if (hStatement == SQL_NULL_HSTMT)
    {
      this->UnlockSelf();
      return false;
    }
    
    SQLINTEGER cbLen;
    if ((retcode = SQLPrepare(hStatement, (SQLCHAR *)(szSQL), SQLINTEGER(strlen(szSQL)))) != SQL_SUCCESS)
    {
      // bail out...
      this->UnlockSelf();
      return false;
    }
    retcode = SQLExecute(hStatement);
    switch (retcode)
    {
      case SQL_NO_DATA: // don't believe the lying server; there may be results that have come from executing a string -- they will be "next"
      case SQL_SUCCESS:
      case SQL_SUCCESS_WITH_INFO:
      {
        if (aszRows == NULL)
          break;
        for (unsigned int i = 0; i < stResultSet_Offset; i++)    // advance the result set pointer
        {
          if (SQLMoreResults(hStatement) != SQL_SUCCESS)
            break;
        }
        // get the records back into the string grid
        while (true)
        {
          retcode = SQLFetch(hStatement);
          if (retcode != SQL_SUCCESS)
          {
            SQLRETURN moreResults = SQLMoreResults(hStatement);
            if (moreResults == SQL_SUCCESS)
              continue;
            if (moreResults == SQL_STILL_EXECUTING)
            {
              Sleep(50);  // prevent mad cpu overload
              continue;
            }
            break;
          }
          
          RETCODE retCol = SQL_SUCCESS;
          SQLSMALLINT tmpCols;
          retCol = SQLNumResultCols(hStatement, &tmpCols);
          if (retCol != SQL_SUCCESS)
          {
            this->UnlockSelf();
            return false;
          } 
          cols = tmpCols;
          CHAR **aszColData = (CHAR **)malloc(cols * sizeof(CHAR*));
          
          char *szTmp = (CHAR *)malloc(this->mulMaxFieldLength * sizeof(CHAR));
          if (fColnamesInFirstRow)
          {
            rows++;
            *aszRows = (CHAR ***)malloc(sizeof(CHAR**));
            (*aszRows)[0] = aszColData;
            // get column names into first row
            SQLSMALLINT ssiLen;
            for (size_t i = 0; i < cols; i++)
            {
              if (SQLColAttribute(hStatement, SQLUSMALLINT(i+1), SQL_DESC_NAME, SQLPOINTER(szTmp), 
                SQLSMALLINT(this->mulMaxFieldLength), &ssiLen, NULL) == SQL_SUCCESS)
              {
                aszColData[i] = (CHAR *)malloc((1 + ssiLen) * sizeof(CHAR));
                strcpy(aszColData[i], szTmp);
              }
            }
          }
          else
            *aszRows = NULL;  // make sure reallocs work later down
          size_t stRowAlloc = rows;
          SQLUSMALLINT foo = SQL_C_CHAR;
          while (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
          {
            if ((stRowAlloc - rows) < 1)
            {
              stRowAlloc += 1024;
              *aszRows = (CHAR ***)realloc(*aszRows, stRowAlloc * sizeof(CHAR ***));
              if (*aszRows == NULL)
              {
                this->LogLastSQLError("Unable to allocate memory for result return");
                SQLFreeHandle(SQL_HANDLE_STMT, hStatement);
                this->UnlockSelf();
                return false;
              }
            }
            aszColData = (CHAR **)malloc(cols * sizeof(CHAR *));
            for (size_t i = 0; i < cols; i++)
            {
              szTmp[0] = '\0';
              retCol = SQLGetData(hStatement, SQLUSMALLINT(i + 1), SQL_C_CHAR, szTmp,
                this->mulMaxFieldLength, &cbLen);
              if (retCol == SQL_SUCCESS || retCol == SQL_SUCCESS_WITH_INFO)
              {
                aszColData[i] = (CHAR *)_MALLOC((1 + strlen(szTmp)) * sizeof(CHAR));
                strcpy(aszColData[i], szTmp);
              }
              else if (this->mfLoggingEnabled)
                Log(LS_ERROR, "Unable to retrieve column %u of row %u for query", i, rows);
            }
            rows++;
            (*aszRows)[rows - 1] = aszColData;
            retcode = SQLFetch(hStatement);
          }
          if (*aszRows)
            *aszRows = (CHAR ***)realloc(*aszRows, rows * sizeof(CHAR***));
          free(szTmp);
          
          if (rows || cols)   // already have the first result set (may be zero rows, but cols were returned)
            break;
          else if (SQLMoreResults(hStatement) != SQL_SUCCESS)
          {
            this->LogLastSQLError("SQLMoreResults", hStatement, SQL_HANDLE_STMT);
            break;    // break out here
          }
        }
        break;
      }
      case SQL_ERROR:
      {
        int n = 0;
        // this will be a mess in mt-environments;
        //CHAR *szErr = (CHAR *)malloc((128 + strlen(szSQL)) * sizeof(CHAR));
        SQLRETURN sqlErr = 0;
        #ifdef _LOG_H_
        if (GetLogLevel() > LS_DEBUG)
        {
          if (this->mfLoggingEnabled)
            Log(GetLogLevel(), "Error when running sql:\n%s\n", szSQL);
          sqlErr = this->LogLastSQLError(NULL, hStatement, SQL_HANDLE_STMT);
        }
        else
        #endif
          sqlErr = this->LogLastSQLError("Error running last sql statement:", hStatement, SQL_HANDLE_STMT, szSQL);
        if (sqlErr == 0)
        {
          do
          {
            SQLCHAR sql_state[10] = {0};
            SQLINTEGER NativeError = 0;
            SQLCHAR sql_message[255] = {0};
            SQLSMALLINT required = 0;
            retcode = SQLGetDiagRec(SQL_HANDLE_DBC, hStatement, ++n, sql_state, &NativeError, sql_message, 255, &required);
            if (atol((const char *)sql_state) == 40001)
            {
              fDeadLock = true;
              break;
            }
          } while (retcode == SQL_SUCCESS);
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hStatement);
        this->UnlockSelf();
        return false;
        break;    // AR
      }
      default:
      {
        int n = 0;
        this->LogLastSQLError("Unknown error", hStatement, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hStatement);
        this->UnlockSelf();
        return false;
      }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hStatement);
    this->UnlockSelf();
    
    if (stRows)
      *stRows = rows;
    if (stCols)
      *stCols = cols;
    if (fDeadLock)
    {
      Log(LS_WARNING, "Deadlock causes query failure; will try again in a second...");
      Sleep(1000);
    }
  } while (fDeadLock && (uiDeadlockAttempts++ < 10));
  return true;
}

const CHAR *CSimpleODBC::Quote(const CHAR *szToQuote, bool fWrapInQuotesForQuery, size_t stMaxSizeHint)
{
  /*
  * quotes strings to make them safe for SQL data; optionally quotes the entire string for
  *   insertion; also optionally handles mem allocation; default handles mem allocation with realloc()
  */
  if (szToQuote == NULL)
  {
    CHAR *ret = this->GetBuffer(8);
    sprintf(ret, "(null)");
    return (const CHAR *)ret;
  }
  size_t stSQLLen = strlen(szToQuote);
  size_t stBufSize = strlen(szToQuote) + 12;
  if (stBufSize < stMaxSizeHint)
    stBufSize = stMaxSizeHint;    // allow the caller to give a buffer size hint for re-use
  CHAR *ret = (CHAR *)malloc(stBufSize * sizeof(CHAR));
  strcpy(ret, szToQuote);
  
  // replace all ' with ''
  CHAR *pos = strchr(ret, '\'');
  CHAR *szTmp = NULL;
  bool fQuoted = false;
  while (pos)
  {
    fQuoted = true;
    if ((stBufSize - stSQLLen) < 2)
    {
      size_t stOffset = strlen(ret) - strlen(pos);
      stBufSize += 128;
      ret = (CHAR *)realloc(ret, stBufSize * sizeof(CHAR));
      pos = ret + stOffset * sizeof(CHAR);
    }
    pos++;
    if (szTmp == NULL)
      szTmp = this->GetBuffer(strlen(pos));
    strcpy(szTmp, pos);
    *pos = '\'';
    strcpy(++pos, szTmp);
    this->FreeBuffer(szTmp);
    szTmp = NULL;
    pos = strchr(pos, '\'');
    stSQLLen = strlen(ret);
  }
  if (szTmp)
    this->FreeBuffer(szTmp);
  if (!fQuoted && fWrapInQuotesForQuery)
  {
    size_t stBufLen = strlen(szToQuote) + 3;
    if (stBufLen < stMaxSizeHint)
      stBufLen = stMaxSizeHint;
    szTmp = this->GetBuffer(stBufLen);
    sprintf(szTmp, "'%s'", szToQuote);
    free(ret);
    ret = szTmp;
    szTmp = NULL;
  }
  else
  {
    if (!fQuoted)
      strcpy(ret, szToQuote);
  
    if (fWrapInQuotesForQuery)
    {
      size_t stBufLen = strlen(ret) + 3;
      if (stBufLen < stMaxSizeHint)
        stBufLen = stMaxSizeHint;
      szTmp = this->GetBuffer(stBufLen);
      sprintf(szTmp, "'%s'", ret);
      free(ret);
      ret = szTmp;
      szTmp = NULL;
    }
    else
      this->RegisterBuffer(ret, stBufSize);
  }

  return (const CHAR *)ret;
}


long long CSimpleODBC::GetLongVal(const CHAR *szSQL, long lngDefaultVal)
{
  CHAR ***rows = NULL;
  size_t stRows = 0, stCols = 0;
  
  long long ret = lngDefaultVal;
  if (this->Query(szSQL, &rows, &stRows, &stCols, false))
  {
    if (stRows && stCols)
    {
      ret = atoll(rows[0][0]);
      this->FreeRows(rows, stRows, stCols);
    }
  }
  return ret;
}

unsigned long long CSimpleODBC::GetULongVal(const CHAR *szSQL, long long lngDefaultVal)
{
  CHAR ***rows = NULL;
  size_t stRows = 0, stCols = 0;
  
  unsigned long long ret = (unsigned long long)lngDefaultVal;
  if (this->Query(szSQL, &rows, &stRows, &stCols, false))
  {
    if (stRows && stCols)
    {
      CHAR *pos;
      ret = (unsigned long long)strtod(rows[0][0], &pos);
      if (pos == rows[0][0])
        ret = lngDefaultVal;    // couldn't convert to a number...
      this->FreeRows(rows, stRows, stCols);
    }
  }
  return ret;
}


void CSimpleODBC::FreeRows(CHAR ***asz, size_t stRows, size_t stCols)
{
  if (asz && stRows)
  {
    for (size_t i = 0; i < stRows; i++)
    {
      for (size_t j = 0; j < stCols; j++)
        free(asz[i][j]);
      free(asz[i]);
    }
    free(asz);
  }
}

long long CSimpleODBC::GetLongVal(const CHAR *szSQL)
{
  return (long long)this->GetLongVal(szSQL, (long)this->mlngDefaultLongVal);
}

unsigned long long CSimpleODBC::GetULongVal(const CHAR *szSQL)
{
  return this->GetULongVal(szSQL, this->mlngDefaultLongVal);
}

bool CSimpleODBC::Exec_NoSub(const CHAR *szSQL)
{
  CHAR ***rows = NULL;
  size_t stRows = 0;
  size_t stCols = 0;
  bool res = this->Query(szSQL, &rows, &stRows, &stCols);
  if (!res)
    return false;
  this->FreeRows(rows, stRows, stCols);   // just in case the exec actually returned anything
  return true;
}

bool CSimpleODBC::Exec(const CHAR *szSQL,...)
{
  QUERY_BASE;
  
  CHAR ***rows = NULL;
  size_t stRows = 0;
  size_t stCols = 0;
  bool res = this->Query(szBuf, &rows, &stRows, &stCols);
  if (szBuf)
    free(szBuf);
  if (!res)
    return false;
  this->FreeRows(rows, stRows, stCols);   // just in case the exec actually returned anything
  return true;
}

CSimpleODBC::CDataset *CSimpleODBC::Qry(const CHAR *szSQL,...)
{
/*
* Query with optional string formatting -- be aware that this function does not escape
*   quotes for you -- caller needs to prepare any string formatting appropriately
*   also, this only returns the first dataset -- if you want something else, you will have to
*   make use of the more arcane ::Query() function
*/
//bool CSimpleODBC::Query(const CHAR *szSQL, CHAR ****aszRows, unsigned long *ulRows, unsigned long *ulCols, unsigned int uiResultSet_Offset)
  QUERY_BASE;

  CHAR ***rows = NULL;
  size_t stRows = 0, stCols = 0;
  bool res = this->Query(szBuf, &rows, &stRows, &stCols);
  if (szBuf)
    free(szBuf);
  if (!res)
    return NULL;
    
  CSimpleODBC::CDataset *dg = new CSimpleODBC::CDataset();
  dg->SetRows(rows, stRows, stCols);
  return dg;
}

void CSimpleODBC::GetSQLDate(CHAR *szBuf, time_t ttTimestamp)
{
  /*
  * Gets a sql-friendly date string into szBuf. szBuf must be at least 20 chars long.
  */
  if (ttTimestamp == 0)
    ttTimestamp = time(NULL);
  struct tm *mytm;
  mytm = localtime(&ttTimestamp);
  
  strftime(szBuf, 20, "%Y-%m-%d %H:%M:%S", mytm);
    
}

const CHAR *CSimpleODBC::GetSQLDateDiff(bool fQuote, long lngDiffSecs)
{
  time_t ttTimetamp = time(NULL) + lngDiffSecs;
  return this->GetSQLDate(fQuote, ttTimetamp);
}

const CHAR *CSimpleODBC::GetSQLDate(bool fQuote, time_t ttTimestamp)
{
  CHAR *tmp = (CHAR *)malloc(32 * sizeof(CHAR));
  this->GetSQLDate(tmp, ttTimestamp);
  strcpy(this->mszDate, tmp);
  free(tmp);
  return this->Quote(this->mszDate, fQuote);
}

CHAR *CSimpleODBC::AllocSQLStr(const CHAR *format,...)    // works like sprintf, returning a pointer to the char buffer that it creates with malloc()
{
  CHAR *pos;
  CHAR *szFSCopy = (CHAR *)malloc((1 + strlen(format)) * sizeof(CHAR));
  strcpy(szFSCopy, format);
  CHAR *szArg, *szBuf;
  size_t stReqLen = strlen(format);
  va_list args;
  va_start(args, format);
  while (pos = strstr(szFSCopy, "%"))
  {
    ++pos;
    if (*pos == '%')
    {
      strcpy(szFSCopy, ++pos);
      continue;
    }
    
    szArg = va_arg(args, char*);
    if (*pos == 's')
      stReqLen += strlen(szArg);
    else
      stReqLen += NUMERIC_FORMAT_MAXSIZE;
    strcpy(szFSCopy, pos);
  }
  va_end(args);
  
  va_start(args, format);
  szBuf = (CHAR *)malloc((1 + stReqLen) * sizeof(CHAR));
  vsprintf(szBuf, format, args);
  va_end(args);
  free(szFSCopy);
  
  return szBuf;
}

void CSimpleODBC::FreeSQLStr(CHAR *sz)                    // frees a string allocated by AllocSQLStr
{ 
  // do we need some boiler-plating here? will the caller be stupid enough to double-free?
  free(sz);
}

long long CSimpleODBC::GetLongValV(const CHAR *szSQL,...)
{
  /*
  * Convenience function which takes a format string and variable args, just like printf, and returns the first
  *   long value that the result set from the query returns. If there is no result set, then the default long
  *   value (0, or set by SetDefaultLongVal()) is returned. Nice for ID lookups or insert procs with 
  *   'select @@identity' at the end.
  */
  QUERY_BASE;
  
  long long ret = this->GetLongVal(szBuf, (long)this->mlngDefaultLongVal);
  
  if (szBuf)
    free(szBuf);
  
  return ret;
}

unsigned long long CSimpleODBC::GetULongValV(const CHAR *szSQL,...)
{
  /*
  * Convenience function which takes a format string and variable args, just like printf, and returns the first
  *   long value that the result set from the query returns. If there is no result set, then the default long
  *   value (0, or set by SetDefaultLongVal()) is returned. Nice for ID lookups or insert procs with 
  *   'select @@identity' at the end.
  */
  QUERY_BASE;

  unsigned long long ret = (unsigned long long)this->GetLongVal(szBuf, (long)this->mlngDefaultLongVal);
  
  if (szBuf)
    free(szBuf);
  
  return ret;
}

void CSimpleODBC::LockBuffers()
{
  #ifdef _XTHREADS_H
  XT_LockMutex(this->mBufferLock);
  #endif
}

void CSimpleODBC::UnlockBuffers()
{
  #ifdef _XTHREADS_H
  XT_ReleaseMutex(this->mBufferLock);
  #endif
}

void CSimpleODBC::SetLastError(const CHAR *szErr)
{
  this->LockBuffers();
  size_t stErrLen = strlen(szErr);
  if (this->mszLastError)
  {
    if (strlen(this->mszLastError) < stErrLen)
      this->mszLastError = (CHAR *)realloc(this->mszLastError, (stErrLen + 1) * sizeof(CHAR));
  }
  else
    this->mszLastError = (CHAR *)malloc((stErrLen + 1) * sizeof(CHAR));
  strcpy(this->mszLastError, szErr);
  this->UnlockBuffers();
  if (this->Debug)
    LOG(szErr);
}

CHAR *CSimpleODBC::GetBuffer(const CHAR *szCopy, size_t stPadChars)
{
  CHAR *szDest = this->GetBuffer(strlen(szCopy) + stPadChars);
  strcpy(szDest, szCopy);
  return szDest;
}

CHAR *CSimpleODBC::GetBuffer(size_t stLen)
{
  bool fHaveBuffer = false;
  CHAR *ret = NULL;
  this->LockBuffers();
  SStringBuffer *sbuf;
  for (size_t i = 0; i < this->mstBuffers; i++)
  {
    sbuf = this->masBuffers[i];
    if (!sbuf->InUse && (sbuf->Len >= stLen))
    {
      sbuf->InUse = true;
      this->UnlockBuffers();
      return sbuf->Buffer;
    }
  }
  if ((this->mstBuffersAlloc - this->mstBuffers) < 2)
  {
    if (this->mstBuffersAlloc)
    {
      size_t stInc = this->mstBuffersAlloc / 2;
      if (stInc > 1024)
        stInc = 1024;
      this->mstBuffersAlloc += stInc;
    }
    else
      this->mstBuffersAlloc = 128;
    this->masBuffers = (struct SStringBuffer **)realloc(this->masBuffers, this->mstBuffersAlloc * sizeof(struct SStringBuffer *));
  }
  sbuf = new SStringBuffer(NULL, stLen);
  this->masBuffers[this->mstBuffers++] = sbuf;
  this->UnlockBuffers();
  return sbuf->Buffer;
}

void CSimpleODBC::FreeBuffer(const CHAR *sz)
{
  struct SStringBuffer *s1, *s2;
  this->LockBuffers();
  for (size_t i = 0; i < this->mstBuffers; i++)
  {
    s1 = this->masBuffers[i];
    if (s1->Buffer == sz)
    {
      s1->InUse = false;
      // move to the top of the used pile to make the next search for an open slot quicker (if possible)
      for (size_t j = 0; j < i; j++)
      {
        s2 = this->masBuffers[j];
        if (s2->InUse)
        {
          this->masBuffers[i] = s2;
          this->masBuffers[j] = s1;
          this->UnlockBuffers();
          return;
        }
      }
    }
  }
  this->UnlockBuffers();
}

void CSimpleODBC::FreeBuffers(const CHAR *sz1,...)
{
  if (sz1 != NULL)
  {
    SStringBuffer *s1, *s2;
    this->LockBuffers();
    for (size_t i = 0; i < this->mstBuffers; i++)
    {
      s1 = this->masBuffers[i];
      va_list args;
      va_start(args, sz1);
      const CHAR *arg = sz1;
      do {
        if (s1->Buffer == arg)
        {
          s1->InUse = false;
          for (size_t j = 0; j < i; j++)
          {
            s2 = this->masBuffers[j];
            if (s2->InUse)
            {
              this->masBuffers[i] = s2;
              this->masBuffers[j] = s1;
              break;
            }
          }
        }
      }
      while ((arg = va_arg(args, const CHAR *)) != NULL);
      va_end(args);
    }
    this->UnlockBuffers();
  }
}

void CSimpleODBC::FreeBuffers(bool fFreeMem)
{ // you may call this throughout your apps progress to dereference the memory allocated
  //  via Quote() calls. You don't have to though unless you intend to do a heck of a lot
  //  of quoting and don't destroy the CSimpleODBC object. All buffers are automagically
  //  freed at destructor time. Setting fFreeMem to true will also only retain up to 128
  //  buffer slots; leaving it as false just dereferences buffers so they can be re-used
  //  (which is faster for your app, at the cost of memory)
  
  this->LockBuffers();
  if (this->masBuffers)
  {
    // free buffers which have gotten out of control (:
    if (fFreeMem)
    {
      if (this->mstBuffers >= 128)
      {
        for (size_t i = 0; i < this->mstBuffers; i++)
        {
          this->masBuffers[i]->InUse = false;
          if (i == 127)
            break;
        }
        for (size_t i = 128; i < this->mstBuffers; i++)
          delete this->masBuffers[i];
        this->mstBuffers = 128;
      }
    }
    else
    {
      for (size_t i = 0; i < this->mstBuffers; i++)
        this->masBuffers[i]->InUse = false;
    }
  }
  this->UnlockBuffers();
}

void CSimpleODBC::RegisterBuffer(CHAR *sz, size_t stBufLen)
{
  this->LockBuffers();
  if ((this->mstBuffersAlloc - this->mstBuffers) < 2)
  {
    if (this->mstBuffersAlloc)
    {
      size_t stInc = this->mstBuffersAlloc / 2;
      if (stInc > 1024)
        stInc = 1024;
      this->mstBuffersAlloc += stInc;
    }
    else
      this->mstBuffersAlloc = 128;
    this->masBuffers = (struct SStringBuffer **)realloc(this->masBuffers, 
      this->mstBuffersAlloc * sizeof(struct SStringBuffer *));
  }
  this->masBuffers[this->mstBuffers++] = new struct SStringBuffer(sz);
  if (stBufLen && (stBufLen > strlen(sz)))
    this->masBuffers[this->mstBuffers-1]->Len = stBufLen;
  this->UnlockBuffers();
}

bool CSimpleODBC::BeginTransaction()
{
  if (!this->Connected())
    return false;
  this->LockSelf();
  this->mfInTransaction = true;
  RETCODE ret = SQLSetConnectAttr(this->mhDBC, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
  this->UnlockSelf();
  if (ret == SQL_SUCCESS)
    return true;
  this->LogLastSQLError("Can't start transaction");
  return false;
}

bool CSimpleODBC::Commit()
{
  if (!this->Connected())
    return false;
  RETCODE ret = SQLEndTran(SQL_HANDLE_DBC, this->mhDBC, SQL_COMMIT);
  this->mfInTransaction = false;
  this->UnlockSelf();
  this->SetAutoCommitOn();
  if (ret == SQL_SUCCESS)
    return true;
  this->LogLastSQLError("Can't commit transaction");
  return false;
}

bool CSimpleODBC::Rollback()
{
  if (!this->Connected())
    return false;
    
  RETCODE ret = SQLEndTran(SQL_HANDLE_DBC, this->mhDBC, SQL_ROLLBACK);
  this->mfInTransaction = false;
  this->UnlockSelf();
  this->SetAutoCommitOn();
  if (ret == SQL_SUCCESS)
    return true;
  this->LogLastSQLError("Can't rollback transaction");
  return false;
}

bool CSimpleODBC::SetAutoCommitOn()
{
  if (!this->Connected())
    return false;
  this->LockSelf();
  RETCODE ret = SQLSetConnectAttr(this->mhDBC, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
  this->UnlockSelf();
  if (ret == SQL_SUCCESS)
    return true;
  this->LogLastSQLError("Can't set auto-commit on");
  return false;
}

bool CSimpleODBC::Connected()
{
  this->LockSelf();    // maybe not necessary...
  bool fRet = (this->mhDBC) ? true : false;
  this->UnlockSelf();
  return fRet;
}

long long CSimpleODBC::DefaultLongVal()
{
  return this->mlngDefaultLongVal;
}

const CHAR *CSimpleODBC::ConnectionString()
{
  this->LockSelf();
  const CHAR *ret = (const CHAR *)(this->mszConnectionString);
  this->UnlockSelf();
  return ret;
}

bool CSimpleODBC::CloneConnection(CSimpleODBC *src)
{ 
  if (src->ConnectionString())
  {
    for (int i = 0; i < MAX_CLONE_ATTEMPTS; i++)
    {
      if (this->Connect(src->ConnectionString()))
        return true;
      Sleep(1000);
    }
  }
  else
    this->LogLastSQLError("Can't clone connection: src doesn't have a connection string (probably not connected...)");
  return false;
}

void CSimpleODBC::LockSelf()
{
  #ifdef _XTHREADS_H
  if (!this->mfInTransaction) // transaction wrappers automatically lock the object
    XT_LockMutex(this->mLock);
  #endif
}
void CSimpleODBC::UnlockSelf()
{
  #ifdef _XTHREADS_H
  if (!this->mfInTransaction)
    XT_ReleaseMutex(this->mLock);
  #endif
}

// -- CDataset methods --

CSimpleODBC::CDataset::CDataset()
{
  this->maszRows = NULL;
  this->mstRows = 0;
  this->mstCols = 0;
  this->mfEOF = true;
  this->mfBOF = true;
}

CSimpleODBC::CDataset::~CDataset()
{
  this->Clear();
}

void CSimpleODBC::CDataset::Clear()
{
  if (this->mstRows && this->maszRows)
  {
    for (size_t i = 0; i < this->mstRows; i++)
    {
      for (size_t j = 0; j < this->mstCols; j++)
        free(this->maszRows[i][j]);
      free(this->maszRows[i]);
    }
    free(this->maszRows);
    this->maszRows = NULL;
    this->mstRows = 0;
    this->mstCols = 0;
  }
  this->SetOutOfDS();
}

const CHAR ***CSimpleODBC::CDataset::Rows()
{
  if (this->mstRows > 1)
    return (const CHAR ***)(this->maszRows[1]);
  else
    return NULL;
}

const CHAR *CSimpleODBC::CDataset::Field(size_t stRow, size_t stCol, const CHAR *szNullValue)
{
  if (((this->mstRows) && (this->mstCols - stCol)))
    return (const CHAR *)(this->maszRows[stRow][stCol]);
  return szNullValue;
}

const CHAR *CSimpleODBC::CDataset::Field(size_t stCol, const CHAR *szNullValue)
{
  return (this->Field(this->mstPos, stCol, szNullValue));
}

const CHAR *CSimpleODBC::CDataset::FieldByName(size_t stRow, const CHAR *szName, const CHAR *szNullValue)
{
  int intColIDX = this->ColumnIndex(szName);
  if (intColIDX == -1)
    return szNullValue;
  return this->Field(stRow, (size_t)intColIDX, szNullValue);
}

const CHAR *CSimpleODBC::CDataset::FieldByName(const CHAR *szName, const CHAR *szNullValue)
{
  return this->FieldByName(this->mstPos, szName);
}

int CSimpleODBC::CDataset::ColumnIndex(const CHAR *szName)
{
  if (this->mstRows)
  {
    for (size_t i = 0; i < this->mstCols; i++)
      if (stricmp(this->maszRows[0][i], szName) == 0)
        return (int)i;
  }
  return -1;
}

bool CSimpleODBC::CDataset::Next()
{
  if ((this->mstRows > 1) && ((this->mstRows - this->mstPos) > 1))
  {
    this->mstPos++;
    this->SetInDS();
    return true;
  }
  this->mfEOF = true;
  return false;
}

bool CSimpleODBC::CDataset::EndOfDS()
{
  return this->mfEOF;
}

bool CSimpleODBC::CDataset::BeginningOfDS()
{
  return this->mfBOF;
}

void CSimpleODBC::CDataset::SetInDS()
{
  this->mfBOF = false;
  this->mfEOF = false;
}

void CSimpleODBC::CDataset::SetOutOfDS()
{
  this->mfBOF = true;
  this->mfEOF = true;
}

bool CSimpleODBC::CDataset::Prev()
{
  if ((this->mstPos > 1) && (this->mstRows > 1))
  {
    this->mstPos--;
    this->SetInDS();
    return true;
  }
  this->mfBOF = true;
  this->mfEOF = false;
  return false;
}

bool CSimpleODBC::CDataset::First()
{
  if (this->mstRows > 1)    // first row is column names
  {
    this->mstPos = 1;
    this->SetInDS();
    return true;
  }
  this->SetOutOfDS();
  return false;
}

bool CSimpleODBC::CDataset::Last()
{
  if (this->mstRows > 1)
  {
    this->mstPos = this->mstRows - 1;
    this->SetInDS();
    return true;
  }
  this->SetOutOfDS();
  return false;
}

void CSimpleODBC::CDataset::SetRows(CHAR ***aszNewRows, size_t stNewRows, size_t stNewCols)
{
  this->Clear();
  this->maszRows = aszNewRows;
  this->mstRows = stNewRows;
  this->mstCols = stNewCols;
  this->mstPos = 1;
  if (stNewRows)
    this->SetInDS();
  else
    this->SetOutOfDS();
}

const CHAR **CSimpleODBC::CDataset::Columns()
{
  if (this->mstRows)
    return (const CHAR **)(this->maszRows[0]);
  return NULL;
}

const CHAR *CSimpleODBC::CDataset::ColName(size_t idx)
{
  if (this->mstCols)
    if (idx < this->mstCols)
      return (const CHAR *)this->maszRows[0][idx];
  return NULL;
}

size_t CSimpleODBC::CDataset::RowCount()
{
  if (this->mstRows)
    return this->mstRows - 1;
  else
    return 0;
}

size_t CSimpleODBC::CDataset::ColCount()
{
  return this->mstCols;
}



// == fallback log function ==
#if defined(USE_SIMPLEODBC_INTERNAL_LOGGER)
void Log(unsigned int uiLevel, const CHAR *sz,...)
{
  // very raw, but should pass if you REALLY don't want all the neatness of Log.h
  printf("[%u] ", uiLevel);
  va_list args;
  va_start(args, sz);
  vprintf(sz, args);
  va_end(args);
  printf("\n");
}
#endif