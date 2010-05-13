#include "Log.h"
#ifdef  _SIMPLEODBC_H_
#define MAX_DB_CONNECT_FAILURES     3   // max consecutive db connection failures before db logs will be discarded
#endif

int log_str_replace(CHAR **szMain, const CHAR *szFind, const CHAR *szReplace, size_t *stBufLen, bool fHandleMem = true);

struct SLogItem {
  unsigned int uiSeverity;
  CHAR *szLogString;
  int intErrorCode;
	time_t ttLogTime;
	bool Status;
	SLogItem()
	{
	  this->Status = false;
	}
};

bool g_LastLogWasStatus = false;
// these obj-level options are set via SetLogOptions -- see there for details on what they
//  mean, and what to set them to

bool mfLogToFile = false;                                          
bool mfLogToStdOut = true;
bool mfLogToDB = false;
bool mfThreadedLogFlushes = false;
bool mfLogFileThreadRunning = false;
bool mfLogScreenThreadRunning = false;
bool mfLogDBThreadRunning = false;
bool mfLogRotateThreadRunning = false;
bool mfDateStampLogs = true;
bool mfStdOutLoggingSuspended = false;
unsigned int muiMinimumLoggingSeverity = LS_NOTICE;
unsigned int muiFlushIntervalSecs = DEFAULT_LOG_FLUSH_INTERVAL;
CHAR mszLogFilePath[MAX_PATH] = {0};
#ifdef _SIMPLEODBC_H_
SDatabaseConnectionInfo msDBConnInfo;
std::string strLogSQL = "";
#endif
CHAR mszDateTimeFormat[MAX_DATE_TIME_FORMAT_LEN] = DEFAULT_TIME_FORMAT;
unsigned int muiMaxLogfileSizeKB = DEFAULT_MAX_LOG_SIZE;
unsigned int muiMaxArchivedLogs = DEFAULT_MAX_ARCHIVED_LOGS;
unsigned int muiMinLogsForFlush = DEFAULT_MIN_LOGS_FOR_FLUSH;
unsigned int muiFailedDBConnections = 0;

MUTEX_TYPE mmLogFileQ = NULL;
MUTEX_TYPE mmLogScreenQ = NULL;
MUTEX_TYPE mmLogDBQ = NULL;
MUTEX_TYPE mmLogRotate = NULL;
MUTEX_TYPE mmLogFile = NULL;
MUTEX_TYPE mmScreen = NULL;
MUTEX_TYPE mmWindDown = NULL;

THREAD_CB_RET FlushFileLogsThread(void *ptr);
THREAD_CB_RET FlushScreenLogsThread(void *ptr);
THREAD_CB_RET FlushDBLogsThread(void *ptr);
THREAD_CB_RET RotateLogsThread(void *ptr);

CHAR *GetBlankLine();
unsigned int GetConsoleColumns();

#ifdef _PTHREAD_H
pthread_t ptFileLogThread, ptScreenLogThread, ptDBLogThread, ptRotateLogsThread;
#endif

void FlushScreenLogs(bool fFlushAll = false);
void FlushFileLogs(bool fFlushAll = false);
void FlushDBLogs(bool fFlushAll = false);
const CHAR *szFileRenameError(int intErr);
bool RenameFile(CHAR *szOld, CHAR *szNew);

unsigned int muiFileLogQueueLen = 0;
unsigned int muiScreenLogQueueLen = 0;
unsigned int muiDBLogQueueLen = 0;
SLogItem **asliFileLogs = NULL;
SLogItem **asliScreenLogs = NULL;
SLogItem **asliDBLogs = NULL;

bool mfSingleThreaded = true;   // threaded logging has to actually be turned on
bool mfColorLogging = true;

#ifndef PATH_DELIMITER
#if defined(WIN32) || defined(WIN64)
#define PATH_DELIMITER "\\"
#else
#define PATH_DELIMITER "/"
#endif
#endif
#ifndef PATH_DELIMITER_C
#if defined(WIN32) || defined(WIN64)
#define PATH_DELIMITER_C '\\'
#else
#define PATH_DELIMITER_C '/'
#endif
#endif

#ifndef WIN32
#ifndef strlwr
#define strlwr(sz) \
	for (size_t i = 0; i < strlen(sz); i++) \
	{ \
		if ((sz[i] >= 'A') && (sz[i] <= 'Z')) \
			sz[i] += ('a' - 'A'); \
	}
#endif		
#endif

#define LOG_FN_VA \
  CHAR *szBuf; \
	if (fVariableArgs) \
	{ \
    CHAR *pos; \
    CHAR *szFSCopy = (CHAR *)_MALLOC((1 + strlen(szFormattedLogString)) * sizeof(CHAR)); \
    strcpy(szFSCopy, szFormattedLogString); \
    CHAR *szArg; \
    size_t stReqLen = strlen(szFormattedLogString); \
    va_list args; \
    va_start(args, szFormattedLogString); \
    while ((pos = strstr(szFSCopy, "%"))) \
    { \
      ++pos; \
      if (*pos == '%') \
      { \
        strcpy(szFSCopy, ++pos); \
        continue; \
      } \
      while (strchr("0123456789", *pos)) \
        pos++; \
      if (pos == NULL) \
        break; \
      switch (*pos) \
      { \
        case 'f': \
        case '.': \
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
          stReqLen += NUMERIC_FORMAT_MAXSIZE; \
          break; \
        } \
        case 'l': \
        { \
          stReqLen += NUMERIC_FORMAT_MAXSIZE; \
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
          break; \
        } \
        case 'i': \
        default: \
        { \
          va_arg(args, int); \
          stReqLen += NUMERIC_FORMAT_MAXSIZE; \
        } \
      } \
			CHAR *szTmpCp = (CHAR *)malloc((1 + strlen(pos)) * sizeof(CHAR)); \
			strcpy(szTmpCp, pos); \
      strcpy(szFSCopy, szTmpCp); \
			free(szTmpCp); \
    } \
    va_end(args); \
     \
    va_start(args, szFormattedLogString); \
    szBuf = (CHAR *)_MALLOC((128 + stReqLen) * sizeof(CHAR)); \
    vsprintf(szBuf, szFormattedLogString, args); \
    va_end(args); \
    _FREE(szFSCopy);\
  } \
  else \
  { \
    szBuf = (CHAR *)_MALLOC((1 + strlen(szFormattedLogString)) * sizeof(CHAR)); \
    strcpy(szBuf, szFormattedLogString);  \
  } \

#define LOG_FN_BODY(status)  if (mfLogToFile) \
  { \
    SLogItem *sliTmp = (SLogItem *)_MALLOC(sizeof(SLogItem)); \
    sliTmp->szLogString = (CHAR *)_MALLOC((1 + strlen(szBuf)) * sizeof(CHAR)); \
    strcpy(sliTmp->szLogString, szBuf); \
    sliTmp->intErrorCode = intErrorCode; \
    sliTmp->uiSeverity = uiSeverity; \
		if (mfDateStampLogs)	\
			sliTmp->ttLogTime = time(NULL); \
		else \
			sliTmp->ttLogTime = 0; \
    XT_LockMutex(mmLogFileQ); \
    asliFileLogs = (SLogItem **)_REALLOC(asliFileLogs, (muiFileLogQueueLen + 1) * sizeof(SLogItem)); \
    asliFileLogs[muiFileLogQueueLen++] = sliTmp; \
    XT_ReleaseMutex(mmLogFileQ); \
  } \
   \
  if (mfLogToStdOut) \
  { \
    SLogItem *sliTmp = (SLogItem *)_MALLOC(sizeof(SLogItem)); \
    sliTmp->szLogString = (CHAR *)_MALLOC((1 + strlen(szBuf)) * sizeof(CHAR)); \
    strcpy(sliTmp->szLogString, szBuf); \
    sliTmp->intErrorCode = intErrorCode; \
    sliTmp->uiSeverity = uiSeverity; \
		if (mfDateStampLogs)	\
			sliTmp->ttLogTime = time(NULL); \
		else \
			sliTmp->ttLogTime = 0; \
    sliTmp->Status = status; \
    XT_LockMutex(mmLogScreenQ); \
    asliScreenLogs = (SLogItem **)_REALLOC(asliScreenLogs, (muiScreenLogQueueLen + 1) * sizeof(SLogItem)); \
    asliScreenLogs[muiScreenLogQueueLen++] = sliTmp; \
    XT_ReleaseMutex(mmLogScreenQ); \
  } \
   \
  if (mfLogToDB) \
  { \
    SLogItem *sliTmp = (SLogItem *)_MALLOC(sizeof(SLogItem)); \
    sliTmp->szLogString = (CHAR *)_MALLOC((1 + strlen(szBuf)) * sizeof(CHAR)); \
    strcpy(sliTmp->szLogString, szBuf); \
    sliTmp->intErrorCode = intErrorCode; \
    sliTmp->uiSeverity = uiSeverity; \
    sliTmp->ttLogTime = time(NULL); \
    XT_LockMutex(mmLogDBQ); \
    asliDBLogs = (SLogItem **)_REALLOC(asliDBLogs, (muiDBLogQueueLen + 1) * sizeof(SLogItem)); \
    asliDBLogs[muiDBLogQueueLen++] = sliTmp; \
    XT_ReleaseMutex(mmLogDBQ); \
  } \
  _FREE(szBuf); \
  if (mfSingleThreaded) \
  { \
    FlushLogs(false); \
    RotateLogs(); \
  } \

#ifndef WIN32
void Sleep(unsigned long ulMicroSeconds)
{
	usleep(ulMicroSeconds * 1000);
}
#endif

void SetLogOptions(unsigned int uiMinimumLogSeverity, bool fLogToFile, bool fLogToStdOut,
  const CHAR *szLogFile, unsigned int uiFlushIntervalSecs, bool fDateStampLogs, 
  const CHAR *szLogDateTimeFormat, unsigned int uiMaxLogfileSizeKB, unsigned int uiMaxArchivedLogs, 
  bool fSingleThreaded, unsigned int uiMinLogsForFlush
  #ifndef DISABLE_DB_LOGGING
  , bool fLogToDB, SDatabaseConnectionInfo *sDBConnInfo, const CHAR *szLogSQL
  #endif
  )
{
  /*
  * Set logging options with this function before starting any logging threads
  *   if you want to change options, you should probably stop the logging threads
  *   and restart them, unless you're just changing the minimum logging level. 
  *   Other options will affect how logging threads are run -- you may get unexpected
  *   behaviour.
  * Conventions: parameters are mentioned by name, with the default value in brackets. If the default value
  *   results in no change to the running parameters, then the startup parameter value is mentioned after a ~
  * Parameters for this funtion:
  *   uiMinimumLogSeverity: (LS_DEBUG == 10)
  *     (numeric) log level describing the minimum log level to be considered for a log
  *     item to enter the queue. Note that there are #defines for the 5 most common log levels
  *     that you might want to use:
  *       LS_DEBUG    10
  *       LS_TRIVIA   20
  *       LS_NOTICE   30
  *       LS_WARNING  40
  *       LS_ERROR    50
  *     as well as the symbolic LS_NONE (100), which can be used here to effectively suppress
  *     all logging
  *   fLogToFile: (false -- you need to specify a log file name anyway)
  *     (bool) whether or not to log to file. (Re-)start logging threads to see effect in MT
  *     environments. Requires that a non-NULL value be set for szLogFile
  *   fLogToStdOut: (true -- might as well have *something* on by default)
  *     (bool) whether or not to log to stdout. (Re-)start logging threads to see effect in MT
  *     environments.
  *   fLogToDB: (false -- you need to specify a database connection string anyway)
  *     (bool) whether or not to log to database. (Re-)start logging threads to see effect in MT
  *     environments. Requires that a non-NULL value be set for szDBConnStr.
  *     NOTE: none of the DB stuff will work unless you have included (prior to Log.h), SimpleODBC.h
  *   szLogFile: (NULL)
  *     (cstring) path to base log file (rotated logs will be <szLogFile>.N where N is the
  *     zero-based rotation number.
  *   sDBConnInfo: (NULL)
  *     (cstring) connection info from which an ODBC connection string will be built
  *   uiFlushIntervalSecs: (DEFAULT_LOG_FLUSH_INTERVAL == 5)
  *     (numeric) interval in seconds between calls to log flush routines. 
  *       Obvious disclaimer: Note that if you set uiMinLogsForFlush
  *       to something non-zero, then you are not guaranteed to see output every uiFlushIntervalSecs seconds
  *   fDateStampLogs: (true)
  *     (boolean) whether or not to add time/date stamps to FILE logs only. DB logs expect that a datetime
  *     field will exist
  *   szLogDateTimeFormat: (NULL; when NULL is received, then DEFAULT_TIME_FORMAT == "" will be assigned if nothing has been previously assigned)
  *     (cstring) format for FILE log date/time stamps, of the format expected by strftime()
  *   uiMaxLogfileSizeKB: (DEFAULT_MAX_LOG_SIZE == 512)
  *     (numeric) max size, in KB for log files to grow to before they become elligable for rotation
  *     note that this doesn't mean your log files will be exactly this big -- rather, once they have
  *     grown beyond this, they will be rotated either at the next rotation interval, or when you specifically
  *     call RotateLogs()
  *   uiMaxArchivedLogs:
  *     how many archives to keep. Setting this, for example to N would result in N archives of your logs
  *     being kept, like so:
  *       mylog.log
  *       mylog.log.0
  *       ...
  *       mylog.log.(N-1)
  *   fSingleThreaded: (false)
  *     (bool) when set, the log functions expect to work without the aid of the logging threads, meaning that
  *     Log() will call flushing functions for you. Your minimum log buffer size determines if you actually see
  *     queued logs. This option is useful for light, one-quick-pass programs, which can do something like:
  *     int main (int argc, char *argv[])
  *     {
  *       SetLogOptions(...);
  *       Log(LS_NOTICE, "Starting up!");
  *       // program logic
  *       ...
  *       Log(LS_DEBUG, "Some debug output here!");
  *       // more program logic
  *       ...
  *       Log(LS_WARNING, "Last log of the program... are you going to miss me?");
  *       FlushAllLogs();     // only required if the log buffer is > 1
  *       // program ends
  *       return 0;
  *     }
  *   uiMinLogsForFlush: (0)
  *     (numeric) minimumum length of logging queue (for all log queues) before a flush call
  *     for that queue actually does anything useful
  *   szLogSQL: (NULL)
  *     (cstring) SQL string to use for logging; there are some variables which will be substituted
  *                 as appropriate, with automagic quoting (so don't quote them in your sql):
  *                 %desc%    ::  Log description string
  *                 %level%   ::  Numeric log level (as resolved from symbolic levels)
  *                 %errno%   ::  Error number, where appripriate
  */

  mfSingleThreaded = fSingleThreaded;
  muiMinimumLoggingSeverity = uiMinimumLogSeverity;
  muiMinLogsForFlush = uiMinLogsForFlush;
  mfLogToFile = fLogToFile;
	if (mfLogToFile)
	{
		muiMaxLogfileSizeKB = uiMaxLogfileSizeKB;
		muiMaxArchivedLogs = uiMaxArchivedLogs;
		if ((szLogFile == NULL) || (strlen(szLogFile) == 0))
		{
			printf("File logging requested, but no log file given; disabling\n");
			mfLogToFile = false;
		}
		else
			strcpy(mszLogFilePath, szLogFile);
	}
  mfLogToStdOut = fLogToStdOut;
  if (szLogFile  && (strlen(szLogFile) > (MAX_PATH - 1)))
  {
    printf("ERROR: provided logfile path is > %li chars in length; unsupported! (this is limited by the\
     windows MAX_CHARS #define\n", (long)MAX_PATH);
  }
  muiFlushIntervalSecs = uiFlushIntervalSecs;
	mfDateStampLogs = fDateStampLogs;
	if (szLogDateTimeFormat)
	{
		if (strcmp(szLogDateTimeFormat, "none") == 0)
			mfDateStampLogs = false;
		else
		{
			if (strlen(szLogDateTimeFormat) < (MAX_DATE_TIME_FORMAT_LEN - 1))
				strcpy(mszDateTimeFormat, szLogDateTimeFormat);
			else
				printf("ERROR: Your logging date/time format is too long (max %li chars), falling back on default (%s)\n",
						(long)(MAX_DATE_TIME_FORMAT_LEN - 1), mszDateTimeFormat);
		}
	}
	else if (strlen(mszDateTimeFormat) == 0)
		strcpy(mszDateTimeFormat, DEFAULT_TIME_FORMAT);

  #ifdef _SIMPLEODBC_H_
  mfLogToDB = fLogToDB;
  if (sDBConnInfo && szLogSQL)
  {
    msDBConnInfo.Copy(sDBConnInfo);
    strLogSQL = szLogSQL;
  }
  else
  {
    if (mfLogToDB)
    {
      mfLogToDB = false;
      fprintf(stderr, "ERROR: fLogToDB specified as TRUE for SetLogOptions, but one or more of db conn string and logsql were not specified specified\n");
    }
  }
  #endif
}

#ifndef DISABLE_INI_SETTINGS
void SetLogOptions(CINIFile *ini)
{
  CHAR szDefaultLogFile[MAX_PATH];
  strcpy(szDefaultLogFile, ini->MainFile());
  CHAR *pos = strrchr(szDefaultLogFile, '.');
  if (pos)
    *pos = '\0';
  strcat(szDefaultLogFile, ".log");
  #ifdef DISABLE_DB_LOGGING
  bool fLogToDatabase = false;
  void *sDBConnInfo = NULL;
  #else
  bool fLogToDatabase = ini->GetBoolValue(LI_SECTION_LOGGING, LI_SETTING_DB, LI_DEFAULT_DB, true);
  SDatabaseConnectionInfo *sDBConnInfo = NULL;
  if (fLogToDatabase)
  {
    const CHAR *szDBHost = ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_DBHOST);
    const CHAR *szDBName = ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_DBNAME);
    if (szDBHost && szDBName)
    {
      const CHAR *szDBUser = ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_DBUSER, "");
      const CHAR *szDBPass = ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_DBPASS, "");
      const CHAR *szDBDriver = ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_DBDRIVER, LI_DEFAULT_DBDRIVER);
      sDBConnInfo = new SDatabaseConnectionInfo(szDBHost, szDBName, szDBUser, szDBPass, szDBDriver);
    }
    else  // look for a generic database section with the same settings
    {
      szDBHost = ini->GetSZValue(LI_SECTION_DATABASE, LI_SETTING_DBHOST);
      szDBName = ini->GetSZValue(LI_SECTION_DATABASE, LI_SETTING_DBNAME);
      if (szDBHost && szDBName)
      {
        const CHAR *szDBUser = ini->GetSZValue(LI_SECTION_DATABASE, LI_SETTING_DBUSER, "");
        const CHAR *szDBPass = ini->GetSZValue(LI_SECTION_DATABASE, LI_SETTING_DBPASS, "");
        const CHAR *szDBDriver = ini->GetSZValue(LI_SECTION_DATABASE, LI_SETTING_DBDRIVER, LI_DEFAULT_DBDRIVER);
        sDBConnInfo = new SDatabaseConnectionInfo(szDBHost, szDBName, szDBUser, szDBPass, szDBDriver);
      }
      else
        fprintf(stderr, "WARNING: database logging has been set on in file '%s' but sections '%s' and '%s don't have enough information ('%s' and '%s' at the least) to establish a database connection\n",
          ini->MainFile(), LI_SECTION_LOGGING, LI_SECTION_DATABASE, LI_SETTING_DBHOST, LI_SETTING_DBNAME);
    }
  }
  #endif 
  
  SetLogOptions(GrokLogLevel(ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_LEVEL, LS_DEFAULT_NAME, true), LS_DEFAULT),
                ini->GetBoolValue(LI_SECTION_LOGGING, LI_SETTING_FILE, LI_DEFAULT_FILE, true),
                ini->GetBoolValue(LI_SECTION_LOGGING, LI_SETTING_SCREEN, LI_DEFAULT_SCREEN, true),
                ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_LOGFILE, szDefaultLogFile, true),
                ini->GetUIntValue(LI_SECTION_LOGGING, LI_SETTING_FLUSH_INTERVAL, LI_DEFAULT_FLUSH, true),
                ini->GetBoolValue(LI_SECTION_LOGGING, LI_SETTING_DATESTAMP, LI_DEFAULT_DATESTAMP, true),
                ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_DATESTAMPFORMAT, DEFAULT_TIME_FORMAT, true),
                ini->GetUIntValue(LI_SECTION_LOGGING, LI_SETTING_LOGSIZE, LI_DEFAULT_LOGSIZE, true),
                ini->GetUIntValue(LI_SECTION_LOGGING, LI_SETTING_ARCHIVE, LI_DEFAULT_ARCHIVE, true),
                !ini->GetBoolValue(LI_SECTION_LOGGING, LI_SETTING_THREADED, LI_DEFAULT_THREADED, true),
                ini->GetUIntValue(LI_SECTION_LOGGING, LI_SETTING_BUFFER, LI_DEFAULT_BUFFER, true)
                #ifndef DISABLE_DB_LOGGING
                ,fLogToDatabase, sDBConnInfo,ini->GetSZValue(LI_SECTION_LOGGING, LI_SETTING_LOGSQL, NULL, false)
                #endif
                );
  #ifndef DISABLE_DB_LOGGING                
  if (sDBConnInfo)
    delete sDBConnInfo;
  #endif
                 
}
#endif

void StartLogFlushThreads()
{
  if (mfSingleThreaded)
  { // has no effect if singlethreaded set
    return;
  }
  #ifdef _PTHREAD_H
  pthread_attr_t *attr = NULL;
  pthread_attr_init(attr);
  pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED);
  #endif
	if (mfLogToFile && (mmLogFileQ == NULL))
	{
	  #ifdef _PTHREAD_H
	  pthread_create(&ptFileLogThread, attr, FlushFileLogsThread, NULL);
	  #else
		_beginthreadex(NULL, 0, FlushFileLogsThread, NULL, 0, NULL);
		#endif
		while (mmLogFileQ == NULL)
		  #if defined(WIN32) || defined(WIN64)
		  Sleep(50);
		  #else
		  usleep(50000);
		  #endif
		while (!XT_ThreadSafeBooleanCheck(mmLogFileQ, &mfLogFileThreadRunning))
		  #if defined(WIN32) || defined(WIN64)
		  Sleep(50);
		  #else
		  usleep(50000);
		  #endif
  }
	if (mfLogToStdOut && (mmLogScreenQ == NULL))
	{
		#ifdef _PTHREAD_H
		pthread_create(&ptScreenLogThread, attr, FlushScreenLogsThread, NULL);
		#else
		_beginthreadex(NULL, 0, FlushScreenLogsThread, NULL, 0, NULL);
		#endif
		while (mmLogScreenQ == NULL)
		  #if defined(WIN32) || defined(WIN64)
		  Sleep(50);
		  #else
		  usleep(50000);
		  #endif
		while (!XT_ThreadSafeBooleanCheck(mmLogScreenQ, &mfLogScreenThreadRunning))
		  #if defined(WIN32) || defined(WIN64)
		  Sleep(50);
		  #else
		  usleep(50000);
		  #endif
  }
	if (mfLogToDB && (mmLogDBQ == NULL))
	{
		#ifdef _PTHREAD_H
		pthread_create(&ptDBLogThread, attr, FlushDBLogsThread, NULL);
		#else
		_beginthreadex(NULL, 0, FlushDBLogsThread, NULL, 0, NULL);
		#endif
		while (mmLogDBQ == NULL)
		  #if defined(WIN32) || defined(WIN64)
		  Sleep(50);
		  #else
		  usleep(50000);
		  #endif
		while (!XT_ThreadSafeBooleanCheck(mmLogDBQ, &mfLogDBThreadRunning))
		  #if defined(WIN32) || defined(WIN64)
		  Sleep(50);
		  #else
		  usleep(50000);
		  #endif
	}	
	#ifdef _PTHREAD_H
	pthread_attr_destroy(attr);
	#endif
}

void StopLogFlushThreads()
{
  if (mfSingleThreaded)
    return;
  // spawn actual log finisher in a background thread so that we can time out the stop process
  //  for example if the DB isn't responding or something similarly bad
	mmWindDown = XT_CreateMutex(false);
  XT_SpawnThread(StopLogFlushThreadsWorker, NULL, XT_THREAD_LIMIT_CEILING - 1, NULL);
  unsigned int uiSlept = 0;
  while (mmWindDown)
  {
    Sleep(100);
    if (uiSlept++ > 100)  // just give up; we've waited 10s
      break;
  }
}

THREAD_CB_RET StopLogFlushThreadsWorker(void *ptr)
{
  if (mmWindDown == NULL)
  	mmWindDown = XT_CreateMutex(false);
  #if defined(WIN32) || defined(WIN64)
  Sleep(muiFlushIntervalSecs * 1100);   // there has to be a better solution; i'm getting threadlocked if the wind-down is too close to a start-up
  #endif
  FlushLogs();
	unsigned long long uiWaited = 0;
	XT_LockMutex(mmWindDown);
	if (mmLogScreenQ && mfLogScreenThreadRunning)
	{
		//XT_LockMutex(mmLogScreenQ);
		mfLogScreenThreadRunning = false;
		//XT_ReleaseMutex(mmLogScreenQ);
		//FlushScreenLogs();
	}
	if (mmLogFileQ && mfLogFileThreadRunning)
	{
		//XT_LockMutex(mmLogFileQ);
		mfLogFileThreadRunning = false;
		//XT_ReleaseMutex(mmLogFileQ);
		//FlushFileLogs();
	}
	if (mmLogDBQ && mfLogDBThreadRunning)
	{
		//XT_LockMutex(mmLogDBQ);
		mfLogDBThreadRunning = false;
		//XT_ReleaseMutex(mmLogDBQ);
		//FlushDBLogs();
	}
	
	XT_ReleaseMutex(mmWindDown);
	while (true)
	{
	  if (uiWaited > (5 * muiFlushIntervalSecs * 1000))
	    break;
		XT_LockMutex(mmWindDown);
		if (mmLogScreenQ != NULL)
		{
			XT_ReleaseMutex(mmWindDown);
			uiWaited += 100;
			Sleep(100);
			continue;
		}
		if (mmLogFileQ != NULL)
		{
			XT_ReleaseMutex(mmWindDown);
			uiWaited += 100;
			Sleep(100);
			continue;
		}
		if (mmLogDBQ != NULL)
		{
		  XT_ReleaseMutex(mmWindDown);
		  uiWaited += 100;
		  Sleep(100);
		  continue;
		}
		break;
	}
	XT_ReleaseMutex(mmWindDown);
	#ifdef _PTHREAD_H
	pthread_mutex_destroy(mmWindDown);
	#else
	XT_DestroyMutex(mmWindDown);
	mmWindDown = NULL;
	#endif
	THREAD_EXIT;
}

void FlushFileLogs(bool fFlushAll)
{
  XT_LockMutex(mmLogFileQ);
  if (!fFlushAll && (muiFileLogQueueLen < muiMinLogsForFlush))
  {
    XT_ReleaseMutex(mmLogFileQ);
    return;
  }
  
	SLogItem **asliLocalFileLogs = asliFileLogs;
	asliFileLogs = NULL;
	unsigned int uiLogItems = muiFileLogQueueLen;
	muiFileLogQueueLen = 0;
  XT_ReleaseMutex(mmLogFileQ);
  
  if (uiLogItems == 0)
    return;
   
	XT_LockMutex(mmLogFile);
	FILE *fp = fopen(mszLogFilePath, "ab");
  CHAR szLogTime[32];
  szLogTime[0] = '\0';
  struct tm *mytm;
  
	if (!fp)
	{
		printf("Unable to open %s for appending\n", mszLogFilePath);
		for (unsigned int i = 0; i < muiFlushIntervalSecs; i++)
		{
			if (!XT_ThreadSafeBooleanCheck(mmLogFile, &mfLogFileThreadRunning))
				break;
			Sleep(1000);
		}
		XT_ReleaseMutex(mmLogFile);
		return;
	}
	for (unsigned int i = 0; i < uiLogItems; i++)
	{
		CHAR *szLogLine = (CHAR *)_MALLOC((128 + strlen(asliLocalFileLogs[i]->szLogString)) * sizeof(CHAR));
		CHAR szLevel[12];
		switch (asliLocalFileLogs[i]->uiSeverity)
		{
			case LS_DEBUG:
			{
				strcpy(szLevel, "(debug)");
				break;
			}
			case LS_TRIVIA:
			{
				strcpy(szLevel, "(trivia)");
				break;
			}
			case LS_NOTICE:
			{
				strcpy(szLevel, "(notice)");
				break;
			}
			case LS_WARNING:
			{
				strcpy(szLevel, "(warning)");
				break;
			}
			case LS_ERROR:
			{
				strcpy(szLevel, "(error)");
				break;
			}
			default:
			{
				sprintf(szLevel, "(l: %u)", asliLocalFileLogs[i]->uiSeverity); 
			}
		}
		if (asliLocalFileLogs[i]->ttLogTime && mfDateStampLogs)
		{
			mytm = localtime(&(asliLocalFileLogs[i]->ttLogTime));
			strftime(szLogTime, MAX_DATE_TIME_FORMAT_LEN, mszDateTimeFormat, mytm);
			sprintf(szLogTime, "%s :: ", szLogTime);
		}
		if (asliLocalFileLogs[i]->intErrorCode)
			sprintf(szLogLine, "%s%s :: (%i) %s%s", szLogTime, szLevel, asliLocalFileLogs[i]->intErrorCode, 
					asliLocalFileLogs[i]->szLogString, NEWLINE);
		else
			sprintf(szLogLine, "%s%s :: %s%s", szLogTime, szLevel, asliLocalFileLogs[i]->szLogString, NEWLINE);

		if (strlen(szLogLine) > MAX_FILE_IO_SIZE)
		{
			CHAR *ptr = szLogLine;
			size_t stWritten = 0, stToWrite, stStrlen = strlen(szLogLine);
			while (stWritten < stStrlen)
			{
				stToWrite = (stStrlen - stWritten);
				if (stWritten > MAX_FILE_IO_SIZE)
					stWritten = MAX_FILE_IO_SIZE;
			  fflush(stdout);
				if (fwrite(ptr, sizeof(CHAR), stToWrite, fp) != stToWrite)
				{
					printf("ERROR: Unable to write %li bytes to %s\n", (long)stToWrite, mszLogFilePath);
					break;
				}
				stWritten += stToWrite;
				ptr += stToWrite;
			}
		}
		else
		{
		  fflush(stdout);
			fwrite(szLogLine, sizeof(CHAR), strlen(szLogLine), fp);
    }
		_FREE(szLogLine);
		_FREE(asliLocalFileLogs[i]->szLogString);
		_FREE(asliLocalFileLogs[i]);
	}
	_FREE(asliLocalFileLogs);
	fclose(fp);
	XT_ReleaseMutex(mmLogFile);
}

#ifdef _PTHREAD_H
void *FlushFileLogsThread(void *ptr)
#else
unsigned int WINAPI FlushFileLogsThread(void *ptr)
#endif
{
  mmLogFileQ = XT_CreateMutex(false);
	mmLogFile = XT_CreateMutex(false);

  mfLogFileThreadRunning = true;

	if (muiMaxLogfileSizeKB)
	{
		#ifdef _PTHREAD_H
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&ptRotateLogsThread, &attr, RotateLogsThread, NULL);
		pthread_attr_destroy(&attr);
		#else 
		_beginthreadex(NULL, 0, RotateLogsThread, NULL, 0, NULL);
		#endif
	}
	while(XT_ThreadSafeBooleanCheck(mmLogFileQ, &mfLogFileThreadRunning))
  {
    FlushFileLogs();
    for (unsigned int i = 0; i < (muiFlushIntervalSecs * 10); i++)
    {
      if (!XT_ThreadSafeBooleanCheck(mmLogFileQ, &mfLogFileThreadRunning))
        break;
      Sleep(100);
    }
  }
  FlushFileLogs();
	if (muiMaxArchivedLogs)
	{
		XT_LockMutex(mmLogRotate);
		mfLogRotateThreadRunning = false;
		XT_ReleaseMutex(mmLogRotate);
	}
	XT_LockMutex(mmWindDown);
	XT_DestroyMutex(mmLogFileQ);
	XT_DestroyMutex(mmLogFile);
	mmLogFileQ = NULL;
	mmLogFile = NULL;
	XT_ReleaseMutex(mmWindDown);
	#ifdef _PTHREAD_H
	return NULL;
	#else
	_endthreadex(0);
  return 0;
  #endif
}

void LogS(unsigned int uiSeverity, const CHAR *szLogString)
{
  char *szBuf = (char *)malloc((1 +strlen(szLogString)) * sizeof(char));
  strcpy(szBuf, szLogString);
  int intErrorCode = 0;
  LOG_FN_BODY(false);
}

void Log_Status(unsigned int uiSeverity, const CHAR *szFormattedLogString, ...)
{
  if (uiSeverity < muiMinimumLoggingSeverity)
    return;
	int intErrorCode = 0;
  bool fVariableArgs = true;
	LOG_FN_VA;
	LOG_FN_BODY(true);
}

void Log(unsigned int uiSeverity, const CHAR *szFormattedLogString,...)
{
  #ifdef _DEBUG
  if (uiSeverity >= LS_ERROR)
    while (0);
  #endif
  if (uiSeverity < muiMinimumLoggingSeverity)
    return;
	int intErrorCode = 0;
  bool fVariableArgs = true;
	LOG_FN_VA;
	LOG_FN_BODY(false);
}

void Log(unsigned int uiSeverity, int intErrorCode, const CHAR *szFormattedLogString,...)
{
  #ifdef _DEBUG
  if (uiSeverity >= LS_ERROR)
    while(0); // NOOP for debugging
  #endif
  if (uiSeverity < muiMinimumLoggingSeverity)
    return;
  bool fVariableArgs = true;
	LOG_FN_VA;
	LOG_FN_BODY(false);
}

unsigned int GetConsoleColumns()
{
  #ifdef WIN32
  CONSOLE_SCREEN_BUFFER_INFO i;
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleScreenBufferInfo(hStdOut, &i))
    return 80;  // default
  unsigned int uiRet = i.dwMaximumWindowSize.X;
  if (uiRet > 16)
    return uiRet;
  else
    return 80;  // sacrifice accuracy for something that works
  #else
  // at some point, put in some ioctl magic here
  return 80;
  #endif
}

CHAR *GetBlankLine()
{
  unsigned int uiCols = GetConsoleColumns();
  CHAR *ret = (CHAR *)_MALLOC(uiCols * sizeof(CHAR));
  if (ret)
  {
    ret[0] = '\r';
    for (unsigned int i = 1; i < uiCols - 2; i++)
      ret[i] = ' ';
    ret[uiCols-2] = '\r'; 
    ret[uiCols-1] = '\0';
  }
  return ret;
}

void FlushScreenLogs(bool fFlushAll)
{
	if (!XT_ThreadSafeBooleanCheck(mmLogScreenQ, &mfLogToStdOut))
	  return;
	  
	XT_LockMutex(mmLogScreenQ);
	if ((!fFlushAll) && (muiScreenLogQueueLen < muiMinLogsForFlush))
	{
	  XT_ReleaseMutex(mmLogScreenQ);
	  return;
	}
	if (muiScreenLogQueueLen == 0)
	{
	  XT_ReleaseMutex(mmLogScreenQ);
	  return;
	}
	SLogItem **asliLocalScreenLogs = asliScreenLogs;
	asliScreenLogs = NULL;
	unsigned int uiLogItems = muiScreenLogQueueLen;
	muiScreenLogQueueLen = 0;
	XT_ReleaseMutex(mmLogScreenQ);
	
	XT_LockMutex(mmScreen);
  struct tm *mytm;
	CHAR szLogTime[256];
	szLogTime[0] = 0;
  WORD wLineAttribs;
  CHAR *szLogBlankLine = GetBlankLine();
  size_t stMaxLen = strlen(szLogBlankLine) - 1;
  
  const CHAR *szTimeDelimiter;
  if (mfDateStampLogs)
    szTimeDelimiter = " :: ";
  else
    szTimeDelimiter = "";
  
	for (unsigned int i = 0; i < uiLogItems; i++)
	{
		CHAR szLevel[12];
		switch (asliLocalScreenLogs[i]->uiSeverity)
		{
			case LS_DEBUG:
			{
				strcpy(szLevel, "[D] ");
				wLineAttribs = FOREGROUND_WHITE;
				break;
			}
			case LS_TRIVIA:
			{
				strcpy(szLevel, "[T] ");
				wLineAttribs = FOREGROUND_CYAN;
				break;
			}
			case LS_NOTICE:
			{
				strcpy(szLevel, "[N] ");
				wLineAttribs = FOREGROUND_BRIGHT_YELLOW;
				break;
			}
			case LS_WARNING:
			{
				strcpy(szLevel, "[W] ");
				wLineAttribs = FOREGROUND_BRIGHT_FUSCHIA;
				break;
			}
			case LS_ERROR:
			{
				strcpy(szLevel, "[E] ");
				wLineAttribs = FOREGROUND_BRIGHT_RED;
				break;
			}
			default:
			{
			  wLineAttribs = FOREGROUND_WHITE;
				sprintf(szLevel, "[%u]", asliLocalScreenLogs[i]->uiSeverity); 
			}
		}

		if (mfDateStampLogs)
		{
			mytm = localtime(&(asliLocalScreenLogs[i]->ttLogTime));
			strftime(szLogTime, MAX_DATE_TIME_FORMAT_LEN, mszDateTimeFormat, mytm);
			strcpy(szLogTime, szLogTime);
		}
		
		if (asliLocalScreenLogs[i]->Status)
		{
		  if (strlen(asliLocalScreenLogs[i]->szLogString) > stMaxLen)
		  {
		    for (int j = 5; j > 1; j--)
		      asliLocalScreenLogs[i]->szLogString[stMaxLen-j] = '.';
		    asliLocalScreenLogs[i]->szLogString[stMaxLen-1] = '\0';
		  }
		  if (asliLocalScreenLogs[i]->intErrorCode)
		  {
			  printf_color(wLineAttribs, "%s%s (%i) %s",
			    szLogBlankLine, szLevel, asliLocalScreenLogs[i]->intErrorCode, 
				  asliLocalScreenLogs[i]->szLogString);
			}
		  else
		  {
			  printf_color(wLineAttribs, "%s%s %s",
			    szLogBlankLine, szLevel, asliLocalScreenLogs[i]->szLogString);
			}
			g_LastLogWasStatus = true;
		}
		else
		{
		  if (g_LastLogWasStatus)
		    printf("\r%s\r", szLogBlankLine);
		  g_LastLogWasStatus = false;
		  if (asliLocalScreenLogs[i]->intErrorCode)
		  {
			  printf_color(wLineAttribs, "%s%s%s%s(%i) %s\n",
			    szLogBlankLine, szLevel, szLogTime, szTimeDelimiter, asliLocalScreenLogs[i]->intErrorCode, 
				  asliLocalScreenLogs[i]->szLogString);
			}
		  else
		  {
			  printf_color(wLineAttribs, "%s%s%s%s%s\n",
			    szLogBlankLine, szLevel, szLogTime, szTimeDelimiter, asliLocalScreenLogs[i]->szLogString);
			}
	  }
		_FREE(asliLocalScreenLogs[i]->szLogString);
		_FREE(asliLocalScreenLogs[i]);
	}
	
	_FREE(szLogBlankLine);
	_FREE(asliLocalScreenLogs);
	XT_ReleaseMutex(mmScreen);
	fflush(stdout);
}

void PauseScreenLogging()
{
  // flushes the screen buffer and stops screen output
  if (mfLogToStdOut == false)
    return;
  XT_LockMutex(mmLogScreenQ);
  mfLogToStdOut = false;
  mfStdOutLoggingSuspended = true;
  XT_ReleaseMutex(mmLogScreenQ);
  FlushScreenLogs();
}

void ResumeScreenLogging()
{
  // re-enables screen logging
  if (!mfStdOutLoggingSuspended)
    return;
  
  XT_LockMutex(mmLogScreenQ);
  mfStdOutLoggingSuspended = false;
  mfLogToStdOut = true;
  XT_ReleaseMutex(mmLogScreenQ);
}

#ifdef _PTHREAD_H
void *FlushScreenLogsThread(void *ptr)
#else
unsigned int WINAPI FlushScreenLogsThread(void *ptr)
#endif
{
  store_console_attribs();
	mmLogScreenQ = XT_CreateMutex(true);
  mmScreen = XT_CreateMutex(false);
  mfLogScreenThreadRunning = true;
  XT_ReleaseMutex(mmLogScreenQ);
  
  while(XT_ThreadSafeBooleanCheck(mmLogScreenQ, &mfLogScreenThreadRunning))
  {
    FlushScreenLogs();
    for (unsigned int i = 0; i < (muiFlushIntervalSecs * 10); i++)
    {
      if (!XT_ThreadSafeBooleanCheck(mmLogScreenQ, &mfLogScreenThreadRunning))
        break;
      Sleep(100);
    }
  }
  FlushScreenLogs();
	XT_LockMutex(mmWindDown);
	XT_DestroyMutex(mmLogScreenQ);
	XT_DestroyMutex(mmScreen);
	mmLogScreenQ = NULL;
	mmScreen = NULL;
	XT_ReleaseMutex(mmWindDown);
	#ifdef _PTHREAD_H
	return NULL;
	#else
	_endthreadex(0);
  return 0;
  #endif
}

#ifdef _PTHREAD_H
void *FlushDBLogsThread(void *ptr)
#else
unsigned int WINAPI FlushDBLogsThread(void *ptr)
#endif
{
	mmLogDBQ = XT_CreateMutex(true);
  mfLogDBThreadRunning = true;
  XT_ReleaseMutex(mmLogDBQ);
  bool fExiting = false;
  
  while(XT_ThreadSafeBooleanCheck(mmLogDBQ, &mfLogDBThreadRunning))
  {
    FlushDBLogs();
    for (unsigned int i = 0; i < muiFlushIntervalSecs; i++)
    {
      if (!XT_ThreadSafeBooleanCheck(mmLogDBQ, &mfLogDBThreadRunning))
      {
        fExiting = true;
        break;
      }
      Sleep(1000);
    }
    if (fExiting)
      break;
  }
  FlushDBLogs();
	XT_LockMutex(mmWindDown);
	XT_DestroyMutex(mmLogDBQ);
	mmLogDBQ = NULL;
	XT_ReleaseMutex(mmWindDown);
	#ifdef _PTHREAD_H
	return NULL;
	#else
	_endthreadex(0);
  return 0;
  #endif
}


bool LoggingThreadsRunning()
{
  if (XT_ThreadSafeBooleanCheck(mmLogFileQ, &mfLogFileThreadRunning))
    return true;
  if (XT_ThreadSafeBooleanCheck(mmLogScreenQ, &mfLogScreenThreadRunning))
    return true;
  if (XT_ThreadSafeBooleanCheck(mmLogDBQ, &mfLogDBThreadRunning))
    return true;
  return false;
}


#ifdef _PTHREAD_H
void *RotateLogsThread(void *ptr)
#else
unsigned int WINAPI RotateLogsThread(void *ptr)
#endif
{
	mmLogRotate = XT_CreateMutex(true);
	mfLogRotateThreadRunning = true;
	XT_ReleaseMutex(mmLogRotate);

	while (XT_ThreadSafeBooleanCheck(mmLogRotate, &mfLogRotateThreadRunning))
	{
	  RotateLogs();
		for (unsigned int i = 0; i < muiFlushIntervalSecs * 2; i++)
		{
			if (!XT_ThreadSafeBooleanCheck(mmLogRotate, &mfLogRotateThreadRunning))
				break;
			Sleep(100);
		}
	}

	XT_DestroyMutex(mmLogRotate);
	mmLogRotate = NULL;
	#ifdef _PTHREAD_H
	return NULL;
	#else
	_endthreadex(0);
	return 0;   // shouldn't get here; compiler requires it
	#endif
}

void RotateLogs()
{
  struct stat st;
	XT_LockMutex(mmLogRotate);
	if (stat(mszLogFilePath, &st) == 0)
	{
		if ((unsigned int)(st.st_size / 1024) >= muiMaxLogfileSizeKB)
		{
			CHAR szOld[MAX_PATH];
			CHAR szNew[MAX_PATH];
			for (unsigned int i = (muiMaxArchivedLogs - 2); i > 0; i--)
			{
				sprintf(szOld, "%s.%u", mszLogFilePath, (i - 1));
				if (stat(szOld, &st) == 0)
				{
					sprintf(szNew, "%s.%u", mszLogFilePath, i);
					XT_LockMutex(mmLogFile);
					RenameFile(szOld, szNew);
					XT_ReleaseMutex(mmLogFile);
				}
			}
			XT_LockMutex(mmLogFile);
			sprintf(szNew, "%s.0", mszLogFilePath);
			// tail, amongst others, will prevent this file from being moved if it is being watched
			//  -- so let's copy out the current content and truncate the existing file
			// copy file out to .0
			#if defined(WIN32) || defined(WIN64)
			CopyFile(mszLogFilePath, szNew, FALSE);
			#else
			struct stat st;
			if (stat(mszLogFilePath, &st) == 0)
			{
				FILE *fpsrc = fopen(mszLogFilePath, "rb");
				FILE *fpdst = fopen(szNew, "wb");
				if (fpsrc && fpdst)
				{
					#define CHUNK_SIZE 4096
					char buf[CHUNK_SIZE];
					size_t stToTransfer, stCopied = 0, stOperation;
					while (stCopied < (size_t)st.st_size)
					{
						stToTransfer = st.st_size - stCopied;
						if (stToTransfer > CHUNK_SIZE)
							stToTransfer = CHUNK_SIZE;
						if ((stOperation = fread(buf, sizeof(char), stToTransfer, fpsrc)) == stToTransfer)
						{
							if ((stOperation = fwrite(buf, sizeof(char), stToTransfer, fpdst)) != stToTransfer)
								break;
						}
						else
							break;
						stCopied += stToTransfer;
					}
					fclose(fpsrc);
					fclose(fpdst);
					#undef CHUNK_SIZE
				}
			}
			#endif
			// truncate log file
			FILE *fp = fopen(mszLogFilePath, "wb");
			if (fp)
			  fclose(fp);
			XT_ReleaseMutex(mmLogFile);
		}
	}
	XT_ReleaseMutex(mmLogRotate);
}

bool RenameFile(CHAR *szOld, CHAR *szNew)
{
  struct stat st;
  if (stat(szNew, &st) == 0)
    remove(szNew);
	int intErr = rename(szOld, szNew);
	if (intErr)
	{
		printf("Unable to rotate file '%s' to '%s': (%i) %s\n", szOld, szNew, intErr, szFileRenameError(intErr));
		return false;
	}
	else
		return true;
}

const CHAR *szFileRenameError(int intErr)
{
	switch (intErr)
	{
		case EACCES:
			return "Access denied";
		case ENOENT:
			return "Original file not found";
		case EINVAL:
			return "Filname contains invalid characters";
		default:
			return "Unknown error";
	}
}

void FlushLogs(bool fForce)
{
  /*
  * Flushes all pending logs to the specified outputs
  * - setting fForce to false only flushes queues longer than muiMinLogsForFlush 
  *   (used by single-threaded logging functions to buffer logs a little)
  * - bear this in mind if you have a single-threaded app: you need to call FlushLogs()
  *   before terminating, or at any time where you actually want to make sure you can see
  *   all logging output
  */
  if (mfLogToStdOut)
    FlushScreenLogs(fForce);
  if (mfLogToFile)
    FlushFileLogs(fForce);
  if (mfLogToDB)
    FlushDBLogs(fForce);
}

int log_str_replace(CHAR **szMain, const CHAR *szFind, const CHAR *szReplace, size_t *stBufLen, bool fHandleMem)
{
  /*
   * replaces one substring in a cstring with another
   * returns the number of substitutions done
   *  valid return values: -1 (either: out of space in the buffer, with fHandleMem off, or error on realloc())
   *  on failure, the contents of *szMain are undefined: you could have just about anything in there
   * optionally (default on) does mem allocations to handle the replacement, if necessary
   * - stBufLen will contain the new buffer length, if re-allocation is done
   * - if mem handling isn't allowed (eg, fixed-sized array), then the function will return -1 if unable to
   *   do the replacement due to buffer size contraints -- in this case, *stBufLen will also be set to zero,
   *   to distinguish from the false return you will also get 
   */
  CHAR *pos;
  CHAR *szTmp = NULL;
  size_t stTmpBuf = 0;
  int ret = 0;
  size_t stReplaceLen = strlen(szReplace);
  size_t stFindLen = strlen(szFind);
  size_t stMyBufLen;
  if (stBufLen)
  {
    if (*stBufLen < strlen(*szMain))
      return -1;  // caller is either confused about the buffer length or really hasn't allocated enough space (in which case, we're just lucky that the strlen doesn't flunk out here)
    else
      stMyBufLen = *stBufLen;
  }
  else
    stMyBufLen = strlen(*szMain) + 1;
  if (stReplaceLen == stFindLen)
    fHandleMem = false;   // nothing to handle if the find & replace are the same length

  while ((pos = strstr(*szMain, szFind)))
  {
    ret++;
    CHAR *endpos = pos + strlen(szFind);
    bool fFreeEndPos = false;
    size_t stTmpLen = strlen(endpos);
    if (stTmpLen)
    {
      if (stMyBufLen < (strlen(*szMain) + (stReplaceLen - stFindLen + 1)))
      {
        if (fHandleMem)
        {
          CHAR *tmp = (CHAR *)malloc((1 + strlen(endpos)) * sizeof(CHAR));
          strcpy(tmp, endpos);
          endpos = tmp;
          fFreeEndPos = true;
          stMyBufLen += 4 * (stReplaceLen - stFindLen);      // allocate for more to save on alloc's
          *szMain = (CHAR *)realloc(*szMain, stMyBufLen * sizeof(CHAR));
          if (*szMain == NULL)
            return -1;  // system allocation error
          pos = strstr(*szMain, szFind);
          if (stBufLen)
            *stBufLen = stMyBufLen;
        }
        else
        {
          if (szTmp)
            free(szTmp);
          return -1;;
        }
      }
      if (stTmpBuf < stTmpLen)
      {
        stTmpBuf = stTmpLen + 1;
        szTmp = (CHAR *)realloc(szTmp, stTmpBuf * sizeof(CHAR));
      }
      strcpy(szTmp, endpos);
      if (fFreeEndPos)
        free(endpos);
      strcpy(pos, szReplace);
      pos += stReplaceLen;
      strcat(pos, szTmp);
    }
    else  // only one match, right-aligned on szMain
    {
      *pos = '\0';
      if ((stMyBufLen - strlen(*szMain)) < (strlen(szFind) + 1))
      {// not enough space in the buffer; expand if possible
        if (!fHandleMem)
        {
          if (szTmp)
            free(szTmp);
          return -1;
        }
        stMyBufLen = strlen(*szMain) + strlen(szFind) + 1;
        *szMain = (CHAR *)realloc(*szMain, *stBufLen * sizeof(CHAR));
      }
      strcat(*szMain, szReplace);
    }
  }
  if (szTmp)
    free(szTmp);
  return ret;
}
void FlushDBLogs(bool fFlushAll)
{
  #ifndef DISABLE_DB_LOGGING
  XT_LockMutex(mmLogDBQ);
  if ((!fFlushAll) && (muiDBLogQueueLen < muiMinLogsForFlush))
  {
    XT_ReleaseMutex(mmLogDBQ);
    return;
  }
  XT_ReleaseMutex(mmLogDBQ);
  #ifdef _SIMPLEODBC_H_
  
  CSimpleODBC sql;
  sql.DisableLogging();
  XT_LockMutex(mmLogDBQ);
  if (!sql.Connect(msDBConnInfo.szHostName, msDBConnInfo.szDatabaseName, 
    msDBConnInfo.szUserName, msDBConnInfo.szPassword, msDBConnInfo.szDriver))
  {
    unsigned int uiAttemptsLeft = MAX_DB_CONNECT_FAILURES - muiFailedDBConnections++;
    if (uiAttemptsLeft)
      fprintf(stderr, "Unable to connect to database; will try %u more times (later)\n", uiAttemptsLeft);
    else
    {
      fprintf(stderr, "Connection to database fails terminally; disabling database logging\n");
      mfLogToDB = false;
		  mfLogDBThreadRunning = false;
    }
    XT_ReleaseMutex(mmLogDBQ);
    return;
  }
  // grab slice of db logs in queue, if any
	SLogItem **asliLocalDBLogs = asliDBLogs;
	asliDBLogs = NULL;
	unsigned int uiLogItems = muiDBLogQueueLen;
	muiDBLogQueueLen = 0;
  XT_ReleaseMutex(mmLogDBQ);
  
  if (uiLogItems == 0)
    return; // nothing to do eh
    
  size_t stAlloc = LI_SQL_CHUNK_ALLOC;
  if (strLogSQL.length() >= LI_SQL_CHUNK_ALLOC)
    stAlloc = strLogSQL.length() + LI_SQL_CHUNK_ALLOC;
  
  CHAR *szSQL = (CHAR *)_MALLOC(stAlloc * sizeof(CHAR));
  strcpy(szSQL, strLogSQL.c_str());
  CHAR szNumBuf[64];
  bool fDesc = (strstr(szSQL, LI_REPLACEVAR_DESC))?true:false;
  bool fLevel = (strstr(szSQL, LI_REPLACEVAR_LEVEL))?true:false;
  bool fErrNo = (strstr(szSQL, LI_REPLACEVAR_ERRNO))?true:false;
  
  unsigned int uiFail = 0;
  const CHAR *szQLogString;
  for (unsigned int i = 0; i < uiLogItems; i++)
  {
    if (fDesc)
    {  
      szQLogString = sql.Quote(asliLocalDBLogs[i]->szLogString);
      if (log_str_replace(&szSQL, LI_REPLACEVAR_DESC, szQLogString, &stAlloc, true) == -1)
      {
        sql.FreeBuffer(szQLogString);
        break;
      }
      sql.FreeBuffer(szQLogString);
    }
    if (fLevel)
    {
      sprintf(szNumBuf, "%u", (int)(asliLocalDBLogs[i]->uiSeverity));
      if (log_str_replace(&szSQL, LI_REPLACEVAR_LEVEL, szNumBuf, &stAlloc, true) == -1)
        break;
    }
    if (fErrNo)
    {
      sprintf(szNumBuf, "%i", (int)asliLocalDBLogs[i]->intErrorCode);
      if (log_str_replace(&szSQL, LI_REPLACEVAR_ERRNO, szNumBuf, &stAlloc, true) == -1)
        break;
    }
    if (!sql.Exec_NoSub(szSQL))
    {
      if (uiFail++ > 10)
        break;
    }
    if ((uiLogItems - i) > 1)
      strcpy(szSQL, strLogSQL.c_str());
  }
  
  _FREE(szSQL);
  
  // FREE queue slice mem  
  for (unsigned int i = 0; i < uiLogItems; i++)
  {
    _FREE(asliLocalDBLogs[i]->szLogString);
    _FREE(asliLocalDBLogs[i]);
  }
  if (asliLocalDBLogs)
    _FREE(asliLocalDBLogs);
  
  #endif
  #endif
}

bool isNumeric(const CHAR *sz)
{
  if (sz)
  {
    for (unsigned int i = 0; i < strlen(sz); i++)
      if (strchr("0123456789", sz[i]) == NULL)
        return false;
    return true;
  }
  else
    return false;
}

int GrokLogLevel(const CHAR *sz, int intDefaultLevel)
{
  if (isNumeric(sz))
    return atoi(sz);
  else
  {
    int ret;
    CHAR *szCopy = (CHAR *)malloc((1 + strlen(sz)) * sizeof(CHAR));
    strcpy(szCopy, sz);
    strlwr(szCopy);
    if (strcmp(szCopy, "debug") == 0)
      ret = LS_DEBUG;
    else if (strcmp(szCopy, "trivia") == 0)
      ret = LS_TRIVIA;
    else if (strcmp(szCopy, "notice") == 0)
      ret = LS_NOTICE;
    else if (strcmp(szCopy, "warning") == 0)
      ret = LS_WARNING;
    else if (strcmp(szCopy, "error") == 0)
      ret = LS_ERROR;
    else
      ret = intDefaultLevel;
    free(szCopy);
    return ret;
  }
}

void GetAppLogfileName(const CHAR *szArgv0, CHAR *szOut)
{ // call with argv[0] and a reasonable buffer to get a default log file name created for you
  strcpy(szOut, szArgv0);
  CHAR *pos = strrchr(szOut, PATH_DELIMITER_C);
  if (pos)
  {
    CHAR *pos2 = strstr(pos, ".");
    if (pos2)
      *pos2 = '\0';
  }
  strcat(szOut, ".log");
}

void SetLogLevel(unsigned int uiNewLevel)
{
  muiMinimumLoggingSeverity = uiNewLevel;
}

void SetColorLogging(bool fOn)
{
  mfColorLogging = fOn;
}

unsigned int GetLogLevel()
{
  return muiMinimumLoggingSeverity;
}

#if defined(WIN32) || defined(WIN64)
bool GetAppLogFileName(CHAR *szOut)
#else
bool GetAppLogFileName(CHAR *szOut, const char *szAppPath)
#endif
{
  #ifndef WIN32
  size_t stLen = strlen(szAppPath);
  #endif
  #if defined(WIN32) || defined(WIN64)
  size_t stLen = GetModuleFileNameA(NULL, szOut, MAX_PATH);
  #else
  if (stLen > (MAX_PATH-1))   // respect MAX_PATH, even on non-win32
    return false;
  #endif
  #ifndef WIN32
  // TODO: add support for determining the path if not absolute, via getcwd or similar
  // NOTE: this functionality is more than likely quite broken on Linux due to the fun
  //  of paths and symlinks; but there is a way to fix it. It's just not done (yet)
  strcpy(szOut, szAppPath);
  #endif
  
  CHAR *pos = strrchr(szOut, PATH_DELIMITER_C);
  pos = strrchr(pos, '.');
  if (pos != NULL)
    *pos = '\0';
  strcat(szOut, ".log");
  return true;
}


