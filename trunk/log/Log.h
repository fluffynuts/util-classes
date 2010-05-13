#ifndef _LOG_H_   // multi-inclusion guard; also used by CommonFunctions to enable / disable logging
#define _LOG_H_
// I use some convenience functions from xthreads but not the actual spawning / thread limiting since I don't
//  want xthreads to count logging threads as part of the overall threadcount
#include "xthreads.h"
#ifndef DISABLE_DB_LOGGING    // define this at project level to enable database logging functionality
#include "SimpleODBC.h"
#endif

#ifndef DISABLE_INI_SETTINGS  // define this to disable configuration via standard settings in a CINIFile object
#include "inifile.h"
#endif

/*
*   Provides a fairly simple multi-output capable logging interface, effectively with C-style
*   (we don' need no stinkin' objects here!)
*   - This code is very win32-specific. 
*   - Can do threaded background logging and log file rotation.
*   - makes use of mutexes, not critical sections, simply because that's how I started, and
*     a mutex is a handle, so it can have a NULL value for when it's uninitialised (if there's
*     some special function which can check on the init stat of a CRITICAL_SECTION, I'll change
*     this... Tell me about it!
*
*   Basic usage:
   #include "Log.h"            // this file

   // MULTI-THREADED EXAMPLE
   int main(int argc, char *argv[])
   {
     // set logging options
     SetLogOptions(...);     // see SetLogOptions definition for more on what to put here
     // start background logging threads:
     StartLogFlushThreads();
     ...
     // do program logic, with inter-mingled Log() calls:
     ...
     Log(LS_DEBUG, "Some useless debug log here");
     ...
     // stop flush threads; this will do all necessary cleanup
     StopLogFlushThreads();
     return 0;
   }


   // SINGLE-THREADED EXAMPLE (eg: small, linear, small app)
   int main (int argc, char *argv[])
   {
     // set logging options
     SetLogOptions(...);    // don't forget to set fSingleThreaded, right at the end of the args list!
     ...
     // do program logic, with inter-mingled Log() calls:
     ...
     Log(LS_DEBUG, "Some useless debug log here");
     ...
     // Flush outstanding logs
     FlushAllLogs();
     // Rotate log files
     RotateLogs();
     return 0;
   }
*/

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <malloc.h>
#include <stdlib.h>
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#include <process.h>
#include <conio.h>
#else
#include <pthread.h>
#include <unistd.h>
#ifndef CHAR
#define CHAR char
#endif
#ifndef MAX_PATH
#define MAX_PATH	260
#endif
#endif
#include <time.h>
#include <errno.h>
#ifdef DO_MEM_ACCOUNTING
#include "mem_accounting.h" // optional: allows simple tracking of memory allocations with malloc, realloc and free
#endif
#include "ColorTerm.h"

// the def's below are to allow you to not depend on mem_accounting
#ifndef _FREE
  #define _FREE(addr) free(addr)
#endif
#ifndef _MALLOC
  #define _MALLOC(size) malloc(size)
#endif
#ifndef _REALLOC
  #define _REALLOC(addr, size) realloc(addr, size)
#endif

#define COLORED_OUTPUT      // undefine to prevent console coloured output
#if defined(WIN32) || defined(WIN64)
#define NEWLINE               "\r\n"
#else
#define NEWLINE				  "\n"
#endif
#define MAX_DATE_TIME_FORMAT_LEN 64
#define DEFAULT_TIME_FORMAT		"%Y-%m-%d %H:%M:%S"
#define MAX_FILE_IO_SIZE		 32768
// logging severity definitions
#define LS_DEFAULT            30            // default log level (NOTICE)
#define LS_DEFAULT_NAME       "notice"
#define LS_DEBUG              10
#define LS_TRIVIA             20
#define LS_NOTICE             30
#define LS_WARNING            40
#define LS_ERROR              50
#define LS_NONE               100

#define LEC_FILEACCESS        1
#define LEC_LIBERROR          2

#define LOG(str) (Log(LS_NOTICE, str))

#define DEFAULT_LOG_FLUSH_INTERVAL	  5
#define DEFAULT_MAX_LOG_SIZE		      512
#define DEFAULT_MAX_ARCHIVED_LOGS	    8
#define DEFAULT_MIN_LOGS_FOR_FLUSH    0     // don't flush logs until the queue is at least this long
#define DEFAULT_MAXLEN_DBLOG_DESC_COL 255   // max length for log strings; truncation is done for you

#define MAX_LEN_DB_CONNSTR          1024  //max len of a db connection string

#ifndef DISABLE_INI_SETTINGS
// basics
#define LI_SECTION_DATABASE         "database"
#define LI_SECTION_LOGGING          "logging"
#define LI_SETTING_LEVEL            "level"
#define LI_SETTING_SCREEN           "screen"
#define LI_SETTING_FILE             "file"
#define LI_SETTING_DB               "db"
#define LI_SETTING_LOGFILE          "logfile"
#define LI_SETTING_BUFFER           "buffer"
#define LI_SETTING_FLUSH_INTERVAL   "flush_interval"
// tweaks
#define LI_SETTING_DATESTAMP        "datestamp"
#define LI_SETTING_ARCHIVE          "max_file_archives"
#define LI_SETTING_LOGSIZE          "max_file_size_kb"
#define LI_SETTING_DBHOST           "dbhost"
#define LI_SETTING_DBNAME           "dbname"
#define LI_SETTING_DBUSER           "dbuser"
#define LI_SETTING_DBPASS           "dbpass"
#define LI_SETTING_DBDRIVER         "dbdriver"
#define LI_SETTING_LOGSQL           "logsql"
#define LI_SETTING_DATESTAMPFORMAT  "datestamp_format"
#define LI_SETTING_THREADED         "threaded"

// defaults
#define LI_DEFAULT_LEVEL            "notice"
#define LI_DEFAULT_SCREEN           true
#define LI_DEFAULT_FILE             true
#define LI_DEFAULT_DB               false
#define LI_DEFAULT_ARCHIVE          7
#define LI_DEFAULT_FLUSH            1
#define LI_DEFAULT_LOGSIZE          512
#define LI_DEFAULT_DATESTAMP        true
#define LI_DEFAULT_THREADED         true
#define LI_DEFAULT_BUFFER           1
#define LI_DEFAULT_DBDRIVER         "SQL Server"

#define LI_SQL_CHUNK_ALLOC          4096
#define LI_REPLACEVAR_DESC          "%desc%"
#define LI_REPLACEVAR_LEVEL         "%level%"
#define LI_REPLACEVAR_ERRNO         "%errno%"
#endif

#ifdef _SIMPLEODBC_H_
struct SDatabaseConnectionInfo
{
  CHAR szHostName[MAX_LEN_DB_CONNSTR + 1];
  CHAR szDatabaseName[64];
  CHAR szUserName[32];
  CHAR szPassword[128];
  CHAR szDriver[64];
  SDatabaseConnectionInfo()
  {
    strcpy(this->szHostName, "localhost");
    strcpy(this->szDatabaseName, "master");
    strcpy(this->szUserName, "sa");
    this->szPassword[0] = '\0';
    strcpy(this->szDriver, DEFAULT_ODBC_DRIVER);
  }
  SDatabaseConnectionInfo(const CHAR *szNewHostname, const CHAR *szNewDatabaseName, const CHAR *szNewUsername, 
    const CHAR *szNewPassword, const CHAR *szNewDriver = DEFAULT_ODBC_DRIVER)
  {
    strcpy(this->szHostName, szNewHostname);
    strcpy(this->szDatabaseName, szNewDatabaseName);
    strcpy(this->szUserName, szNewUsername);
    strcpy(this->szPassword, szNewPassword);
    if (szNewDriver && strlen(szNewDriver))
      strcpy(this->szDriver, szNewDriver);
    else
      strcpy(this->szDriver, DEFAULT_ODBC_DRIVER);
  }
  void Copy(SDatabaseConnectionInfo *src)
  {
    strcpy(this->szHostName, src->szHostName);
    strcpy(this->szDatabaseName, src->szDatabaseName);
    strcpy(this->szUserName, src->szUserName);
    strcpy(this->szPassword, src->szPassword);
    if (src->szDriver && strlen(src->szDriver))
      strcpy(this->szDriver, src->szDriver);
    else
      strcpy(this->szDriver, DEFAULT_ODBC_DRIVER);
  }
};
#endif


void Log(unsigned int uiSeverity, int intErrorCode, const CHAR *szFormattedLogString,...);
void Log(unsigned int uiSeverity, const CHAR *szFormattedLogString,...);
void LogS(unsigned int uiSeverity, const CHAR *szLogString);
void Log_Status(unsigned int uiSeverity, const CHAR *szFormattedLogString, ...);
//  See definition of SetLogOptions for more detailed information
void SetLogOptions(
  unsigned int uiMinimumLogLevel = LS_DEBUG,                      // log everything from DEBUG up
  bool fLogToFile = false,                                        // don't log to file
  bool fLogToStdOut = true,                                       // do log to stdout
  const CHAR *szLogFile = NULL,                                   // base filename to log to if fLogToFile is set
  unsigned int uiFlushIntervalSecs = DEFAULT_LOG_FLUSH_INTERVAL,  // interval between flushes on all log queues
  bool fDateStampLogs = true,                                     // add timestamp to logs (only applies really to file / stdout; you can do your own timestamping for DB)
  const CHAR *szLogDateTimeFormat = NULL,                         // datestamp format (as expected by strftime())
  unsigned int uiMaxLogfileSizeKB = DEFAULT_MAX_LOG_SIZE,         // max size of log files before they are rotated 
  unsigned int uiMaxArchivedLogs = DEFAULT_MAX_ARCHIVED_LOGS,     // max number of log files to archive before deleting
  bool fSingleThreaded = false,                                   // act as a single-threaded app (StartLogThreads will bitch, and you will have to manually flush logs (at least at the end of your app) if you have a uiMinLogsForFlush > 1
  unsigned int uiMinLogsForFlush = 0                              // min. logs in any queue before that queue is flushed
  #ifdef _SIMPLEODBC_H_
  ,bool fLogToDB = false,                                         // don't log to database
  SDatabaseConnectionInfo *sDBConnInfo = NULL,                    // database connection info if fLogToDB is set
  const CHAR *szLogSQL = NULL                                     // SQL string to use to do DB logging; check SetLogOptions definition for usage
  #endif
);                              
#ifndef DISABLE_INI_SETTINGS
void SetLogOptions(CINIFile *ini);
#endif

void StartLogFlushThreads();
void StopLogFlushThreads();
bool LoggingThreadsRunning();
void FlushLogs(bool fForce = true);
void RotateLogs();
bool isNumeric(const CHAR *sz);
int GrokLogLevel(const CHAR *sz, int intDefaultLevel);
void GetAppLogfileName(const CHAR *szArgv0, CHAR *szOut);
void SetLogLevel(unsigned int uiNewLevel);
void SetColorLogging(bool fOn = true);
void PauseScreenLogging();
void ResumeScreenLogging();
unsigned int GetLogLevel();

#if defined(WIN32) || defined(WIN64)
bool GetAppLogFileName(CHAR *szOut);
#else
bool GetAppLogFileName(CHAR *szOut, const char *szAppPath);
void Sleep(unsigned long ulMicroSeconds);
#endif

THREAD_CB_RET StopLogFlushThreadsWorker(void *ptr);

#endif
