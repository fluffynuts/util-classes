#include "xthreads.h"

// thread abstraction
//  note that I've chosen CRITICAL_SECTIONs over mutexes in windows
//  apparently mutexes in windows are a lot slower -- though you can
//  share them between apps by naming them.

class CThreadPool {
  public:
    CHAR *Name;
    THREAD_TYPE Threads[XT_THREAD_LIMIT_CEILING];
    MUTEX_TYPE Lock;
    unsigned int RunningThreads;
    CThreadPool(const CHAR *szName)
    {
      if (szName)
      {
        this->Name = (CHAR *)malloc((1 + strlen(szName)) * sizeof(CHAR));
        strcpy(this->Name, szName);
      }
      else
      {
        this->Name = (CHAR *)malloc(sizeof(CHAR));
        this->Name[0] = '\0';
      }
      this->Lock = XT_CreateMutex(false);
      for (size_t i = 0; i < XT_THREAD_LIMIT_CEILING; i++)
        this->Threads[i] = 0;
      this->RunningThreads = 0;
    }
    ~CThreadPool()
    {
      if (this->Name)
        free(this->Name);
      XT_LockMutex(this->Lock);
      for (size_t i = 0; i < XT_THREAD_LIMIT_CEILING; i++)
        if (this->Threads[i]) CloseHandle((HANDLE)(this->Threads[i]));
      XT_ReleaseMutex(this->Lock);
      XT_DestroyMutex(this->Lock);
    }
};

#define ENABLE_THREAD_POOLS

CThreadPool **XT_ThreadPool = NULL;
size_t        XT_ThreadPoolCount = 0;
MUTEX_TYPE    XT_ThreadPoolLock;

THREAD_TYPE XT_Threads[XT_THREAD_LIMIT_CEILING];
unsigned int XT_RunningThreads = 0;
MUTEX_TYPE XT_ThreadLock;
long XT_Initialised = 0;

bool XT_GetThreadPool(const CHAR *szPoolName, THREAD_TYPE **Threads, MUTEX_TYPE *Lock, unsigned int **RunningThreads, bool CreateIfRequired = false);

THREAD_CB_RET XT_RunSystemCommand(void *ptr)
{
  if (ptr)
  {
    char *szCmd = (char *)(ptr);
    int ret = system(szCmd);
    free(szCmd);
		#if defined(WIN32) || defined(WIN64)
    THREAD_EXIT(ret);
		#else
		THREAD_EXIT((void*)ret);
		#endif
  }
  THREAD_EXIT_V(NULL);
}

#if defined(WIN32) || defined(WIN64)
MUTEX_TYPE XT_CreateMutex(bool fLock)
{
  CRITICAL_SECTION *m = new CRITICAL_SECTION;
  InitializeCriticalSection(m);
  if (fLock)
    XT_LockMutex(m);
  return m;
}
void XT_LockMutex(MUTEX_TYPE m)
{
  if (m)
    EnterCriticalSection(m);
}

void XT_ReleaseMutex(MUTEX_TYPE m)
{
  if (m)
    LeaveCriticalSection(m);
}
void XT_DestroyMutex(MUTEX_TYPE m)
{
  if (m)
	  DeleteCriticalSection(m);
	delete m;
	m = NULL;
}
unsigned int XT_WaitForOneThread(const CHAR *szPoolName)
{
  if (XT_Initialised == 0)
    return 0;
  THREAD_TYPE *Threads = NULL; // XT_Threads;
  unsigned int *RunningThreads = NULL; //&XT_RunningThreads;
  MUTEX_TYPE ThreadLock = NULL; //XT_ThreadLock;
  #ifdef ENABLE_THREAD_POOLS
  if(szPoolName)
  #else
  if (false)
  #endif
  {
    XT_LockMutex(XT_ThreadLock);   // global xt thread lock
    if (!XT_GetThreadPool(szPoolName, &Threads, &ThreadLock, &RunningThreads))
    {
      XT_ReleaseMutex(XT_ThreadLock); // releas global thread lock; thread pool is unknown
      return 0;
    }
    XT_LockMutex(ThreadLock); // lock pool-specific threadlock
    XT_ReleaseMutex(XT_ThreadLock); // release global threadlock
  }
  else
  {
    XT_LockMutex(XT_ThreadLock);
    Threads = XT_Threads;
    RunningThreads = &XT_RunningThreads;
    ThreadLock = XT_ThreadLock;
  }
  if (*RunningThreads == 0)
  {
    XT_ReleaseMutex(ThreadLock);  // no running threads in this pool; exit out
    return 0;
  }
  unsigned int uiRet = 0;
  FILETIME CreationTime;
  FILETIME ExitTime;
  FILETIME KernelTime;
  FILETIME UserTime;
  
  for (unsigned int i = 0; i < *RunningThreads; i++)
  { // look for an invalid handle -- _beginthreadex kindly gives us one when the thread exits "too quickly"
    if (GetThreadTimes((HANDLE)Threads[i], &CreationTime, &ExitTime, &KernelTime, &UserTime) == FALSE)
    { // thread cannot be found; must have terminated
      uiRet = i;
      CloseHandle((HANDLE)Threads[i]);
      for (unsigned int j = i; j < *RunningThreads; j++)
        Threads[j] = Threads[j+1];
      (*RunningThreads)--;
      Threads[*RunningThreads] = NULL;
      XT_ReleaseMutex(ThreadLock);
      return uiRet;
    }
  }
  DWORD dwWait;
  //dwWait = WaitForMultipleObjects(XT_RunningThreads, (const HANDLE*)Threads, false, INFINITE);
  while (true)
  {
    dwWait = WaitForMultipleObjects(*RunningThreads, Threads, false, 1000);
    if (dwWait == WAIT_TIMEOUT)
    { // wait timed out -- check for a dead thread in the queue
      for (unsigned int i = 0; i < *RunningThreads; i++)
      {
        // consider a thread dead if the id doesn't belong to this process
        if (GetThreadTimes((HANDLE)Threads[i], &CreationTime, &ExitTime, &KernelTime, &UserTime) == FALSE)
        {
          uiRet = i;
          CloseHandle((HANDLE)Threads[i]);
          for (unsigned int j = i; j < *RunningThreads; j++)
            Threads[j] = Threads[j+1];
          (*RunningThreads)--;
          Threads[*RunningThreads] = NULL;
          XT_ReleaseMutex(ThreadLock);
          return uiRet;
        }
      }
      continue;
    }
    if (dwWait != WAIT_FAILED)
      break;
  }
  if (dwWait == WAIT_FAILED)
  {
    fprintf(stderr, "Wait for one thread fails (%li)!\n", (long)dwWait);
    uiRet = XT_ERROR_INDEX;
  }
  else
  {
    uiRet = dwWait - WAIT_OBJECT_0;
    CloseHandle((HANDLE)(Threads[uiRet]));
    for (unsigned int i = uiRet; i < *RunningThreads; i++)
      Threads[i] = Threads[i + 1];
    (*RunningThreads)--;
    Threads[(*RunningThreads)] = NULL;
  }
  XT_ReleaseMutex(ThreadLock);
  return uiRet;
}
#else
void XT_LockMutex(pthread_mutex_t *m)
{
	if (m)
		pthread_mutex_lock(m);
}
void XT_ReleaseMutex(pthread_mutex_t *m)
{
	if (m)
		pthread_mutex_unlock(m);
}
pthread_mutex_t *XT_CreateMutex(bool fLock)
{
	pthread_mutex_t *ret = new pthread_mutex_t;
	pthread_mutex_init(ret, NULL);
	if (fLock)
		XT_LockMutex(ret);
	return ret;
}
void XT_DestroyMutex(pthread_mutex_t *m)
{
	if (m)
	{
		pthread_mutex_destroy(m);
		delete m;
		m = NULL;
	}
}
unsigned int XT_WaitForOneThread(const CHAR *szPoolName)
{   // pthreads version (doesn't do pools -- yet)
  XT_LockMutex(XT_ThreadLock);
  if (XT_RunningThreads == 0)
  {
    XT_ReleaseMutex(XT_ThreadLock);
    return 0;
  }
  unsigned int uiRet = 0;

  uiRet = XT_RunningThreads;
  XT_ReleaseMutex(XT_ThreadLock);
  int intRestrict;
  struct sched_param sp;
  while (true)
  {
    XT_LockMutex(XT_ThreadLock);
		bool fSomethingRunning = false;
    for (unsigned int i = 0; i < XT_THREAD_LIMIT_CEILING; i++)
    {
			if (XT_Threads[i] == 0)
				continue;
			fSomethingRunning = true;
      if (pthread_getschedparam(XT_Threads[i], &intRestrict, &sp) == ESRCH)
      {
        uiRet = i;
				XT_Threads[i] = 0;
				XT_RunningThreads--;
        break;
      }
    }
    XT_ReleaseMutex(XT_ThreadLock);
    if (uiRet || !fSomethingRunning)
      break;
    #if defined(WIN32) || defined(WIN64)
    Sleep(PTHREAD_WAIT_FOR_ONE_THREAD_MS);
    #else
    usleep(PTHREAD_WAIT_FOR_ONE_THREAD_MS * 1000);
    #endif
  }
  return uiRet;
}

#endif

void XT_Init()
{
  if (XT_Initialised++)
    return;
  XT_ThreadLock = XT_CreateMutex(false);
  XT_ThreadPoolLock = XT_CreateMutex(false);
	XT_RunningThreads = 0;
	for (unsigned int i = 0; i < XT_THREAD_LIMIT_CEILING; i++)
		XT_Threads[i] = 0;
}

void XT_Deinit()
{
  if (XT_Initialised == 0)
  {
    #if defined(WIN32) || defined(WIN64)
    XT_LockMutex(XT_ThreadLock);
    for (unsigned int i = 0; i < XT_RunningThreads; i++)
      CloseHandle((HANDLE)(XT_Threads[i]));
    XT_ReleaseMutex(XT_ThreadLock);
    
    XT_LockMutex(XT_ThreadPoolLock);
    for (size_t i = 0; i < XT_ThreadPoolCount; i++)
      delete XT_ThreadPool[i];
    free(XT_ThreadPool);
    XT_ThreadPool = NULL;  
    XT_ThreadPoolCount = 0;
    XT_ReleaseMutex(XT_ThreadPoolLock);
    #endif
    XT_DestroyMutex(XT_ThreadLock);
    XT_DestroyMutex(XT_ThreadPoolLock);
    XT_ThreadLock = NULL;
  }
  else
    XT_Initialised--;
}

THREAD_TYPE XT_SpawnThread(THREAD_CB_FN routine, void *data, unsigned int uiMaxRunningThreads, const CHAR *szPoolName)
{
  THREAD_TYPE ret = 0;
  if (uiMaxRunningThreads > XT_THREAD_LIMIT_CEILING)
    uiMaxRunningThreads = XT_THREAD_LIMIT_CEILING;
  
  unsigned int *RunningThreads = NULL; //&XT_RunningThreads;
  THREAD_TYPE *Threads = NULL; //XT_Threads;
  MUTEX_TYPE ThreadLock = NULL; //XT_ThreadLock;
  #ifdef ENABLE_THREAD_POOLS
  if (szPoolName)
  #else
  if (false)
  #endif
  {
    XT_LockMutex(XT_ThreadLock);
    if (!XT_GetThreadPool(szPoolName, &Threads, &ThreadLock, &RunningThreads, true))
    {
      XT_ReleaseMutex(XT_ThreadLock);
      return 0;
    }
    XT_ReleaseMutex(XT_ThreadLock);
  }
  else
  {
    RunningThreads = &XT_RunningThreads;
    ThreadLock = XT_ThreadLock;
    Threads = XT_Threads;
  }
  while (*RunningThreads >= uiMaxRunningThreads)
	{
    XT_WaitForOneThread(szPoolName);
  }   
  XT_LockMutex(ThreadLock);
  #ifdef _PTHREAD_H
	int idx = -1;
	for (unsigned int i = 0; i < uiMaxRunningThreads; i++)
	{
		if (XT_Threads[i] == 0)
		{
			idx = (int)i;
			break;
		}
	}
	if (idx > -1)
	{
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_t pt;
		pthread_create(&pt, &attr, routine, data);
		XT_Threads[idx] = pt;
		pthread_attr_destroy(&attr);
	}
	else
	{
		ret = XT_ERROR_INDEX;
		fprintf(stderr, "Unable to find a slot to spawn thread\n");
	}
  #else
  
  Threads[*RunningThreads] = (HANDLE)(_beginthreadex(NULL, 0, routine, data, CREATE_SUSPENDED, 0));
  Sleep(0);
  ResumeThread((HANDLE)(Threads[*RunningThreads]));
  #endif
	(*RunningThreads)++;
  XT_ReleaseMutex(ThreadLock);
  return ret;
}

void XT_WaitForAllThreads(unsigned int uiLeaveThisManyThreadsRunning, const CHAR *szPoolName)
{
  while (true)
  {
    #ifdef ENABLE_THREAD_POOLS
    if (szPoolName)
    #else
    if (false)
    #endif
    {
      THREAD_TYPE *Threads = NULL;
      MUTEX_TYPE Lock = NULL;
      unsigned int *RunningThreads = NULL;
      if (XT_GetThreadPool(szPoolName, &Threads, &Lock, &RunningThreads, false))
      {
        if (*RunningThreads <= uiLeaveThisManyThreadsRunning)
          return;
        XT_WaitForOneThread(szPoolName);
      }
      else
        return; // this pool doesn't exist; there is nothing to wait for
    }
    else
    {
      XT_LockMutex(XT_ThreadLock);
      if (XT_RunningThreads <= uiLeaveThisManyThreadsRunning)
      {
        XT_ReleaseMutex(XT_ThreadLock);
        return;
      }
      XT_ReleaseMutex(XT_ThreadLock);
      XT_WaitForOneThread(szPoolName);
    }
  }
}

void XT_WaitForAllThreadPools(unsigned int uiLeaveThisManyThreadsRunning)
{
  // wait on the default queue
  XT_WaitForAllThreads(uiLeaveThisManyThreadsRunning);
  // do each pool
  CHAR **aszPools = NULL;
  size_t stPools = 0;
  XT_LockMutex(XT_ThreadPoolLock);
  if (XT_ThreadPoolCount)
  {
    aszPools = (CHAR **)malloc(XT_ThreadPoolCount * sizeof(CHAR*));
    stPools = XT_ThreadPoolCount;
    for (size_t i = 0; i < XT_ThreadPoolCount; i++)
    {
      aszPools[i] = (CHAR *)malloc((1 + strlen(XT_ThreadPool[i]->Name)) * sizeof(CHAR));
      strcpy(aszPools[i], XT_ThreadPool[i]->Name);
    }
  }
  XT_ReleaseMutex(XT_ThreadPoolLock);
  
  for (size_t i = 0; i < stPools; i++)
    XT_WaitForAllThreads(uiLeaveThisManyThreadsRunning, aszPools[i]);
  
  if (stPools)
  {
    for (size_t i = 0; i < stPools; i++)
      free(aszPools[i]);
    free(aszPools);
  }
}

// convenience functions
bool XT_ThreadSafeBooleanCheck(MUTEX_TYPE m, bool *fFlag)
{
  if (m)
  {
    XT_LockMutex(m);
    bool fRet = *fFlag;
    XT_ReleaseMutex(m);
    return fRet;
  }
  else
    return *fFlag;
}

void XT_SpawnBackgroundProcess(const char *szCommand, unsigned int uiLimit, const CHAR *szPoolName)
{
  char *szCmd = (char *)malloc((1 + strlen(szCommand)) * sizeof(char));
  strcpy(szCmd, szCommand);
  XT_SpawnThread(XT_RunSystemCommand, szCmd, uiLimit, szPoolName);
}

unsigned int XT_GetProcessorCount()
{
  #if defined(WIN32) || defined(WIN64)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors;
  #else
  fprintf(stderr, "WARNING: XT_GetProcessorCount not implemented for non-win32 (yet); returning 1\n");
  return 1;
  #endif
}

void XT_KillAllThreads()
{
  #if defined(WIN32) || defined(Win64)
  XT_LockMutex(XT_ThreadLock);
  for (unsigned int i = 0; i < XT_RunningThreads; i++)
  {
    TerminateThread((HANDLE)(XT_Threads[i]), 0);
    CloseHandle((HANDLE)XT_Threads[i]);
    XT_Threads[i] = NULL;
  }
  XT_RunningThreads = 0;
  XT_ReleaseMutex(XT_ThreadLock);
  XT_LockMutex(XT_ThreadPoolLock);
  for (unsigned int i = 0; i < XT_ThreadPoolCount; i++)
  {
    XT_LockMutex(XT_ThreadPool[i]->Lock);
    for (unsigned int j = 0; j < XT_ThreadPool[i]->RunningThreads; j++)
      TerminateThread((HANDLE)(XT_ThreadPool[i]->Threads[j]), 0);
    XT_ReleaseMutex(XT_ThreadPool[i]->Lock);
    delete XT_ThreadPool[i];
  } 
  free(XT_ThreadPool);
  XT_ThreadPool = NULL;
  XT_ThreadPoolCount = 0;
  XT_ReleaseMutex(XT_ThreadPoolLock);
  #else
  fprintf(stderr, "\nXT_KillAllThreads not implemented for this platform (yet)\n");
  #endif
}

bool XT_GetThreadPool(const CHAR *szPoolName, THREAD_TYPE **Threads, MUTEX_TYPE *Lock, unsigned int **RunningThreads, bool CreateIfRequired)
{
  // gets a thread pool (if possible), creating it (if necessary)
  if (szPoolName == NULL)
    return false;
  bool ret = false;
  XT_LockMutex(XT_ThreadPoolLock);
  for (unsigned int i = 0; i < XT_ThreadPoolCount; i++)
  {
    if (strcmp(XT_ThreadPool[i]->Name, szPoolName) == 0)
    {
      ret = true;
      *Threads = XT_ThreadPool[i]->Threads;
      *Lock = XT_ThreadPool[i]->Lock;
      *RunningThreads = &(XT_ThreadPool[i]->RunningThreads);
      ret = true;
      break;
    }
  }
  if (!ret && CreateIfRequired)
  {
    CThreadPool *tmp = new CThreadPool(szPoolName);
    XT_ThreadPool = (CThreadPool **)realloc(XT_ThreadPool, (XT_ThreadPoolCount + 1) * sizeof(CThreadPool *));
    XT_ThreadPool[XT_ThreadPoolCount++] = tmp;
    ret = true;
    *Threads = tmp->Threads;
    *Lock = tmp->Lock;
    *RunningThreads = &(tmp->RunningThreads);
  }
  XT_ReleaseMutex(XT_ThreadPoolLock);
  return ret;
}

unsigned int XT_RunningThreadCount(const CHAR *szPoolName)
{
  return 0;
  XT_LockMutex(XT_ThreadLock);   // global xt thread lock
  if (szPoolName)
  {
    THREAD_TYPE *Threads = NULL;
    unsigned int *RunningThreads = NULL;
    MUTEX_TYPE ThreadLock = NULL;
    if (!XT_GetThreadPool(szPoolName, &Threads, &ThreadLock, &RunningThreads))
    { // unknown pool (perhaps just not created yet)
      XT_ReleaseMutex(XT_ThreadLock);
      return 0;
    }
    XT_LockMutex(ThreadLock);
    XT_ReleaseMutex(XT_ThreadLock);
    unsigned int ret = *RunningThreads;
    XT_ReleaseMutex(ThreadLock);
    return ret;
  }
  else
  {
    unsigned int ret = XT_RunningThreads;
    XT_ReleaseMutex(XT_ThreadLock);
    return ret;
  }
}