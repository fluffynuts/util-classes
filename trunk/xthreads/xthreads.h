#ifndef _XTHREADS_H
#define _XTHREADS_H
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#include <process.h>
#define MUTEX_TYPE                  CRITICAL_SECTION*
#define THREAD_TYPE                 HANDLE //uintptr_t
#define THREAD_CB_RET               unsigned int WINAPI                      
#define THREAD_CB_RET_TYPE          unsigned int
#define THREAD_CB_RET_DEFAULTVAL    0
#define THREAD_EXIT_V(val)          {_endthreadex(val);return val;}
#define THREAD_EXIT                 {_endthreadex(0);return 0;}
#define THREAD_EXIT_VOID            {_endthreadex(0);return;}
typedef unsigned int (WINAPI *THREAD_CB_FN)(LPVOID lpThreadParameter);
#else
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#define MUTEX_TYPE                      pthread_mutex_t*
#define THREAD_TYPE                     pthread_t
#define THREAD_CB_RET                   void *
#define THREAD_CB_RET_TYPE              void *
#define THREAD_CB_RET_DEFAULTVAL        NULL
#define THREAD_EXIT_V(val)              {pthread_exit(val);return val;}
#define THREAD_EXIT                     {pthread_exit(NULL);return NULL;}
#define PTHREAD_WAIT_FOR_ONE_THREAD_MS	10
typedef void *(*THREAD_CB_FN)(void *data);
#endif

#define XT_THREAD_LIMIT_CEILING        64
#define XT_ERROR_INDEX                    128

void XT_Init();
void XT_Deinit();

void XT_LockMutex(MUTEX_TYPE m);
void XT_ReleaseMutex(MUTEX_TYPE m);
void XT_DestroyMutex(MUTEX_TYPE m);
MUTEX_TYPE XT_CreateMutex(bool fLock = false);
THREAD_TYPE XT_SpawnThread(THREAD_CB_FN routine, void *data, unsigned int uiMaxRunningThreads = XT_THREAD_LIMIT_CEILING, const CHAR *szPoolName = NULL);
unsigned int XT_WaitForOneThread(const CHAR *szPoolName = NULL);
void XT_WaitForAllThreads(unsigned int uiLeaveThisManyThreadsRunning = 0, const CHAR *szPoolName = NULL);
void XT_WaitForAllThreadPools(unsigned int uiLeaveThisManyThreadsRunning = 0);
bool XT_ThreadSafeBooleanCheck(MUTEX_TYPE m, bool *fFlag);
void XT_SpawnBackgroundProcess(const char *szCommand, unsigned int uiLimit = XT_THREAD_LIMIT_CEILING, const CHAR *szPoolName = NULL);
unsigned int XT_GetProcessorCount();
void XT_KillAllThreads();
unsigned int XT_RunningThreadCount(const CHAR *szPoolName = NULL);

#endif
