#ifndef _INIFILE_H_
#define _INIFILE_H_
//  port of inifile to drop all use of std::string and std::vector
//  - the M$ implementation is slow
//  - BoundsChecker spends far too much time in this arena -- because it's slow
//  NOTE: this class is not thread-safe. You will have to lock instances of it
//        if you want to share it between threads in a non-readonly manner
// uncomment below to get some console output
//#define _SHOW_DEBUG_INFO_INI
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(WIN32) || defined (_W32_WCE)
  #include <windows.h>      // required for dir enumeration stuff, but you should be referencing this anyways, to get WIN32 defined...
  #include <direct.h>

  // windows compat stuff
  #define ssize_t long
  // ok, I don't get the point of "supporting" POSIX funcs when you rename them and spew warnings. Typical M$
  #define getcwd(a,b) _getcwd(a,b)
  #define chdir(a) _chdir(a)
  #define NEWLINE "\r\n"
#else
  #include <unistd.h>
  #include <dirent.h>
  #define NEWLINE "\n"
  #ifndef CHAR
  #define CHAR char
  #endif
  #ifndef MAX_PATH
  #define MAX_PATH 260
  #endif
#endif

#ifndef PATH_DELIMITER
  #if defined (WIN32) || defined (_W32_WCE)
    #define PATH_DELIMITER "\\"
  #else
    #define PATH_DELIMITER "/"
  #endif
#endif
#ifndef PATH_DELIMITER_C
  #if defined (WIN32) || defined (_W32_WCE)
    #define PATH_DELIMITER_C '\\'
  #else
    #define PATH_DELIMITER_C '/'
  #endif
#endif

#define MEMORY_FILENAME      ":memory:"

#ifndef INI_SECTION_ALLOC_CHUNK
#define INI_SECTION_ALLOC_CHUNK 512
#endif

#ifndef INI_SETTING_ALLOC_CHUNK
#define INI_SETTING_ALLOC_CHUNK 512
#endif

#ifndef INI_RENDER_ALLOC_CHUNK
#define INI_RENDER_ALLOC_CHUNK  16384
#endif

#ifndef INI_STRINGPOOL_ALLOC
#define INI_STRINGPOOL_ALLOC  256
#endif

#ifndef INI_MAX_FOPEN_ATTEMPTS
#define INI_MAX_FOPEN_ATTEMPTS      10
#endif
#ifndef INI_SLEEP_FOPEN_FAILS
#define INI_SLEEP_FOPEN_FAILS      1
#endif
#ifndef INI_MAX_FILE_OPER_BYTES
#define INI_MAX_FILE_OPER_BYTES    32768
#endif

#define CLONESTR(szDst, szSrc) \
  do { \
    size_t stLen = 0; \
    if (szSrc) \
      stLen += strlen(szSrc) + 1; \
    if (stLen) \
      szDst = (CHAR *)realloc(szDst, stLen * sizeof(CHAR)); \
    if (szSrc) \
      strcpy(szDst, szSrc); \
    else \
    { \
      if (szDst) \
        free(szDst); \
      szDst = NULL; \
    } \
  } while (0)
#define RELEASECLONE(sz) \
  do { \
    if (sz) \
    { \
      free(sz); \
      sz = NULL; \
    } \
  } while (0)

class CINIFile
{
  public:
    class CIniFileSection;
  private:
    friend class CIniFileSection;
    struct SLookup {
      size_t Index;
      CHAR *Name;
      SLookup (const CHAR *szName, size_t stIndex)
      {
        this->Name = NULL;
        CLONESTR(this->Name, szName);
        this->Index = stIndex;
      }
     ~SLookup()
     {
       RELEASECLONE(this->Name);
     }
    };
  public:
    class CIniFileItem
    {
      public:
        CHAR *Name;
        CHAR *Value;
        CHAR *Comment;
        bool MemOnly;
        bool IsLineComment;
        bool ChangedValue;
        bool ChangedComment;
        bool ChangedName;
        CIniFileItem()
        {
          this->Name = NULL;
          this->Value = NULL;
          this->Comment = NULL;
          this->IsLineComment = false;
          this->MemOnly = false;
          this->ChangedValue = false;
          this->ChangedName = false;
          this->ChangedComment = false;
        }
        ~CIniFileItem()
        {
          if (this->ChangedName)
            RELEASECLONE(this->Name);
          if (this->ChangedValue)
            RELEASECLONE(this->Value);
          if (this->ChangedComment)
            RELEASECLONE(this->Comment);
        }
    };

    class CIniFileSection
    {
      public:
        CHAR *Name;
        CHAR *Alias;
        CINIFile::CIniFileItem **Settings;
        size_t SettingCount;
        CINIFile *owner;
        bool ChangedName;
        bool ChangedAlias;

        CIniFileSection(const CHAR *szName = NULL, const CHAR *szAlias = NULL, 
            CINIFile::CIniFileItem **asSettings = NULL, size_t stSettings = 0);
        ~CIniFileSection();

        void PushSetting(CINIFile::CIniFileItem *ii);
        void DelSetting(const CHAR *szName);
        long long SettingIndex(const CHAR *szName);
      private:
        size_t mstSettingAlloc;
        size_t mstSettingIndex;
        size_t mstSettingIndexAlloc;
        SLookup **masSettingIndex;
        bool mfIndexed;
        void IndexSettings();
    };

    CINIFile(const CHAR *szFileName = NULL);
    ~CINIFile();
    bool ExtendedSyntax;              // include extended syntax (#addpath, #include)
    bool IncludesOverride;            // use first occurrence of a parameter is kept when another occurrence happens
    bool AddFilePathToIncludePath;    // allow inclusion relative to ini file's dir
    bool MergeAllPossibleIncludes;   // don't stop with the first match for an #include or #addpath -- merge all that can be found
    bool CaseSensitive;             // determines whether or not section and value lookups are case sensitive (default false)
    bool ValueRetentionEnabled;       // enable retention of default values for file writing
    unsigned int  DecimalPlaces;      // how many decimal places to use when saving a floating point number to file
    #if defined(WIN32) || defined(WIN64)
    const CHAR *GetAppINIFileName();
    #else
    const CHAR *GetAppINIFileName(const char *szAppPath);
    #endif
    // value getters
    const CHAR *GetSZValue(const CHAR *szSection, const CHAR *szName, const CHAR *szDefault = NULL,
                    bool blnRetainDefault = false);
    unsigned int  GetUIntValue(const CHAR *szSection, const CHAR *szName, unsigned int uiDefaultValue = 0, 
      bool blnRetainDefault = false);
    bool GetBoolValue(const CHAR *szSection, const CHAR *szName, bool fDefaultFalue = false, 
      bool blnRetainDefault = false);
    double        GetDoubleValue(const CHAR *szSection, const CHAR *szName, double dblDefaultValue = 0.0, 
      bool blnRetainDefault = false);
    long long GetLongLongValue(const CHAR *szSection, const CHAR *szName, long long llDefaultValue = 0, 
      bool blnRetainDefault = false);
    
    // override value getters: gets values based on a priority list of sections to search
    const CHAR *GetOverrideSZValue(const CHAR *szSections, const CHAR *szName, const CHAR *szDefaultValue = "",
                    const CHAR *szSectionDelimiter = ",");
    bool GetOverrideBoolValue(const CHAR *szSections, const CHAR *szName, bool fDefaultValue = false,
                    const CHAR *szSectionDelimiter = ",");
    double GetOverrideDoubleValue(const CHAR *szSections, const CHAR *szName, double dblDefaultVal = 0,
                    const CHAR *szSectionDelimiter = ",");
    long long GetOverrideLongLongValue(const CHAR *szSections, const CHAR *szName, long long llDefaultVal = 0,
                    const CHAR *szSectionDelimiter = ",");
    // get a numbered value from a section as a const CHAR *; when you get szDefault back, we're out of values,
    //    assuming that there are no gaps in the numbering
    const CHAR    *GetNumberedSZValue(const CHAR *szSection, const CHAR *szBaseName, unsigned long ulIndex, 
                    const CHAR *szDefault = NULL);
    
    // file / string loading
    bool LoadBuffer(const CHAR *szSrc, size_t stSrcLen, bool fMerge = false);  // loads a buffer or part therof (doesn't have to be NULL-terminated)
    bool LoadFile(const CHAR *szFileName, bool fMerge = false);
    bool LoadSZ(const CHAR *szIniData, bool fMerge = false);
    bool Loaded();
    
    // INI searching / enumeration
    bool ValueExists(const CHAR *szSection, const CHAR *szName);
    bool SectionExists(const CHAR *szSection);
    size_t ListSections(const CHAR ***aszOut, const CHAR *szStartsWith = NULL, const CHAR *szContains = NULL,
                      const CHAR *szEndsWith = NULL, bool fCaseSensitive = false);
    size_t ListSettings(const CHAR ***aszOut, const CHAR *szSection, const CHAR *szStartsWith, const CHAR *szContains,
                      const CHAR *szEndsWith, bool fCaseSensitive = false);
    const CHAR *MainFile() {return (const CHAR *)(this->mszMainFile);};
    const CHAR *GetSection(size_t i);
    const CHAR *GetSectionAlias(const CHAR *szSection);
    void SetSectionAlias(const CHAR *szSection, const CHAR *szNewAlias);
    const CHAR *GetSectionAlias(size_t stIndex);
    void SetSectionAlias(size_t stIndex, const CHAR *szNewAlias);
    size_t SectionCount();
    const CHAR *GetSetting(const CHAR *szSection, size_t i);
    
    // INI manipulation
    unsigned int SetValue(const CHAR * szSection, const CHAR *szName, const CHAR *szValue, const CHAR *szComment = "", bool fMemOnly = false, bool fReferenceInputBuffers = false);
    //bool RenameSetting(const CHAR *szOldSection, const CHAR *szOldName, const CHAR *szNewSection, const CHAR *szNewName);
    void SetComment(const CHAR *szSection, const CHAR *szName, const CHAR *szComment, const CHAR *szMissingVal = "");
    void AppendComment(const CHAR *szSection, const CHAR *szName, const CHAR *szComment);
    void IncludeFile(const CHAR *szFileName);
    void AddPath(const CHAR *szPath);
    void SetMemOnly(const CHAR *szSection, const CHAR *szSetting, bool fSetMemOnly = true);
    
    // output INI contents to file or buffer
    bool WriteFile(const CHAR *szFileName = NULL, const CHAR *szHeader = NULL);
    bool WriteTabularFile(const CHAR *szFileName);
    const CHAR *ToSZ(const CHAR *szHeader = NULL, bool fShowComments = true);
    void SetTabular(bool fIsTabular, const CHAR *szMainColumnName = NULL);

    // convenience functions: used internally to convert values between types but may be useful to the caller as well
    bool SZToBool(const CHAR *sz);
    CHAR *IntToSZ(long long val);  // NB: these calls will use CINIFile::CloneStr for buffers
    CHAR *BoolToSZ(bool val);      //   whilst these buffers are cleaned at xtor time, it would be nice
    CHAR *DoubleToSZ(double val);  //   to clean them when they are no longer used
    // convenience functions: filesystem functions used internally, which may be useful to another caller
    CHAR *GenINIRelativePath(const CHAR *szSrcPath); // do a ReleaseClone() on this when you're done with it
#if defined (WIN32) || defined (_W32_WCE) || defined (_WIN32)
    size_t ListDirContents(CHAR ***aszOut, const CHAR *szDirname, bool fPrependDirname = false, const CHAR *szMask = "*.*");
#else
    size_t ListDirContents(CHAR ***aszOut, const CHAR *szDirName, bool fPrependDirname = false);
#endif
    // string buffer helpers
    CHAR *CloneStr(const CHAR *szToClone, size_t stPad = 0);    // clones a char* buffer; auto-freed when this instance dies
    void ReleaseClone(CHAR **szToRelease);                      // release a clone earlier than wehn this instance dies
  private:
    struct SStringBuffer {
      size_t stLen; // not the buffer len: the len of the str that can go here (ie, buffer-len - 1)
      CHAR *szBuf;
      bool fInUse;
      SStringBuffer()
      {
        this->stLen = 0;
        this->szBuf = NULL;
        this->fInUse = false;
      }
      SStringBuffer(const CHAR *szCopy, size_t stPad = 0, bool fMarkInUse = true)
      {
        this->stLen = stPad;
        if (szCopy)
          this->stLen += strlen(szCopy);
        this->szBuf = (CHAR *)malloc((1 + this->stLen) * sizeof(CHAR)); // auto-allocate char for null-terminator
        if (szCopy)
          strcpy(this->szBuf, szCopy);
        else
          this->szBuf[0] = '\0';
        this->fInUse = fMarkInUse;
      }
      ~SStringBuffer()
      {
        if (this->szBuf)
          free(this->szBuf);
      }
    };
    CHAR **maszShadow;
    size_t mstShadow;
    CHAR **maszIncludePaths;
    size_t mstIncludePaths;
    CHAR mszAppIniFilePath[MAX_PATH];
    
    size_t mstSectionLookup;
    size_t mstSectionLookupAlloc;
    SLookup **masSectionLookup;
    const CHAR *mszLastSection;
    size_t mstLastSectionIndex;
    bool mfSorted;

    bool mfTableFormat;
    CHAR mszMainFile[MAX_PATH];
    CHAR mszMainFileExt[MAX_PATH];

    CIniFileSection **masSections;
    size_t mstSections;
    size_t mstSectionsAlloc;

    // string buffers
    size_t mstStringPool;
    size_t mstStringPoolAlloc;
    SStringBuffer **masStringPool;

    CHAR *mszTrailingComment;
    CHAR *mszRenderBuffer;
    size_t mstRenderBufferAlloc;
    bool            mfFileLoaded;
    CHAR *mszTabularLeadColumn;
    
    bool LoadSZ_Internal(CHAR *szIniDataIN, bool fMerge);
    bool FileExists(const CHAR *szFileName);
    long long SectionIDX(const CHAR *szSection);
    long long ValueIDX(size_t stSectionIndex, const CHAR *szName);
    size_t split(CHAR ***aszOut, const CHAR *szIn, const CHAR *szDelimiter, const CHAR *szQuoteDelimiter = NULL);
    size_t split_buffer(CHAR ***aszOut, CHAR *szIn, const CHAR *szDelimiter, const CHAR *szQuotedDelimiter = NULL);
    CHAR *join(const CHAR **aszParts, size_t stParts, const CHAR *szDelimiter, int intStart = 0, int intEnd = -1);
    CHAR *trim(CHAR *sz, const CHAR *trimchars = " \t\n\r", bool trimleft = true, bool trimright = true, bool respectEscapeChar = true);
    CHAR *ltrim(CHAR *sz, const CHAR *szTrimChars);
    CHAR *rtrim(CHAR *sz, const CHAR *szTrimChars);
    void CleanLine(CHAR **szLine, CHAR **szComments);
    CHAR *Error(int e);
    void CheckIncludePaths(const CHAR *szFileName);
    CHAR *DirName(const CHAR *szFileName);
    const CHAR *FileExt(const CHAR *szFileName);
      const CHAR *BaseName(const CHAR *szFileName, const CHAR cDelimiter = PATH_DELIMITER_C);
    int _strcasecmp(const char *sz1, const char *sz2, bool fDefinitelyCaseInsensitive = false); // was strcasecmp but apr defines that as a macro
    void SortSettings();
    void ClearSettings();
    bool IsTabular(CHAR *szSrc, CHAR ***aszHeadings, size_t *stHeadings, CHAR **szDelimiter, CHAR **szIniDataStart);
    bool LoadTabularSZ(CHAR *szContents, CHAR **aszHeadings, size_t stHeadings, CHAR *szDelimiter);
    void AddSectionLookup(const CHAR *szSection, size_t stIdx);
    void SortSectionLookup();
    long long LookUpSectionByName(const CHAR *szSectionName);
    CHAR *SetShadow(CHAR *szSrc, size_t stLen = 0, bool fUseBuffer = false);
    CHAR *AppendShadow(CHAR *szSrc, size_t stLen = 0, bool fUseBuffer = false);
    void ClearShadow();
    size_t PushSection(CINIFile::CIniFileSection *s);
    bool str_equal(const CHAR *sz1, const CHAR *sz2);
    bool AppendString(CHAR **szBuffer, const CHAR *szAppend, size_t *stBufferLen, size_t stReallocChunkSize,
          size_t stOccurrences = 1);
    void FreeSZArray(CHAR **aszToFree, size_t stElements);
    void QuickSortLookup(SLookup **as, size_t stStart, size_t stElements, SLookup **asPre = NULL, SLookup **asPost = NULL);
    long long SearchLookupArray(SLookup **a, const size_t stLen, const CHAR *szSearch);
    void RemoveIndex(SLookup ***a, size_t *stLen, const CHAR *szRemove);
    void PushIndex(SLookup ***a, size_t *stLen, size_t *stAllocated, const CHAR *szPush, size_t stPush);

    bool ReadFileToMem(const CHAR *szFileName, void **vpFileData,
      size_t *stFileSize, bool fUseMalloc = true,
      bool fNullTerminateTextFile = false);
    bool WriteMemToFile(const CHAR *szFileName, void *vpFileData, size_t stFileSize) ;
    bool EnsureDirExists(const CHAR *szPath, bool fCheckParentOnly = false, const CHAR cPathDelimiter = PATH_DELIMITER_C);
    int str_replace(CHAR **szMain, const CHAR *szFind, const CHAR *szReplace, size_t *stBufLen = NULL, bool fRespectSlashQuoting = true);
    void strcpy_overlapped(CHAR *szDst, CHAR *szSrc);
    bool DirExists(const CHAR *szPath);
};

#endif

