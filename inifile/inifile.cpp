#include "inifile.h"
inline int ini_round (double num) {return (int)(num + 0.5);}
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


CINIFile::CINIFile(const CHAR *szFileName)
{
  #ifdef _SHOW_DEBUG_INFO_INI
  printf("Constructing IniFile object, with filename \"%s\"\n", FileName.c_str());
  #endif
  // public member varsthis->masSettingIndex, this->mstSettingIndex, const CHAR *szName);
  this->DecimalPlaces = 3;        // default to 3 decimal places for double-to-std::string conversions
  this->ExtendedSyntax = true;    // allow extended syntax (#addpath and #include)
  this->AddFilePathToIncludePath = true;  // allow includes relative to the path of the loaded file
  this->IncludesOverride = false;
  this->MergeAllPossibleIncludes = true;
  this->ValueRetentionEnabled = true;
  this->CaseSensitive = false;
  // private items
  this->mfSorted = false;
  this->mfTableFormat = false;
  this->mstSectionLookup = 0;
  this->masSectionLookup = NULL;
  this->mstSectionLookupAlloc = 0;
  this->mszLastSection = NULL;
  this->mszTrailingComment = NULL;
  this->mszRenderBuffer = NULL;
  this->mstRenderBufferAlloc = 0;
  this->masSections = NULL;
  this->mstSections = 0;
  this->mstSectionsAlloc = 0;
  this->maszShadow = NULL;
  this->mstShadow = 0;
  this->mszTabularLeadColumn = NULL;
  this->mstIncludePaths = 0;
  this->maszIncludePaths = 0;
  this->mszMainFileExt[0] = '\0';
  // string pool
  this->mstStringPool = 0;
  this->mstStringPoolAlloc = 0;
  this->masStringPool = NULL;
  
  if (szFileName)
    this->LoadFile(szFileName);
}

CINIFile::~CINIFile()
{
  this->ClearSettings();
  this->ClearShadow();
}

void CINIFile::ClearSettings()
{
  if (this->mstSections)
  {
    for (size_t i = 0; i < this->mstSections; i++)
      delete this->masSections[i];
    free(this->masSections);
    this->masSections = NULL;
    this->mstSections = 0;
  }
  if (this->mstSectionLookup)
  {
    for (size_t i = 0; i < this->mstSectionLookup; i++)
      delete this->masSectionLookup[i];
    free(this->masSectionLookup);
  }
  this->mstSectionLookupAlloc = 0;
  this->mstSectionLookup = 0;
  this->mszLastSection = NULL;
  if (this->mszTrailingComment)
    this->ReleaseClone(&(this->mszTrailingComment));

  if (this->mstStringPool)
  {
    for (size_t i = 0; i < this->mstStringPool; i++)
      delete this->masStringPool[i];
    free(this->masStringPool);
    this->mstStringPool = 0;
    this->mstStringPoolAlloc = 0;
    this->masStringPool = NULL;
  }
  if (this->mszRenderBuffer)
    free(this->mszRenderBuffer);
  if (this->maszIncludePaths)
  {
    free(this->maszIncludePaths); // elements of this array are assigned with CloneStr; cloned strings are cleared above
    this->maszIncludePaths = NULL;
    this->mstIncludePaths = 0;
  }
}

void CINIFile::AddSectionLookup(const CHAR *szSection, size_t stIdx)
{
  this->mfSorted = false;
  if ((this->mstSectionLookupAlloc - this->mstSectionLookup) < 2)
  {
    this->mstSectionLookupAlloc += 128;
    this->masSectionLookup = (struct SLookup **)realloc(this->masSectionLookup, 
      this->mstSectionLookupAlloc * sizeof(struct SLookup*));
  }
  SLookup *tmp = new SLookup(szSection, stIdx);
  this->masSectionLookup[this->mstSectionLookup++] = tmp;
  this->SortSectionLookup();
}

void CINIFile::SortSectionLookup()
{
  if (this->mfSorted)
    return;
  this->QuickSortLookup(this->masSectionLookup, 0, this->mstSectionLookup);
  this->mfSorted = true;
}

long long CINIFile::LookUpSectionByName(const CHAR *szSection)
{
  if (szSection == NULL)
    szSection = "";
  if (this->mszLastSection && (this->_strcasecmp(this->mszLastSection, szSection) == 0))
    return this->mstLastSectionIndex;
  if (!this->masSectionLookup)       // nothing to search
    return -1;
  if (this->mstSectionLookup == 0)   // trivial case
    return -1;
  // binary search: assumes that the searched array is sorted
  long llLower = 0;
  long llUpper = (long)(this->mstSectionLookup); llUpper-=1;
  long llTest;
  SLookup **a = this->masSectionLookup;
  while (llUpper >= llLower)
  {
    llTest = llLower + (size_t)((llUpper - llLower)/2);
    int cmp = this->_strcasecmp(a[llTest]->Name, szSection);
    if (cmp == 0)
    {
      // cache result
      this->mszLastSection = a[llTest]->Name;
      this->mstLastSectionIndex = a[llTest]->Index;
      // return result
      return a[llTest]->Index;
    }
    if (cmp > 0)
      llUpper = llTest - 1;
    else
      llLower = llTest + 1;
  }
  // don't bother caching the miss: most common cause of a miss is from SetValue which will
  //  cache the new setting's position
  return -1;
}

const CHAR *CINIFile::GetOverrideSZValue(const CHAR *szSections, const CHAR *szName, const CHAR *szDefaultValue, 
  const CHAR *szSectionDelimiter)
{
  CHAR **aszSections = NULL;
  size_t stSections = this->split(&aszSections, szSections, szSectionDelimiter);
  for (size_t i = 0; i < stSections; i++)
  {
    const CHAR *szSection = aszSections[i];
    if (this->ValueExists(szSection, szName))
    {
      const CHAR *ret = this->GetSZValue(szSection, szName, szDefaultValue, false);    // overrides aren't maintained for re-write
      this->FreeSZArray(aszSections, stSections);
      return ret;
    }
  }
  this->FreeSZArray(aszSections, stSections);
  return szDefaultValue;
}

bool CINIFile::GetOverrideBoolValue(const CHAR *szSections, const CHAR *szName, bool fDefaultValue,
  const CHAR *szSectionDelimiter)
{
  CHAR *szDefaultValue = this->BoolToSZ(fDefaultValue);
  const CHAR *tmp = this->GetOverrideSZValue(szSections, szName, 
    szDefaultValue, szSectionDelimiter);
  bool ret = this->SZToBool(tmp);
  this->ReleaseClone(&szDefaultValue);
  return ret;
}

double CINIFile::GetOverrideDoubleValue(const CHAR *szSections, const CHAR *szName, double dblDefaultVal,
                    const CHAR *szSectionDelimiter)
{
  CHAR *szDefault = this->DoubleToSZ(dblDefaultVal);
  const CHAR *tmp = this->GetOverrideSZValue(szSections, szName, szDefault, szSectionDelimiter);
  double ret = strtod(tmp, NULL);
  this->ReleaseClone(&szDefault);
  return ret;
}

long long CINIFile::GetOverrideLongLongValue(const CHAR *szSections, const CHAR *szName, long long llDefaultVal, 
    const CHAR *szSectionDelimiter)
{
  CHAR szBuf[64];
  sprintf(szBuf, "%lli", (long long)llDefaultVal);
  const CHAR *tmp = this->GetOverrideSZValue(szSections, szName, szBuf, szSectionDelimiter);
  #if defined(WIN32) || defined(WIN64)
  long long ret = (long long)_atoi64(tmp);
  #else
  long long ret = (long long)atoll(tmp);
  #endif
  return ret;
}

CHAR *CINIFile::GenINIRelativePath(const CHAR *szSrcPath)
{ 
  if ((szSrcPath == NULL) || (strlen(szSrcPath) > MAX_PATH))
    return this->CloneStr(szSrcPath);

  CHAR *szPath = this->CloneStr("", MAX_PATH);
  strcpy(szPath, this->mszMainFile);
  CHAR *pos = strrchr(szPath, PATH_DELIMITER_C);
  if (pos)
    *pos = '\0';
  else
    szPath[0] = '\0';   // file is specified straight-up
    
  CHAR szUp[8];
  CHAR szCwd[8];
  sprintf(szUp, "..%s", PATH_DELIMITER);
  sprintf(szCwd, ".%s", PATH_DELIMITER);
  
  while ((strstr(szSrcPath, szUp) == szSrcPath) || (strstr(szSrcPath, szCwd) == szSrcPath))
  {
    if (strstr(szSrcPath, szUp) == szSrcPath)
    {
      szSrcPath += strlen(szUp);
      pos = strrchr(szPath, PATH_DELIMITER_C);
      if (pos)
        *pos = '\0';
    }
    if (strstr(szSrcPath, szCwd) == szSrcPath)
      szSrcPath += strlen(szCwd);
  }
  if (strlen(szPath))
    strcat(szPath, PATH_DELIMITER);
  strcat(szPath, szSrcPath);
  return szPath;
}


size_t CINIFile::ListSections(const CHAR ***aszOut, const CHAR *szStartsWith, const CHAR *szContains, 
      const CHAR *szEndsWith, bool fCaseSensitive)
{
  *aszOut = (const CHAR **)malloc(this->mstSections * sizeof(const CHAR *));
  size_t ret = 0;
  CHAR *szLowerStart = NULL;
  CHAR *szLowerContains = NULL;
  CHAR *szLowerEnds = NULL;
  if (!fCaseSensitive)
  {
    if (szStartsWith)
    {
      szLowerStart = this->CloneStr(szStartsWith);
      strlwr(szLowerStart);
      szStartsWith = (const CHAR *)szLowerStart;
    }
    if (szContains)
    {
      szLowerContains = this->CloneStr(szContains);
      strlwr(szLowerContains);
      szContains = (const CHAR *)szLowerContains;
    }
    if (szEndsWith)
    {
      szLowerEnds = this->CloneStr(szEndsWith);
      strlwr(szLowerEnds);
      szEndsWith = (const CHAR *)szLowerEnds;
    }
  }
  const CHAR **a = *aszOut;
  size_t retidx = 0;
  for (size_t i = 0; i < this->mstSections; i++)
  {
    bool fSkip = false;
    const CHAR *section = this->masSections[i]->Name;
    for (size_t j = 0; j < ret; j++)  // double-check for duplicate
      if (strcmp(a[j], section) == 0)
      {
        fSkip = true;
        break;
      }
    if (!fSkip)
    {
      if (szStartsWith || szContains || szEndsWith)
      {
        CHAR *szSection = this->CloneStr(section);
        if (!fCaseSensitive)
          strlwr(szSection);
        if (szStartsWith)
        {
          if (strstr(szSection, szStartsWith) == NULL)
          {
            this->ReleaseClone(&szSection);
            continue;
          }
        }
        if (szContains)
        {
          if (strstr(szSection, szContains) == NULL)
          {
            this->ReleaseClone(&szSection);
            continue;
          }
        }
        if (szEndsWith)
        {
          if (strstr(szSection, szEndsWith) == NULL)
          {
            this->ReleaseClone(&szSection);
            continue;
          }
        }
      }
      a[retidx++] = section;
    }
  }
  this->ReleaseClone(&szLowerStart);
  this->ReleaseClone(&szLowerContains);
  this->ReleaseClone(&szLowerEnds);
  // free a little mem for the people
  *aszOut = (const CHAR **)realloc(*aszOut, ret * sizeof(CHAR *));
  return ret;
}

size_t CINIFile::ListSettings(const CHAR ***aszOut, const CHAR *szSection, const CHAR *szStartsWith, 
    const CHAR *szContains, const CHAR *szEndsWith, bool fCaseSensitive)
{
  long long llSectionIdx = this->LookUpSectionByName(szSection);
  if (llSectionIdx == -1)
  {
    *aszOut = NULL;
    return 0;
  }
  *aszOut = (const CHAR **)malloc(this->masSections[(size_t)llSectionIdx]->SettingCount * sizeof(CHAR *));
  size_t ret = 0;
  CHAR *szLowerStart = NULL;
  CHAR *szLowerContains = NULL;
  CHAR *szLowerEnds = NULL;
  if (!fCaseSensitive)
  {
    if (szStartsWith)
    {
      szLowerStart = this->CloneStr(szStartsWith);
      strlwr(szLowerStart);
      szStartsWith = (const CHAR *)szLowerStart;
    }
    if (szContains)
    {
      szLowerContains = this->CloneStr(szContains);
      strlwr(szLowerContains);
      szContains = (const CHAR *)szLowerContains;
    }
    if (szEndsWith)
    {
      szLowerEnds = this->CloneStr(szEndsWith);
      strlwr(szLowerEnds);
      szEndsWith = (const CHAR *)szLowerEnds;
    }
  }
  const CHAR **a = *aszOut;
  size_t retidx = 0;
  CIniFileSection *s = this->masSections[llSectionIdx];
  for (size_t i = 0; i < s->SettingCount; i++)
  {
    bool fSkip = false;
    const CHAR *setting = s->Settings[i]->Name;
    for (size_t j = 0; j < ret; j++)  // double-check for duplicate
      if (strcmp(a[j], setting) == 0)
      {
        fSkip = true;
        break;
      }
    if (!fSkip)
    {
      if (szStartsWith || szContains || szEndsWith)
      {
        CHAR *szSetting = this->CloneStr(setting);
        if (!fCaseSensitive)
          strlwr(szSetting);
        if (szStartsWith)
        {
          if (strstr(szSetting, szStartsWith) == NULL)
          {
            if (!fCaseSensitive) this->ReleaseClone(&szSetting);
            continue;
          }
        }
        if (szContains)
        {
          if (strstr(szSetting, szContains) == NULL)
          {
            if (!fCaseSensitive) this->ReleaseClone(&szSetting);
            continue;
          }
        }
        if (szEndsWith)
        {
          if (strstr(szSetting, szEndsWith) == NULL)
          {
            if (!fCaseSensitive) this->ReleaseClone(&szSetting);
            continue;
          }
        }
      }
      a[retidx++] = setting;
    }
  }

  this->ReleaseClone(&szLowerStart);
  this->ReleaseClone(&szLowerContains);
  this->ReleaseClone(&szLowerEnds);
  // free a little mem for the people
  *aszOut = (const CHAR **)realloc(*aszOut, ret * sizeof(CHAR *));
  return ret;
}

const CHAR *CINIFile::GetSZValue(const CHAR *szSection, const CHAR *szName, const CHAR *szDefaultValue, bool blnRetainDefault)
{
  long sidx = (long)this->SectionIDX(szSection);
  long vidx = -1;
  if (sidx > -1)
    vidx = (long)this->ValueIDX((size_t)sidx, szName);
  if ((sidx > -1) && (vidx > -1))
    return (const CHAR *)(this->masSections[sidx]->Settings[vidx]->Value);
  if (this->ValueRetentionEnabled && blnRetainDefault)
  {
    this->SetValue(szSection, szName, szDefaultValue);
    sidx = (long)this->SectionIDX(szSection);
    if (sidx > -1)
    {
      vidx = (long)this->ValueIDX((size_t)sidx, szName);
      if (vidx > -1)
        return (const CHAR *)(this->masSections[sidx]->Settings[vidx]->Value);
    }
  }
  return szDefaultValue;
}

const CHAR *CINIFile::GetNumberedSZValue(const CHAR *szSection, const CHAR *szBaseName, unsigned long ulIndex, 
  const CHAR *szDefault)
{
  CHAR *szSearch = this->CloneStr(NULL, 64);
  sprintf(szSearch, "%s%lu", szBaseName, ulIndex);
  const CHAR *ret = this->GetSZValue(szSection, szSearch, szDefault, false);
  this->ReleaseClone(&szSearch);
  return ret;
}
                
unsigned int CINIFile::GetUIntValue(const CHAR *szSection, const CHAR *szName, unsigned int uiDefaultValue, bool blnRetainDefault)
{
  unsigned int retVal = uiDefaultValue;
  
  CHAR *szDefault = this->IntToSZ((long long)uiDefaultValue);
  const CHAR *tmp = this->GetSZValue(szSection, szName, szDefault, blnRetainDefault);
  this->ReleaseClone(&szDefault);
  retVal = strtoul(tmp, NULL, 10);
  return retVal;
}

bool CINIFile::GetBoolValue(const CHAR *szSection, const CHAR *szName, bool blnDefaultValue, bool blnRetainDefault)
{
  CHAR *szDefault = this->BoolToSZ(blnDefaultValue);
  bool ret = this->SZToBool(this->GetSZValue(szSection, szName, szDefault, blnRetainDefault));
  this->ReleaseClone(&szDefault);
  return ret;
}

double CINIFile::GetDoubleValue(const CHAR *szSection, const CHAR *szName, double dblDefaultValue, bool blnRetainDefault)
{
  CHAR *szDefault = this->DoubleToSZ(dblDefaultValue);
  double ret = atof(this->GetSZValue(szSection, szName, szDefault, blnRetainDefault));
  this->ReleaseClone(&szDefault);
  return ret;
}

long long CINIFile::GetLongLongValue(const CHAR *szSection, const CHAR *szName, long long llDefaultValue, bool blnRetainDefault)
{
  CHAR *szBuf = this->CloneStr(NULL, 64);
  sprintf(szBuf, "%lli", (long long)llDefaultValue);
  const CHAR *szVal = this->GetSZValue(szSection, szName, szBuf, blnRetainDefault);
  #if defined(WIN32) || defined(WIN64)
  long long ret = (long long)_atoi64(szVal);
  #else
  long long ret = (long long)atoll(szVal);
  #endif
  this->ReleaseClone(&szBuf);
  return ret;
}

CHAR *CINIFile::DoubleToSZ(double val)
{
  long long predec = (long long)val;
  CHAR *szPreDec = this->IntToSZ(predec);
  CHAR *ret = this->CloneStr(szPreDec, this->DecimalPlaces + 4);
  this->ReleaseClone(&szPreDec);
  unsigned int mul = 1;
  for (unsigned int i = 0; i < this->DecimalPlaces; i++)
    mul *= 10;
  double dec = (val - (double)predec) * mul;
  
  if (dec)
  {
    strcat(ret, ".");
    CHAR *szTmp = this->IntToSZ((unsigned int)ini_round(dec));
    strcat(ret, szTmp);
    this->ReleaseClone(&szTmp);

    // i actually wanted to trunc here, but truncing a double 111 to a long gives me 110; which sucks
    while (ret[strlen(ret)-1] == '0')
      ret[strlen(ret)-1] = '\0';
    if (ret[strlen(ret)-1] == '.')
      ret[strlen(ret)-1] = '\0';
  }
  return ret;
}

bool CINIFile::Loaded()
{
  return this->mfFileLoaded;
}

bool CINIFile::LoadBuffer(const CHAR *szSrc, size_t stSrcLen, bool fMerge)
{
  this->SetShadow((CHAR *)szSrc, stSrcLen, false);
  return this->LoadSZ_Internal(this->maszShadow[0], fMerge);
}

bool CINIFile::IsTabular(CHAR *szSrc, CHAR ***aszHeadings, size_t *stHeadings, CHAR **szDelimiter, CHAR **szIniDataStart)
{
  // look for column names in first line
  //  this means not just a lone [name] or a single = for a global section conf item
  CHAR *pos1 = szSrc;
  CHAR *pos2 = NULL;
  CHAR *szLine = NULL;
  CHAR *szTrimmedLine = NULL;
  do
  {  // "slice" first non-comment, non-empty line out of the src into szLine
    this->ReleaseClone(&szTrimmedLine);
    if (pos2)
      *pos2 = '\n';
    pos2 = strstr(pos1, "\n");
    if (pos2 == NULL)
      pos2 = pos1 + strlen(pos1);
    szLine = pos1;
    *pos2 = '\0';
    szTrimmedLine = this->CloneStr(szLine);
    this->trim(szTrimmedLine);
    pos1 = pos2 + 1;
  } while (((strlen(szTrimmedLine) == 0) || (szTrimmedLine[0] == ';'))); // ignore comment lines as well
  CHAR *szCommentStart = strstr(szTrimmedLine, ";");
  if (szCommentStart)
  {
    *szCommentStart = '\0';
    this->trim(szTrimmedLine);
  }
  size_t stLineLen = strlen(szTrimmedLine);
  if (stLineLen == 0)
  {
    this->ReleaseClone(&szTrimmedLine);
    return false;
  }
  if ((szTrimmedLine[0] == '[') && (szTrimmedLine[stLineLen-1] == ']'))  // section header
  {
    this->ReleaseClone(&szTrimmedLine);
    if (pos2)
      *pos2 = '\n';
    return false;
  }
  int intECount = 0;
  pos1 = szTrimmedLine;
  while (strstr(pos1, "="))
  {
    pos1++;
    intECount++;
  }
  if (intECount == 1)   // single global conf line
  {
    if (pos2)
      *pos2 = '\n';
    return false;
  }
    
  // look for CSV first since csv values might have tabs in them
  *szIniDataStart = pos2 + 1;
  if (strstr(szLine, ","))
  {
    *szDelimiter = this->CloneStr(",");
    *stHeadings = this->split_buffer(aszHeadings, szLine, *szDelimiter);
    this->SetTabular(true, (*aszHeadings)[0]);
    return true;
  }
  // look for tab delimited
  *szDelimiter = this->CloneStr("\t");
  *stHeadings = this->split_buffer(aszHeadings, szLine, *szDelimiter);
  this->SetTabular(true, (*aszHeadings)[0]);
  return true;
}

bool CINIFile::LoadFile(const CHAR *szFileName, bool fMerge)
{
  if (!fMerge)
    this->ClearSettings();
  if (strlen(szFileName) >= MAX_PATH)
  {
    this->mfFileLoaded = false;
    return false;
  }
  if (!this->FileExists(szFileName))
  {
    #ifdef _SHOW_DEBUG_INFO_INI
    fprintf(stderr, "ERROR: Unable to open \"%s\": file cannot be found.\n", szFileName);
    #endif
    this->mfFileLoaded = false;
    return false;
  }
  
  CHAR *szToLoad = NULL;
  if (fMerge)
  { // append data onto szshadow
    size_t stRead = 0;
    if (!this->ReadFileToMem(szFileName, (void **)(&(szToLoad)), &stRead, true, true))
      return false;
    szToLoad = this->AppendShadow(szToLoad, stRead);
  }
  else
  {
    strcpy(this->mszMainFile, szFileName);
    const CHAR *szExt = this->FileExt(this->mszMainFile);
    if (strlen(szExt) == 0)
    {
      #ifdef _SHOW_DEBUG_INFO_INI
      printf("WARNING: Main file extension unknown -- defaulting to ini\n");
      #endif
      strcpy(this->mszMainFileExt, "ini"); // default extension for #addpath and the like
    }
    // read file into internal shadow buffer
    this->ClearShadow();
    size_t stFileSize = 0;
    if (!this->ReadFileToMem(this->mszMainFile, (void **)(&szToLoad), &stFileSize, true, true))
      return false;
    this->SetShadow(szToLoad, 0, true);
  }

  // invoke LoadSZ_Internal on the shadow buffer
  if (szToLoad)
    return this->LoadSZ_Internal(szToLoad, fMerge);
  else
    return false;
}

bool CINIFile::LoadSZ(const CHAR *szIniData, bool fMerge)
{
  if (!fMerge)
    this->ClearSettings();
  strcpy(this->mszMainFile, MEMORY_FILENAME);
  this->mszMainFileExt[0] = '\0';
  CHAR *szMyIniData = NULL;
  if (fMerge)
    szMyIniData = this->AppendShadow((CHAR *)szIniData);
  else
    szMyIniData = this->SetShadow((CHAR *)szIniData);
  return this->LoadSZ_Internal(szMyIniData, fMerge);
}

CHAR *CINIFile::SetShadow(CHAR *szSrc, size_t stLen, bool fUseBuffer)
{
  if (stLen == 0)
    stLen = strlen(szSrc);
  this->ClearShadow();
  this->mstShadow = 1;
  this->maszShadow = (CHAR **)realloc(this->maszShadow, this->mstShadow * sizeof(CHAR *));
  if (!fUseBuffer)
  {
    this->maszShadow[0] = (CHAR *)malloc((1 + stLen) * sizeof(CHAR));
    strncpy(this->maszShadow[this->mstShadow-1], szSrc, stLen);
  }
  else
    this->maszShadow[0] = szSrc;
  this->maszShadow[0][stLen] = '\0';
  return this->maszShadow[0];
}

void CINIFile::ClearShadow()
{
  if (this->mstShadow)
  {
    for (size_t i = 0; i < this->mstShadow; i++)
      free(this->maszShadow[i]);
    free(this->maszShadow);
    this->mstShadow = 0;
    this->maszShadow = NULL;
  }
}

CHAR *CINIFile::AppendShadow(CHAR *szSrc, size_t stLen, bool fUseBuffer)
{
  if (stLen == 0)
    stLen = strlen(szSrc);
  this->mstShadow++;
  this->maszShadow = (CHAR **)realloc(this->maszShadow, this->mstShadow * sizeof(CHAR *));
  if (fUseBuffer)
  {
    this->maszShadow[this->mstShadow-1] = szSrc;
    this->maszShadow[stLen] = '\0';  // null-terminate because otherwise the clowns will eat me.
    return szSrc;
  }
  else
  {
    CHAR *tmp = (CHAR *)malloc((1 + stLen) * sizeof(CHAR));
    strncpy(tmp, szSrc, stLen);
    tmp[stLen] = '\0';  // null-terminate because of strncpy
    this->maszShadow[this->mstShadow-1] = tmp;
    return tmp;
  }
}

bool CINIFile::LoadSZ_Internal(CHAR *szIniDataIN, bool fMerge)
{
  if ((szIniDataIN == NULL) || (strlen(szIniDataIN) == 0))
    return true;  // nothing to do
  CHAR *szIniData = szIniDataIN;
  if (this->ExtendedSyntax && (strcmp(this->mszMainFileExt, MEMORY_FILENAME)))
    this->CheckIncludePaths(szIniData);
  #ifdef _SHOW_DEBUG_INFO_INI
  printf("CINIFile::loadFile: loading from file '%s'\n", strFileName.c_str());
  #endif
  CHAR **aszLines = NULL;
  size_t stLines;
  CHAR **aszHeadings = NULL;
  size_t stHeadings = 0;
  CHAR *szDelimiter = NULL;

  CHAR *szIniDataStart = NULL;
  if (this->IsTabular(szIniData, &aszHeadings, &stHeadings, &szDelimiter, &szIniDataStart))
  {
    bool ret = this->LoadTabularSZ(szIniDataStart, aszHeadings, stHeadings, szDelimiter);
    this->ReleaseClone(&szDelimiter);
    return ret;
  }
  if (aszHeadings)
    free(aszHeadings);  // if it had contents (which it shouldn't), they will point into the shadow
  aszHeadings = NULL;
  stHeadings = 0;

  CHAR *szEndOfShadow = szIniData + strlen(szIniData);  // used as a pointer for empty string (:
  const CHAR *szLineDelimiter = "\n"; // deal with \n and \r\n 
  stLines = this->split_buffer(&aszLines, szIniData, szLineDelimiter, "\"");
    
  CHAR *szCurrentSection = NULL;
  CHAR *szName = NULL, *szValue = NULL;
  CHAR *szLine = NULL;
  CHAR **aszSplitLine = NULL;
  size_t stSplitLine = 0;
  unsigned int idx;
  
  unsigned long linenum = 0;
  CHAR *szComment = NULL;
  bool fInINI = (stLines > 0);
  while (fInINI)
  {
    szLine = aszLines[linenum++];
    fInINI = (linenum < stLines);

    this->CleanLine(&szLine, &szComment);
    #ifdef INI_DEBUG
    printf("read line: '%s'\n", szLine);
    #endif
    if (strlen(szLine))
    {
      if (szLine[0] == '[')
      {
        szCurrentSection = szLine;
        this->trim(szCurrentSection, "[]");
        #ifdef INI_DEBUG
        printf("IniFile: reading from section: %s\n", szCurrentSection);
        #endif
        continue;
      }
      else if ((szLine[0] == '#') && (this->ExtendedSyntax))
      {
        if (strcmp(this->mszMainFile, MEMORY_FILENAME) == 0)
        {
          #ifdef INI_DEBUG
          fprintf(stderr, "Unable to perform #include or #addpath: main file is memory-based\n");
          #endif
        }
        else if (this->ExtendedSyntax)
        {
          if (strstr(szLine, "#include") == szLine)
          {
            CHAR **aszParts = NULL;
            size_t stParts = this->split_buffer(&aszParts, szLine, " ", "\"");
            if (stParts > 1)
              this->IncludeFile(this->trim(aszParts[1]));
            #ifdef INI_DEBUG
            else
              fprintf(stderr, "ERROR: Incomplete #include at line %li of (perhaps) file %s\n", linenum, this->mszMainFile);
            #endif
            if (aszParts)
              free(aszParts); // contents of aszParts are still in shadow buffer
          }
          else if (strstr(szLine, "#addpath"))
          {
            CHAR **aszParts = NULL;
            size_t stParts = this->split(&aszParts, szLine, " ", "\"");
            if (stParts > 1)
              this->AddPath(this->trim(aszParts[1]));
            #ifdef INI_DEBUG
            else
              fprintf(stderr, "ERROR: Incomplete #addpath at line %li of (perhaps) file %s\n", linenum, this->mszMainFile);
            #endif
            if (aszParts)
              free(aszParts);
          }
        }
      }
      else
      {
        /*
        stSplitLine = this->split_buffer(&aszSplitLine, szLine, "=");
        szName = this->trim(aszSplitLine[0], "\t ");
        if (stSplitLine == 2)
          szValue = this->trim(aszSplitLine[1], "\t\r\n ");
        else if (stSplitLine > 2)
        {
          // rejoin the leftover pieces of this line with = & then trim
          for (size_t i = 2; i < stSplitLine; i++)
          {
            CHAR *szPart = aszSplitLine[i];
            szPart--;
            *szPart = '=';
          }
          szValue = this->trim(aszSplitLine[1], "\t\r\n ");
        }
        else
          szValue = szEndOfShadow;
        free(aszSplitLine);
        */
        szName = szLine;
        CHAR *szEq = strchr(szLine, '=');
        if (szEq)
        {
          *szEq = '\0';
          szEq++;
          szValue = szEq;
        }
        else
          szValue = NULL;
          
        if (szValue && (strlen(szValue) > 1) && (szValue[0] == '"') && (szValue[strlen(szValue)-1] == '"'))
        {
          this->trim(szValue, "\"");
#define HANDLE_ERR(code) \
          do { \
            int ret = code; \
            if (ret < 0) \
              return false; \
          } while (0);
          HANDLE_ERR(this->str_replace(&szValue, "\\\"", "\""));
          HANDLE_ERR(this->str_replace(&szValue, "\\\\", "\\"));
          HANDLE_ERR(this->str_replace(&szValue, "\\n", "\n"));
          HANDLE_ERR(this->str_replace(&szValue, "\\r", "\r"));
          HANDLE_ERR(this->str_replace(&szValue, "\\t", "\t"));
#undef HANDLE_ERR
        }
        
        if (this->IncludesOverride || !this->ValueExists(szCurrentSection, szName))
        {
          idx = this->SetValue(szCurrentSection, szName, szValue, szComment, false, true);
          #ifdef _SHOW_DEBUG_INFO_INI
          printf("IniFile: set parameter %u: '%s', value '%s', to section '%s'\n",
              idx,
              this->vSettings[idx]->Name.c_str(),
              this->vSettings[idx]->Value.c_str(),
              this->vSettings[idx]->Section.c_str());
          #endif
        }
        #ifdef _SHOW_DEBUG_INFO_INI
        else
        {
          printf("IniFile: Not overiding existing parameter '%s' of section '%s'\n", 
            szName, szCurrentSection);
        }
        #endif
      }
    }
    else if (szComment && strlen(szComment))
    {
      CINIFile::CIniFileItem *ii = new CIniFileItem();
      ii->IsLineComment = true;
      ii->Comment = szComment;
      
      long long sidx = this->SectionIDX(szCurrentSection);
      CIniFileSection *section;
      if (sidx < 0)
      {
        section = new CIniFileSection();
        section->owner = this;
        if (szCurrentSection)
          section->Name = szCurrentSection;
        else
        {
          CLONESTR(section->Name, "");
          section->ChangedName = true;
        }
        this->PushSection(section);
        if (szCurrentSection) // allow for the "global" section (which has no name)
          this->AddSectionLookup(szCurrentSection, this->mstSections - 1);
        else
          this->AddSectionLookup("", this->mstSections - 1);
      }
      else
        section = this->masSections[(size_t)sidx];
      section->PushSetting(ii);
    }
  }
  if (aszHeadings)
    this->FreeSZArray(aszHeadings, stHeadings);
  free(aszLines); // remember the actual buffer used for aszLines' strings is mszShadow!
  if (szComment && strlen(szComment))
  {
    if (fMerge)
    {
      CHAR *tmp = this->CloneStr(this->mszTrailingComment, strlen(szComment) + strlen(NEWLINE));
      strcat(tmp, NEWLINE);
      strcat(tmp, szComment);
      this->ReleaseClone(&(this->mszTrailingComment));
      this->mszTrailingComment = tmp;
    }
    else
    {
      if (this->mszTrailingComment)
        this->ReleaseClone(&(this->mszTrailingComment));
      this->mszTrailingComment = this->CloneStr(szComment);
    }
  }
  return (this->mfFileLoaded = true);
}

bool CINIFile::LoadTabularSZ(CHAR *szContents, CHAR **aszHeaders, size_t stHeaders, CHAR *szDelimiter)
{
  CHAR **aszLines = NULL;
  size_t stLines = this->split_buffer(&aszLines, szContents, "\n");
  CHAR **aszParts = NULL;
  size_t stParts = 0;
  bool fCheckQuoted = false;
  if (strcmp(szDelimiter, ",") == 0)
    fCheckQuoted = true;  // CSV files can be value,value,value, or "value","value","value"
  CHAR *szLine, *szPart;
  for (size_t i = 0; i < stLines; i++) // first line should be headers
  {
    szLine = aszLines[i];
    this->trim(szLine);
    stParts = this->split_buffer(&aszParts, szLine, szDelimiter);
    szPart = this->trim(aszParts[0]);
    if (this->SectionExists(szPart) || (strlen(szPart) == 0))
    {
      free(aszParts);
      continue;   // first section wins out for tabular data
    }
    CIniFileSection *is = new CIniFileSection();
    is->owner = this;
    is->Name = szPart;
    for (size_t j = 1; j < stHeaders; j++)   // first header becomes section name
    {
      CIniFileItem *ii = new CIniFileItem();
      ii->Name = aszHeaders[j];
      if (j < stParts)
      {
        ii->Value = this->trim(aszParts[j], " \t\r\n\"");
      }
      is->PushSetting(ii);
    }
    
    this->PushSection(is);
    this->AddSectionLookup(is->Name, this->mstSections - 1);
    free(aszParts);
  }
  free(aszLines);
  free(aszHeaders);
  this->mfTableFormat = true;
  return true;
} 

// TODO: determine if this method is worth fixing / porting
/*
bool CINIFile::RenameSetting(const CHAR *szOldSection, const CHAR *szOldName, const CHAR *szNewSection, const CHAR *szNewName)
{
  long sidx = (long)this->SectionIDX(szOldSection);
  if (sidx == -1)
    return false;
  long vidx = (long)this->ValueIDX((size_t)sidx, szOldName);
  if (sidx > -1)
  {
    if (this->_strcasecmp(szNewSection, this->masSections[sidx]->Name))
    { // section and name have changed
      this->vSections[sidx]->Settings.erase(this->vSections[sidx]->Settings.begin() + vidx);

      this->setValue(szNewSection, szNewName, this->vSections[sidx]->Settings[vidx]->Value.c_str(),
        this->vSections[sidx]->Settings[vidx]->Comment.c_str(), this->vSections[sidx]->Settings[vidx]->MemOnly);
    }
    else
      this->vSections[sidx]->Settings[vidx]->Name = szNewName;
    return true;
  }
  return false;
}
*/
CHAR *CINIFile::DirName(const CHAR *szFileName)
{
  // ReleaseClone on returned value
  if (szFileName == NULL)
    return NULL;
  CHAR *ret = this->CloneStr(szFileName);
  CHAR *pos = strrchr(ret, PATH_DELIMITER_C);
  if (pos)
    *pos = '\0';
  return ret;
}

void CINIFile::CheckIncludePaths(const CHAR *szFileName)
{
  // adds the base dir of the file to the include dirs
  CHAR *szDirName = this->DirName(szFileName);
  for (unsigned int i = 0; i < this->mstIncludePaths; i++)
  {
    if (strcmp(szDirName, this->maszIncludePaths[i]) == 0)
      return; // already have this include path on the stack
  }
  // add this include path
  this->mstIncludePaths++;
  this->maszIncludePaths = (CHAR **)realloc(this->maszIncludePaths, this->mstIncludePaths * sizeof(CHAR*));
  this->maszIncludePaths[this->mstIncludePaths-1] = this->CloneStr(szDirName);
}

void CINIFile::IncludeFile(const CHAR *szFileName)
{
  if (this->mszMainFile == NULL)
    return; // paranoia to prevent buffer oddness
  if (strcmp(this->mszMainFile, MEMORY_FILENAME) == 0)
    return; // can't do an include when in mem mode
  #ifdef _SHOW_DEBUG_INFO_INI
  printf("Including file %s\n", szFileName);
  #endif
  struct stat st;
  // make sure that relative paths are found wrt the main file
  char *szCwd;
  szCwd = getcwd(NULL, 512);
  CHAR *szMainDir = this->DirName(this->mszMainFile);
  if (chdir(szMainDir) != 0)
  {
    fprintf(stderr, "Can't chdir() to '%s'; won't include files from there\n",
      szMainDir);
    this->ReleaseClone(&szMainDir);
    return;
  }
  this->ReleaseClone(&szMainDir);
  CHAR **aszIncludeFiles = (CHAR **)malloc((1 + this->mstIncludePaths) * sizeof(CHAR *));
  size_t stIncludeFiles = 0;
  if (stat(szFileName, &st) == 0)
    aszIncludeFiles[stIncludeFiles++] = this->CloneStr(szFileName);
  size_t stFileNameLen = strlen(szFileName);
  if (!this->MergeAllPossibleIncludes || (stIncludeFiles == 0))
  {
    // search for the file in include paths
    for (unsigned int i = 0; i < this->mstIncludePaths; i++)
    {
      CHAR *tmp = this->CloneStr(this->maszIncludePaths[i], strlen(PATH_DELIMITER) + stFileNameLen);
      strcat(tmp, PATH_DELIMITER);
      strcat(tmp, szFileName);
      if (stat(tmp, &st) == 0)
      {
        aszIncludeFiles[stIncludeFiles++] = tmp;
        if (!this->MergeAllPossibleIncludes)
          break;
      }
      else
        this->ReleaseClone(&tmp);
    }
  }
  
  #ifdef _SHOW_DEBUG_INFO_INI
  if (vsIncludeFiles.size() == 0)
  {
    printf("ERROR: Unable to find file for inclusion: '%s'\n", strFileName.c_str());
  }
  #endif
  for (size_t i = 0; i < stIncludeFiles; i++)
  {
    if (stat(aszIncludeFiles[i], &st) == 0)
      this->LoadFile(aszIncludeFiles[i], true);
    this->ReleaseClone(&(aszIncludeFiles[i]));
  }
  if (chdir(szCwd))
  {
    fprintf(stderr, "Unable to chdir() back to '%s'\n", szCwd);
  }
  free(szCwd);
  free(aszIncludeFiles);
}

void CINIFile::AddPath(const CHAR *szPath)
{
  struct stat st;
  // make sure that relative paths are found wrt the main file
  #ifdef _SHOW_DEBUG_INFO_INI
  printf("Adding include path %s\n", szPath);
  #endif
  char *szCwd;
  szCwd = getcwd(NULL, 512);
  CHAR *szMainDir = this->DirName(this->mszMainFile);
  if (chdir(szMainDir) != 0)
  {
    fprintf(stderr, "Unable to chdir() to '%s'; can't add path\n",szMainDir);
    this->ReleaseClone(&szMainDir);
    return;
  }
  this->ReleaseClone(&szMainDir);
  CHAR **aszAddPaths = (CHAR **)malloc((1 + this->mstIncludePaths) * sizeof(CHAR *)) ;
  size_t stAddPaths = 0;

  if ((this->MergeAllPossibleIncludes || (stAddPaths == 0)) &&
      (stat(szPath, &st) == 0))
    aszAddPaths[stAddPaths++] = this->CloneStr(szPath);
  size_t stPathLen = strlen(szPath);
  if (this->MergeAllPossibleIncludes || (stAddPaths == 0))
  {
    // allow addpaths from the search path directives
    for (unsigned int i = 0; i < this->mstIncludePaths; i++)
    {
      CHAR *tmp = this->CloneStr(this->maszIncludePaths[i], strlen(PATH_DELIMITER) + stPathLen);
      strcat(tmp, PATH_DELIMITER);
      strcat(tmp, szPath);
      if (stat(tmp, &st) == 0)
      {
        if (this->MergeAllPossibleIncludes || (stAddPaths == 0))
        {
          aszAddPaths[stAddPaths++] = tmp;
          if (!this->MergeAllPossibleIncludes)
            break;
        }
        else
          this->ReleaseClone(&tmp);
      }
    }
  }
  #ifdef _SHOW_DEBUG_INFO_INI
  if (stAddPaths == 0)
  {
    printf("Unable to find directory '%s' for #addpath\n", strPath.c_str());
    free(aszAddPaths);
    return;
  }
  #endif
  
  for (unsigned int i = 0; i < stAddPaths; i++)
  {
    if (stat(aszAddPaths[i], &st) == 0)
    {
      // cause includes from files of the same extension in matched paths
      CHAR **aszPathFiles = NULL;
  #if defined (WIN32) || defined (_W32_WCE) || defined (_WIN32)
      CHAR *szMask = this->CloneStr("*.", strlen(this->mszMainFileExt));
      strcat(szMask, this->mszMainFileExt);
      size_t stPathFiles = this->ListDirContents(&aszPathFiles, aszAddPaths[i], true, szMask);
  #else
      size_t stPathFiles = this->ListDirContents(&aszPathFiles, aszAddPaths[i], true);
  #endif
      #ifdef INI_DEBUG
      #if defined (WIN32) || defined (_W32_WCE) || defined (_WIN32)
      printf("added path %s contains %u possible include file(s)\n", szPath, stPathFiles);
      #else
      printf("added path %s contains %u file(s) to check for includes\n", szPath, stPathFiles);
      #endif
      #endif
      for (unsigned int i = 0; i < stPathFiles; i++)
      {
        CHAR *tmp = aszPathFiles[i];
        #ifdef _SHOW_DEBUG_INFO_INI
        printf("Checking file extension of %s against that of %s\n", tmp, this->mszMainFileExt);
        #endif
  #if !defined (WIN32)
        if (this->_strcasecmp(this->FileExt(tmp), this->mszMainFileExt) == 0)
        {
  #endif
          #ifdef _SHOW_DEBUG_INFO_INI
          printf("Calling IncludeFile on %s\n", tmp);
          #endif
          this->IncludeFile(tmp);
  #if !defined (WIN32)
        }
  #endif
      }
    }
  }
  if (chdir(szCwd))
  {
    fprintf(stderr, "Unable to chdir() back to '%s'\n", szCwd);
  }
  free(szCwd);
}

bool CINIFile::FileExists(const CHAR *szFileName)
{
  struct stat st;
  
  if (stat(szFileName, &st) == 0)
    return true;
  return false;
}

bool CINIFile::ValueExists(const CHAR *szSection, const CHAR *szName)
{
  long long tmp;
  tmp = this->SectionIDX(szSection);
  if (tmp == -1)
    return false;
  
  if (this->ValueIDX((size_t)tmp, szName) == -1)
    return false;
  return true;
}

bool CINIFile::SZToBool(const CHAR *sz)
{
  bool retVal;
  
  if ((strlen(sz) == 0) || (this->_strcasecmp(sz, "0") == 0) || 
    (this->_strcasecmp(sz, "no") == 0) || 
    (this->_strcasecmp(sz, "false") == 0) ||
    (this->_strcasecmp(sz, "off") == 0) ||
    (this->_strcasecmp(sz, "disabled") == 0))
  {
    #ifdef _SHOW_DEBUG_INFO_INI
    printf("strToBool returning false for std::string \"%s\"\n", str.c_str());
    #endif
    retVal = false;
  }
  else if ((this->_strcasecmp(sz, "true") == 0) ||
        (this->_strcasecmp(sz, "yes") == 0) ||
        (this->_strcasecmp(sz, "on") == 0) ||
        (this->_strcasecmp(sz, "enabled") == 0) ||
        (strtoul(sz, NULL, 10) != 0))
  {
    #ifdef _SHOW_DEBUG_INFO_INI
    printf("  strToBool returning true for std::string '%s'\n", str.c_str());
    #endif
    retVal = true;
  }
  else
  {
    printf("CINIFile::strToBool: WARNING: unrecognised boolean value: \"%s\"; assuming false", sz);
    retVal = false;
  }
  
  return retVal;
}

long long CINIFile::SectionIDX(const CHAR *szSection)
{
  return this->LookUpSectionByName(szSection);
}

long long CINIFile::ValueIDX(size_t stSectionIndex, const CHAR *szName)
{
  if (szName == NULL)
    return -1;
  if (stSectionIndex >= this->mstSections)
    return -1;
    
  CIniFileSection *s = this->masSections[stSectionIndex];
  return s->SettingIndex(szName);
  /*
  for (size_t i = 0; i < s->SettingCount; i++)
  {
    if (this->_strcasecmp(s->Settings[i]->Name, szName) == 0)
    {
      return (long long)i;
    }
  }
  return -1;
  */
}

CHAR *CINIFile::IntToSZ(long long val)
{
  CHAR *ret = this->CloneStr(NULL, 64);
  sprintf(ret, "%lli", (long long)val);
  return ret;
}

CHAR *CINIFile::BoolToSZ(bool val)
{
  // use cloned strings just for consistency with other *ToSZ methods
  if (val)
    return this->CloneStr("yes");
  else
    return this->CloneStr("no");
}

void CINIFile::FreeSZArray(CHAR **aszToFree, size_t stElements)
{
  if (stElements)
  {
    for (size_t i = 0; i < stElements; i++)
      free(aszToFree[i]);
    free(aszToFree);
  }
}

size_t CINIFile::split(CHAR ***aszOut, const CHAR *szStr, const CHAR *szDelimiter, const CHAR *szQuoteDelimiter)
{
  /*
  * splits a c string based on another c string (the delimiter) into a CHAR ** array
  * inputs:  szStr: the string to split out
  *          szDelimiter: the c string to delimit szStr on
  * outputs: aszOut: pointer to array that the caller can access for all parts returned
  *           initially, this is malloc'ed, and realloc'ed as required; the caller is
  *           responsible for freeing the array, and may use FreeSZArray() to do so
  * return value: number of elements in aszOut
  * NB: call FreeSZArray on aszOut when you're done
  */
  size_t stRet = 0;
  size_t stAllocated;
  CHAR *szStrCopy = CloneStr(szStr);
  CHAR *pos = strstr(szStrCopy, szDelimiter);
  CHAR *qpos = NULL;
  CHAR *trimmed, *postpos;
  CHAR *szSafeCopyBuffer = (CHAR *)malloc((1 + strlen(szStr)) * sizeof(CHAR));
  #define TRIM_CHARS(str, delimiter) \
  do { \
    if (delimiter == NULL) \
    { \
      trimmed = str; \
      break; \
    } \
    trimmed = str; \
    if (strstr(trimmed, delimiter) == trimmed) \
      trimmed += strlen(delimiter); \
    postpos = str  + strlen(str) - strlen(delimiter); \
    if (strcmp(postpos, delimiter) == 0) \
    { \
      if (postpos != trimmed) \
      { \
        CHAR *preq = postpos-1; \
        if ((*preq == '\\') && (postpos != pos)) \
        { \
          postpos--; \
          if (*postpos != '\\') \
            *postpos = '\0'; \
        } \
        else \
          *postpos = '\0'; \
      } \
      else \
        *postpos = '\0'; \
    } \
  } while (0)

  if (pos)
  {
    stAllocated = 10;
    *aszOut = (CHAR **)malloc(stAllocated * sizeof(CHAR *));
    pos = strstr(szStrCopy, szDelimiter);

    while (pos)
    {
      if (szQuoteDelimiter)
        qpos = strstr(szStrCopy, szQuoteDelimiter);
      if (qpos == szStrCopy)
      { // look for next matching quote -- very simple search that finds the next quote or end of string, irrespective of
        //  delimiters
        qpos++;
        qpos = strstr(qpos, szQuoteDelimiter);
        if (qpos == NULL) // no matching end delimiter; pretend end of string is delimited
          break; // let the end piece pick this up as a full fragment
        if ((qpos + 1) != pos)
        { // quote delimiter is not just before pos -- qpos is the new pos
          pos = qpos;
        }
      }
      if (*pos == '\0')
        break;
      CHAR *szPart = (CHAR *)malloc((1 + (strlen(szStrCopy) - strlen(pos))) * sizeof(CHAR));
      *pos = '\0';
      TRIM_CHARS(szStrCopy, szQuoteDelimiter);
      strcpy(szPart, trimmed);
      (*aszOut)[stRet++] = szPart;
      pos += strlen(szDelimiter);
      this->strcpy_overlapped(szStrCopy, pos);
      pos = strstr(szStrCopy, szDelimiter);
      if (stRet >= stAllocated)
      {
        stAllocated += 10;
        *aszOut = (CHAR **)realloc(*aszOut, stAllocated * sizeof(CHAR *));
      }
    }
    // pick up the leftover part into the last element of the array, if there is anything
    if (szStrCopy && (*szStrCopy != '\0'))
    {
      *aszOut = (CHAR **)realloc(*aszOut, (++stRet) * sizeof(CHAR *));    // dealloc empty array elements
      (*aszOut)[stRet - 1] = (CHAR *)malloc((1 + strlen(szStrCopy)) * sizeof(CHAR));
      pos = szStrCopy;
      // trim quote delimiter, if required
      TRIM_CHARS(szStrCopy, szQuoteDelimiter);
      strcpy((*aszOut)[stRet - 1], trimmed);
    }
    else
      *aszOut = (CHAR **)realloc(*aszOut, stRet * sizeof(CHAR *));
  }
  else
  { 
    *aszOut = (CHAR **)malloc(sizeof(CHAR *));
    (*aszOut)[0] = CloneStr(szStr); 
    stRet = 1;
  }
  free(szStrCopy);
  free(szSafeCopyBuffer);
  return stRet;
  #undef TRIM_CHARS
}

void CINIFile::strcpy_overlapped(CHAR *szDst, CHAR *szSrc)
{
  // this is only here to make paranoid memory watchers happy when
  //  basically shifting part of a string up towards the start
  #ifndef QUICK_UNSAFE_STRINGS
  CHAR *szTmp = this->CloneStr(szSrc);
  strcpy(szDst, szTmp);
  this->ReleaseClone(&szTmp);
  #else
  strcpy(sz, pos);
  #endif
}

size_t CINIFile::split_buffer(CHAR ***aszOut, CHAR *szStr, const CHAR *szDelimiter, const CHAR *szQuoteDelimiter)
{
  /* like split, but mangles the input buffer -- useful for being faster and lower mem usage, if you have
   * control over szStr
   * NB: DO NOT CALL FreeSZArray when you are done. DO, however, free(*aszOut)
   */
  size_t stRet = 0;
  size_t stAllocated;
  CHAR *pos = strstr(szStr, szDelimiter);
  CHAR *qpos = NULL;
  CHAR *trimmed, *postpos;
  CHAR *szSafeCopyBuffer = (CHAR *)malloc((1 + strlen(szStr)) * sizeof(CHAR));
  #define TRIM_CHARS(str, delimiter) \
  do { \
    if (delimiter == NULL) \
    { \
      trimmed = str; \
      break; \
    } \
    trimmed = str; \
    if (strstr(trimmed, delimiter) == trimmed) \
      trimmed += strlen(delimiter); \
    postpos = str  + strlen(str) - strlen(delimiter); \
    if (strcmp(postpos, delimiter) == 0) \
    { \
      if (postpos != trimmed) \
      { \
        CHAR *preq = postpos-1; \
        if ((*preq == '\\') && (postpos != pos)) \
        { \
          postpos--; \
          if (*postpos != '\\') \
            *postpos = '\0'; \
        } \
        else \
          *postpos = '\0'; \
      } \
      else \
        *postpos = '\0'; \
    } \
  } while (0)

  if (pos)
  {
    stAllocated = 10;
    *aszOut = (CHAR **)malloc(stAllocated * sizeof(CHAR *));
    pos = strstr(szStr, szDelimiter);
    CHAR *lastpos = szStr;

    while (pos)
    {
      if (szQuoteDelimiter)
        qpos = strstr(szStr, szQuoteDelimiter);
      if (qpos == szStr)
      { // look for next matching quote -- very simple search that finds the next quote or end of string, irrespective of
        //  delimiters
        qpos++;
        qpos = strstr(qpos, szQuoteDelimiter);
        if (qpos == NULL) // no matching end delimiter; pretend end of string is delimited
          break; // let the end piece pick this up as a full fragment
        if ((qpos + 1) != pos)
        { // quote delimiter is not just before pos -- qpos is the new pos
          pos = qpos;
        }
      }
      if (*pos == '\0')
        break;
      *pos = '\0';
      TRIM_CHARS(lastpos, szQuoteDelimiter);
      (*aszOut)[stRet++] = trimmed;
      pos += strlen(szDelimiter);
      lastpos = pos;
      pos = strstr(pos, szDelimiter);
      if (stRet >= stAllocated)
      {
        stAllocated += 10;
        *aszOut = (CHAR **)realloc(*aszOut, stAllocated * sizeof(CHAR *));
      }
    }
    // pick up the leftover part into the last element of the array, if there is anything
    if (lastpos && (*lastpos != '\0'))
    {
      *aszOut = (CHAR **)realloc(*aszOut, (++stRet) * sizeof(CHAR *));    // dealloc empty array elements
      // trim quote delimiter, if required
      TRIM_CHARS(lastpos, szQuoteDelimiter);
      (*aszOut)[stRet - 1] = trimmed;
    }
    else
      *aszOut = (CHAR **)realloc(*aszOut, stRet * sizeof(CHAR *));
  }
  else
  { 
    *aszOut = (CHAR **)malloc(sizeof(CHAR *));
    (*aszOut)[0] = szStr; 
    stRet = 1;
  }
  free(szSafeCopyBuffer);
  return stRet;
  #undef TRIM_CHARS
}

CHAR *CINIFile::ltrim(CHAR *sz, const CHAR *szTrimChars)
{
  // trims specified chars from left of char *
  //  copies trimmed string back onto sz & returns sz pointer (for recursive calls and the like)
  if ((sz == NULL) || (strlen(sz) == 0))
    return sz;
  CHAR *pos = sz;
  while ((*pos != '\0') && (strchr(szTrimChars, *pos)))
    pos++;
  if (pos != sz)
    this->strcpy_overlapped(sz, pos);
  return sz;
}

CHAR *CINIFile::rtrim(CHAR *sz, const CHAR *szTrimChars)
{
  // trims specified chars from right of char *
  //  copies trimmed string back onto sz & returns sz pointer (for recursive calls and the like)
  if ((sz == NULL) || (strlen(sz) == 0))
    return sz;
  CHAR *pos = sz + (strlen(sz) - 1);
  CHAR *lastnull = NULL;
  while (strchr(szTrimChars, *pos) && (pos != sz))
  {
    lastnull = pos;
    pos--;
  }
  if (lastnull)
    *lastnull = '\0';
  return sz;
}

CHAR *CINIFile::trim(CHAR *sz, const CHAR *trimchars, bool trimleft, bool trimright, bool respectEscapeChar)
{
  if ((sz == NULL) || (strlen(sz) == 0))
    return sz;
  CHAR *ret = sz;
  if (trimleft)
    ret = this->ltrim(sz, trimchars);
  if (trimright)
    ret = this->rtrim(sz, trimchars);
  return ret;
}

CHAR * CINIFile::join(const CHAR **aszParts, size_t stParts, const CHAR *szDelimiter, int intStart, int intEnd)
{
  size_t stStart, stEnd;
  if (intStart < 1)
    stStart = 0;
  else if ((size_t)intStart >= (stParts-1))
    return NULL;  // out of range
  if (intEnd < 0)
    stEnd = stParts;
  else if ((size_t)intEnd >= (stParts-1))
    return NULL; // out of range
  if (stEnd > stStart)
  {
    size_t stTmp = stStart;
    stStart = stEnd;
    stEnd = stTmp;
  }

  // determine required buffer length
  size_t stReq = strlen(szDelimiter) * (stEnd - stStart);
  for (size_t i = stStart; i < stEnd; i++)
    stReq += strlen(aszParts[i]);

  CHAR *ret = this->CloneStr("", stReq);
  
  for (size_t i = stStart; i < stEnd; i++)
  {
    if (i > stStart)
      strcat(ret, szDelimiter);
    strcat(ret, aszParts[i]);
  }
  return ret;
}

void CINIFile::CleanLine(CHAR **szLine, CHAR **szComments)
{
  // remove comments -- remember INI files use ";" for a comment
  // works on input buffer; *szComments, if not NULL, points into
  //  the original buffer
  *szComments = NULL;
  unsigned int uiQuotes = 0;
  for (CHAR *pos = *szLine; *pos != '\0'; pos++)
  {
    switch(*pos)
    {
      case '"':
      {
        uiQuotes++;
        break;
      }
      case ';':
      {
        if (uiQuotes % 2)
          break;
        *szComments = pos;
        (*szComments)++;  // inc ptr past comment char
        *pos = '\0';  // null-terminate to remove comment from viewed line
        break;
      }
    }
  }

  this->trim(*szLine);
  this->trim(*szComments);
}

void CINIFile::SetMemOnly(const CHAR *szSection, const CHAR *szSetting, bool fSetMemOnly)
{
  long long spos = this->SectionIDX(szSection);
  long long vpos = -1;
  if (spos > -1)
    vpos = this->ValueIDX((size_t)spos, szSetting);
  if (vpos > -1)
  {
    this->masSections[(size_t)spos]->Settings[(size_t)vpos]->MemOnly = fSetMemOnly;
  }
}

size_t CINIFile::PushSection(CINIFile::CIniFileSection *s)
{
  if ((this->mstSectionsAlloc - this->mstSections) < 2)
  {
    this->mstSectionsAlloc += INI_SECTION_ALLOC_CHUNK;
    this->masSections = (CIniFileSection **)realloc(this->masSections, this->mstSectionsAlloc * sizeof(CIniFileSection *));
  }
  this->masSections[this->mstSections++] = s;
  size_t ret = this->mstSections-1;
  this->AddSectionLookup(s->Name, ret);
  return ret;
}

unsigned int CINIFile::SetValue(const CHAR *szSection, const CHAR *szName, const CHAR *szValue, const CHAR *szComment, bool fMemOnly, bool fReferenceInputBuffers)
{
  // returns the position of the value within the section
  // inputs:
  //    szSection:  name of section to add to
  //    szName:     name of parameter to set
  //    szValue:    value of parameter to set
  //    szComment:  comment associated with parameter
  //    fMemOnly:   if true, this setting is never output to a file or render stream
  //    fReferenceInputBuffers: if true, this function assumes that the input buffers are pointers into a protected
  //                area and re-uses the pointers instead of allocating memory for section,name,value,comment
  //                buffers. This should provide a speed boost (esp on win) and a mem drop. Use with caution however.
  long long spos;
  spos = this->SectionIDX(szSection);
  
  long long vpos = -1;
  if (spos > -1)
    vpos = this->ValueIDX((size_t)spos, szName);
  CIniFileItem *ii = NULL;
  if (vpos == -1)
  {
    this->mfSorted = false;
    CIniFileSection *s;
    if (spos == -1)
    {
      s = new CIniFileSection();
      s->owner = this;
      if (fReferenceInputBuffers)
        s->Name = (CHAR *)szSection;
      else
      {
        CLONESTR(s->Name, szSection);
        s->ChangedName = true;
      }
      spos = this->PushSection(s);
    }
    else
      s = this->masSections[(size_t)spos];
      
    ii = new CIniFileItem();
    s->PushSetting(ii);
    vpos = s->SettingCount - 1;
  }
  else
    ii = this->masSections[(size_t)spos]->Settings[(size_t)vpos];
  if (!this->str_equal(ii->Name, szName))
  {
    if (fReferenceInputBuffers)
      ii->Name = (CHAR *)szName;
    else
    {
      CLONESTR(ii->Name, szName);
      ii->ChangedName = true;
    }
  }
  if (!this->str_equal(ii->Value, szValue))
  {
    if (fReferenceInputBuffers)
      ii->Value = (CHAR *)szValue;
    else
    {
      CLONESTR(ii->Value, szValue);
      ii->ChangedValue = true;
    }
  }
  if (!this->str_equal(ii->Comment, szComment))
  {
    if (fReferenceInputBuffers)
      ii->Comment = (CHAR *)szComment;
    else
    {
      CLONESTR(ii->Comment, szComment);
      ii->ChangedComment = true;
    }
  }
  ii->MemOnly = fMemOnly;
  
  // cache this for lookup purposes later
  this->mstLastSectionIndex = (size_t)spos;
  this->mszLastSection = this->masSections[this->mstLastSectionIndex]->Name;
  
  return (unsigned int)vpos;
}

bool CINIFile::str_equal(const CHAR *sz1, const CHAR *sz2)
{
  if (sz1 == NULL || sz2 == NULL)
    return (sz2 == sz1) ? true : false; // at least one is NULL
  else
    return (strcmp(sz1, sz2)) ? false : true;
}

bool CINIFile::AppendString(CHAR **szBuffer, const CHAR *szAppend, size_t *stBufferLen, size_t stReallocChunkSize, size_t stOccurrences)
{
  bool fRet = true;
  if (stOccurrences == 0)
  {
    if (*szBuffer == NULL)
    {
      *szBuffer = (CHAR *)malloc(stReallocChunkSize * sizeof(CHAR));
      if (*szBuffer)
      {
        (*szBuffer)[0] = '\0';
      }
      else
        return false;
    }
    return true;
  }
  size_t stRequired = stOccurrences * (strlen(szAppend)) + 1;
  if (*szBuffer)
    stRequired += strlen(*szBuffer);
  if (stRequired > *stBufferLen)
  {
    size_t stInc = stReallocChunkSize;
    while ((stInc + *stBufferLen) < stRequired)
      stInc += stReallocChunkSize;
    *stBufferLen += stInc;
    CHAR *tmp;
    if (*szBuffer)
      tmp = (CHAR *)realloc(*szBuffer, (*stBufferLen) * sizeof(CHAR));
    else
      tmp = (CHAR *)malloc((*stBufferLen) * sizeof(CHAR));
    if (tmp == NULL)
    {
      #if defined(_CONSOLE) || defined(LINUX)
      if (*szBuffer)
        fprintf(stderr, "CommonFunctions: AppendString: Unable to malloc %llu bytes on existing pointer %x (current size %llu)\n",
          (unsigned long long)(*stBufferLen), *szBuffer, *stBufferLen);
      else
        fprintf(stderr, "CommonFunctions: AppendString: Unable to realloc %llu bytes on null pointer %x (current size %llu)\n",
          (unsigned long long)(*stBufferLen), *szBuffer, *stBufferLen);
      #endif
      if (*szBuffer)
        free(*szBuffer);
      *stBufferLen = 0;
      return false;
    }
    if (*szBuffer == NULL)
      tmp[0] = '\0';
    *szBuffer = tmp;
  }
  for (size_t i = 0; i < stOccurrences; i++)
    strcat(*szBuffer, szAppend);
  return fRet;
}


const CHAR *CINIFile::ToSZ(const CHAR *szHeader, bool fShowComments)
{
#define APPEND(sz) \
  do { \
    this->AppendString(&(this->mszRenderBuffer), sz, &(this->mstRenderBufferAlloc), INI_RENDER_ALLOC_CHUNK); \
  } while (0)

  if (szHeader && (strlen(szHeader)) && fShowComments)
  {
    CHAR **aszHeaderLines = NULL;
    size_t stHeaderLines = this->split(&aszHeaderLines, szHeader, NEWLINE);
    for (unsigned int i = 0; i < stHeaderLines; i++)
    {
      CHAR *szLine = aszHeaderLines[i];
      if ((strlen(szLine) == 0) || (szLine[0] != ';'))
        APPEND(";");
      APPEND(szLine);
      APPEND(NEWLINE);
    }
    APPEND(NEWLINE);
    this->FreeSZArray(aszHeaderLines, stHeaderLines);
  }
  
  CIniFileSection *section;
  for (size_t i = 0; i < this->mstSections; i++)
  {
    section = this->masSections[i];
    if (i)
      APPEND(NEWLINE);
    if (section->Name)   // enable simple conf file usage
    {
      APPEND("[");
      APPEND(section->Name);
      APPEND("]");
      APPEND(NEWLINE);
    }
    for (size_t j = 0; j < section->SettingCount; j++)
    {
      if (section->Settings[j]->MemOnly)    // todo: perhaps apply a better way to do this
        continue;
      if (section->Settings[j]->IsLineComment)
      {
        if (fShowComments && section->Settings[j]->Comment && strlen(section->Settings[j]->Comment))
          APPEND(section->Settings[j]->Comment);
        continue;
      }
      if (fShowComments && section->Settings[j]->Comment && strlen(section->Settings[j]->Comment))
      {
        APPEND(";");
        APPEND(section->Settings[j]->Comment);
        APPEND(NEWLINE);
      }
      if (section->Settings[j]->Value)
      { // this means that you can delete a setting by setting the value to NULL....
        APPEND(section->Settings[j]->Name);
        APPEND("=");
        APPEND(section->Settings[j]->Value);
        APPEND(NEWLINE);
      }
    }
  }
  
  if (fShowComments && this->mszTrailingComment && (strlen(this->mszTrailingComment)))
    APPEND(this->mszTrailingComment);
  return this->mszRenderBuffer;
#undef APPEND
}

void CINIFile::SetTabular(bool fIsTabular, const CHAR *szMainColumnName)
{
  this->mfTableFormat = fIsTabular; 
  if (this->mszTabularLeadColumn)
    this->ReleaseClone(&this->mszTabularLeadColumn);
  if (szMainColumnName) 
    this->mszTabularLeadColumn = this->CloneStr(szMainColumnName);
}

bool CINIFile::WriteTabularFile(const CHAR *szFileName)
{
  if (szFileName == NULL)
    return false;
  if (strlen(szFileName) == 0)
    return false;
  if (this->mstSections == 0)
    return false; // nothing to do
  FILE *fp = fopen(szFileName, "wb");
  if (fp == NULL)
    return false;
  const CHAR *szTab = "\t";
  #define WRITESTR(_str) \
  do { \
    size_t _len = strlen(_str); \
    if (fwrite(_str, sizeof(CHAR), _len, fp) != _len) \
    { \
      fclose(fp); \
      return false; \
    } \
  } while (0)
  
  if (this->mszTabularLeadColumn == NULL)
    this->mszTabularLeadColumn = this->CloneStr("");
  WRITESTR(this->mszTabularLeadColumn);
  WRITESTR(szTab);
  
  #define WRITESZ(_sz, _delimiter) \
  do { \
    if (_sz) \
    { \
      size_t stLen = strlen(_sz); \
      if (fwrite(_sz, sizeof(CHAR), stLen, fp) != stLen) \
      { \
        fclose(fp); \
        return false; \
      } \
      if (_delimiter && strlen(_delimiter)) \
      { \
        if (fwrite(_delimiter, sizeof(CHAR), strlen(_delimiter), fp) != stLen) \
        { \
          fclose(fp); \
          return false; \
        } \
      } \
    } \
  } while (0)
  size_t stColumns = this->masSections[0]->SettingCount;
  CHAR **aszColumns = (CHAR **)malloc(stColumns * sizeof(CHAR *));
  for (size_t i = 0; i < this->masSections[0]->SettingCount; i++)
  {
    aszColumns[i] = this->masSections[0]->Settings[i]->Name;
    if (i)
    {
      const CHAR *sz2 = ((i+1)<stColumns)?"\t":"\r\n";
      WRITESZ(aszColumns[i], sz2);
    }
  }
  for (size_t i = 0; i < this->mstSections; i++)
  {
    const CHAR *szSection = this->masSections[i]->Name;
    bool fLastLine = ((i+1) < this->mstSectionLookup)?true:false;
    for (size_t j = 0; j < stColumns; j++)  // now this is some particularly horrid code. Just how lazy can I get?
    {
      const CHAR *sz2 = ((j+1)<stColumns)?"\t":((fLastLine)?"":"\r\n");
      WRITESZ(this->GetSZValue(szSection, aszColumns[j]), sz2);
    }
  }
  fclose(fp);
  free(aszColumns);
  return true;
}

bool CINIFile::WriteFile(const CHAR *szFileName, const CHAR *szHeader)
{
  const CHAR *szOutFile = szFileName;
  if ((szOutFile == NULL) || (strlen(szOutFile) == 0))
    szOutFile = this->mszMainFile;
  if (this->mfTableFormat)
    return this->WriteTabularFile(szOutFile);
  FILE *fp = fopen(szOutFile, "wb");
  if (fp == NULL)
  {
    fprintf(stderr, "ERROR: Unable to open '%s' for writing: %s\n", szOutFile,
      this->Error(errno)); 
    return false;
  }
  
#define APPEND(sz) \
  do { \
    if (sz) \
    { \
      size_t stToWrite = strlen(sz); \
      if (stToWrite) \
      { \
        if (fwrite(sz, sizeof(CHAR), stToWrite, fp) != stToWrite) \
        { \
          fprintf(stderr, "Unable to write %i bytes to file '%s': %s\n", \
              (int)stToWrite, szFileName, this->Error(errno)); \
          fclose(fp); \
          return false; \
        } \
      } \
    } \
  } while (0)

  if (szHeader && (strlen(szHeader)))
  {
    CHAR **aszLines = NULL;
    size_t stLines = this->split(&aszLines, szHeader, NEWLINE);
    for (unsigned int i = 0; i < stLines; i++)
    {
      CHAR *szLine = aszLines[i];
      if ((strlen(szLine) == 0) || (szLine[0] != ';'))
        APPEND(";");
      APPEND(szLine);
      APPEND(NEWLINE);
    }
    this->FreeSZArray(aszLines, stLines);
    APPEND(NEWLINE);
  }
  
  // write out
  CIniFileSection *section;
  fflush(fp);
  for (size_t i = 0; i < this->mstSections; i++)
  {
    if (i)
      APPEND(NEWLINE);
    section = this->masSections[i];
    if (section->Name && strlen(section->Name))   // enable simple conf file handling
    {
      APPEND("[");
      APPEND(section->Name);
      APPEND("]");
      APPEND(NEWLINE);
    }
    
    for (size_t j = 0; j < section->SettingCount; j++)
    {
      if (section->Settings[j]->MemOnly)
        continue;
      CIniFileItem *setting = section->Settings[j];
      if (setting->Comment && strlen(setting->Comment))
      {
        APPEND(";");
        APPEND(setting->Comment);
        APPEND(NEWLINE);
      }
      if (section->Settings[j]->IsLineComment)
        APPEND(setting->Value);
      else 
      {
        APPEND(setting->Name);
        APPEND("=");
        APPEND(setting->Value);
        APPEND(NEWLINE);
      }
    }
  }
  if (this->mszTrailingComment && (strlen(this->mszTrailingComment)))
    APPEND(this->mszTrailingComment);
  fclose(fp);
  return true;
}

void CINIFile::SetComment(const CHAR *szSection, const CHAR *szName, const CHAR *szComment, const CHAR *szMissingVal)
{
  long long spos = this->SectionIDX(szSection);
  long long vpos = -1;
  if (spos > -1)
    vpos = this->ValueIDX((size_t)spos, szName);
  if (vpos > -1)
  {
    CIniFileItem *ii = this->masSections[(size_t)spos]->Settings[(size_t)vpos];
    ii->ChangedComment = true;
    CLONESTR(ii->Comment, szComment);
  }
  else
  { // add in the missing setting as a commented out line
    // this code is not perfect: if you set a new item's comment and then set the item, you will get
    // the item and the commented out one. Just order it right until I'm unlazy enough to fix it
    CHAR *szTmp = this->CloneStr(NULL, (2 + strlen(szName)));
    sprintf(szTmp, ";%s", szName);
    this->SetValue(szSection, szTmp, szMissingVal, szComment);
    this->ReleaseClone(&szTmp);
  }
}

void CINIFile::AppendComment(const CHAR *szSection, const CHAR *szName, const CHAR *szComment)
{
  long long spos = this->SectionIDX(szSection);
  long long vpos = -1;
  if (spos > -1)
    vpos = this->ValueIDX((size_t)spos, szName);
  if (vpos == -1)
  {
    CHAR *szTest = (CHAR *)malloc((2 + strlen(szName)) * sizeof(CHAR));
    sprintf(szTest, ";%s", szName);
    vpos = this->ValueIDX((size_t)spos, szTest);    // may already be in the file as an optional 
    free(szTest);
  }
  if (vpos > -1)
  {
    CIniFileItem *setting = this->masSections[(size_t)spos]->Settings[(size_t)vpos];
    size_t stAlloc = 0;
    if (setting->Comment && strlen(setting->Comment))
    {
      stAlloc = strlen(setting->Comment);
      this->AppendString(&(setting->Comment), NEWLINE, &stAlloc, INI_SETTING_ALLOC_CHUNK, 1);
      this->AppendString(&(setting->Comment), ";  ", &stAlloc, INI_SETTING_ALLOC_CHUNK, 1);
    }
    this->AppendString(&(setting->Comment), szComment, &stAlloc, 1);
  }
  else
    fprintf(stderr, "ERROR: Unable to append comment on '%s::%s': no such setting found\n", 
      szSection, szName);
}

CHAR *CINIFile::Error(int e)
{
  // you can free this with ReleaseClone() if you like, but you probably don't need to bother
  //  since it is freed at xtor time
  CHAR *ret;
  
  #ifdef _REENTRANT
  #ifndef ERR_BUF_SIZE
  #define ERR_BUF_SIZE 64
  #endif
  char errbuf[ERR_BUF_SIZE];
  strerror_r(e, errbuf, ERR_BUF_SIZE);
  ret = this->CloneStr(errbuf);
  #else
  ret = this->CloneStr(strerror(e));
  #endif
  
  return ret;
}

#if defined (WIN32) || defined (_W32_WCE) || defined _WIN32
size_t CINIFile::ListDirContents(CHAR ***aszOut, const CHAR *szDirName, bool fPrependDirName, const CHAR *szMask)
#else
size_t CINIFile::ListDirContents(CHAR ***aszOut, const CHAR *szDirName, bool fPrependDirName)        // don't know about file masks in *nix
#endif
{
  struct stat st;
  size_t ret = 0;
  if (stat(szDirName, &st) != 0)
    return ret;
  size_t stAlloc = 128;
  *aszOut = (CHAR **)malloc(stAlloc * sizeof(CHAR *));
#if defined (WIN32) || defined (_W32_WCE) || defined _WIN32
  WIN32_FIND_DATA findFileData;
  CHAR *szSearch = this->CloneStr(szDirName, strlen(PATH_DELIMITER) + strlen(szMask));
  strcat(szSearch, PATH_DELIMITER);
  strcat(szSearch, szMask);
  HANDLE hFind = FindFirstFile(szSearch, &findFileData);
  while (hFind != INVALID_HANDLE_VALUE)
  {
    CHAR *fn;
    if (fPrependDirName)
    {
      fn = this->CloneStr(szDirName, strlen(PATH_DELIMITER) + strlen(findFileData.cFileName));
      strcat(fn, PATH_DELIMITER);
      strcat(fn, findFileData.cFileName);
    }
    else
      fn = this->CloneStr(findFileData.cFileName);
    if ((stAlloc - ret) < 2)
    {
      stAlloc += 128;
      *aszOut = (CHAR **)realloc(*aszOut, stAlloc * sizeof(CHAR*));
    }
    (*aszOut)[ret] = fn;
    if (!FindNextFile(hFind, &findFileData))
      break;
  }
  this->ReleaseClone(&szSearch);
  FindClose(hFind);
#else
  DIR *dir = opendir(szDirName);
  if (dir)
  {
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
      CHAR *fn;
      if (fPrependDirName)
      {
        fn = this->CloneStr(szDirName, strlen(PATH_DELIMITER) + strlen(ent->d_name));
        strcat(fn, PATH_DELIMITER);
        strcat(fn, ent->d_name);
      }
      else
        fn = this->CloneStr(ent->d_name);
      if ((stAlloc - ret) < 2)
      {
        stAlloc += 128;
        *aszOut = (CHAR **)realloc(*aszOut, stAlloc * sizeof(CHAR*));
      }
      (*aszOut)[ret] = fn;
    }
    closedir(dir);
  }
#endif
  return ret;
}

const CHAR *CINIFile::FileExt(const CHAR *szFileName)
{
  const CHAR *szBaseName = this->BaseName(szFileName);
  return this->BaseName(szBaseName, '.');
}

const CHAR *CINIFile::BaseName(const CHAR *szFileName, const CHAR cDelimiter)
{
  const CHAR *ret = strrchr(szFileName, cDelimiter);
  if (ret == NULL)
    return szFileName;
  return ret;
}

bool CINIFile::SectionExists(const CHAR *szSection)
{
  if (this->LookUpSectionByName(szSection) > -1)
    return true;
  else
    return false;
}

int CINIFile::_strcasecmp(const char *sz1, const char *sz2, bool fDefinitelyCaseInsensitive)
{
  if (this->CaseSensitive && !fDefinitelyCaseInsensitive)
    return strcmp(sz1, sz2);
  else
  #if defined(WIN32) || defined(WIN64)
  return stricmp(sz1, sz2);
  #else
  return ::strcasecmp(sz1, sz2);
  #endif
}

int CINIFile::str_replace(CHAR **szMain, const CHAR *szFind, const CHAR *szReplace, size_t *stBufLen, bool fRespectSlashQuoting)
{
  /*
   * replaces one substring in a cstring with another
   * returns the number of substitutions done
   *  valid return values: -1 (out of space in the buffer)
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

  CHAR *szSearchStart = *szMain;
  while ((pos = strstr(szSearchStart, szFind)))
  {
    if (fRespectSlashQuoting && (pos != *szMain) && (pos[-1] == '\\'))
    {
      szSearchStart = pos + 1;
      continue;
    }
    ret++;
    CHAR *endpos = pos + strlen(szFind);
    size_t stTmpLen = strlen(endpos);
    if (stTmpLen)
    {
      if (stMyBufLen < (strlen(*szMain) + (stReplaceLen - stFindLen + 1)))
      {
        if (szTmp)
          free(szTmp);
        return -1;;
      }
      if (stTmpBuf < stTmpLen)
      {
        stTmpBuf = stTmpLen + 1;
        szTmp = (CHAR *)realloc(szTmp, stTmpBuf * sizeof(CHAR));
      }
      strcpy(szTmp, endpos);
      strcpy(pos, szReplace);
      pos += stReplaceLen;
      strcat(pos, szTmp);
      szSearchStart = pos;
    }
    else  // only one match, right-aligned on szMain
    {
      *pos = '\0';
      if ((stMyBufLen - strlen(*szMain)) < (strlen(szFind) + 1))
      {// not enough space in the buffer; expand if possible
        if (szTmp)
          free(szTmp);
        return -1;
      }
      strcat(*szMain, szReplace);
    }
  }
  if (szTmp)
    free(szTmp);
  return ret;
}

const CHAR *CINIFile::GetSectionAlias(const CHAR *szSection)
{
  long long sidx = this->SectionIDX(szSection);
  if (sidx > -1)
    return this->masSections[(size_t)sidx]->Alias;
  else
    return NULL;
}

const CHAR *CINIFile::GetSectionAlias(size_t stIndex)
{
  if (stIndex < this->mstSections)
  {
    CIniFileSection *s = this->masSections[stIndex];
    if (s->Alias && strlen(s->Alias))
      return s->Alias;
  }
  return NULL;
}

void CINIFile::SetSectionAlias(size_t stIndex, const CHAR *szNewAlias)
{
  if (stIndex < this->mstSections)
  {
    if (szNewAlias)
      CLONESTR(this->masSections[stIndex]->Alias, szNewAlias);
    else
      RELEASECLONE(this->masSections[stIndex]->Alias);
  }
}

void CINIFile::SetSectionAlias(const CHAR *szSection, const CHAR *szNewAlias)
{
  long long sidx = this->SectionIDX(szSection);
  if (sidx > -1)
  {
    if (szNewAlias)
      CLONESTR(this->masSections[(size_t)sidx]->Alias, szNewAlias);
    else
      RELEASECLONE(this->masSections[(size_t)sidx]->Alias);
  }
}

const CHAR *CINIFile::GetSection(size_t i)
{
  if (i < this->mstSections)
    return this->masSections[i]->Name;
  else
    return NULL;
}

const CHAR *CINIFile::GetSetting(const CHAR *szSection, size_t i)
{
  long long llIDX = this->LookUpSectionByName(szSection);
  if (llIDX == -1)
    return NULL;
  CIniFileSection *s = this->masSections[(size_t)llIDX];
  if (i >= s->SettingCount)
    return NULL;
  return s->Settings[i]->Name;
}

size_t CINIFile::SectionCount()
{
  return this->mstSections;
}

#if defined(WIN32) || defined(WIN64)
const CHAR *CINIFile::GetAppINIFileName()
#else
const CHAR *CINIFile::GetAppINIFileName(const CHAR *szAppPath)
#endif
{
  #ifndef WIN32
  size_t stLen = strlen(szAppPath);
  #endif
  #if defined(WIN32) || defined(WIN64)
  size_t stLen = GetModuleFileNameA(NULL, this->mszAppIniFilePath, MAX_PATH);
  #endif
  if (stLen > (MAX_PATH-1))   // respect MAX_PATH, even on non-win32
    return false;
  #ifndef WIN32
  // TODO: add support for determining the path if not absolute, via getcwd or similar
  // NOTE: this functionality is more than likely quite broken on Linux due to the fun
  //  of paths and symlinks; but there is a way to fix it. It's just not done (yet)
  strcpy(this->mszAppIniFilePath, szAppPath);
  #endif
  
  CHAR *pos = strrchr(this->mszAppIniFilePath, PATH_DELIMITER_C);
  pos = strrchr(pos, '.');
  if (pos != NULL)
    *pos = '\0';
  strcat(this->mszAppIniFilePath, ".ini");
  return (const CHAR *)(this->mszAppIniFilePath);
}

CHAR *CINIFile::CloneStr(const CHAR *szToClone, size_t stPad)
{
  size_t stReq = stPad;
  if (szToClone)
    stReq += strlen(szToClone);
    
  struct SStringBuffer *s;
  struct SStringBuffer **a = this->masStringPool;
  for (size_t i = 0; i < this->mstStringPool; i++)
  {
    if (a[i]->fInUse)
      continue;
    s = a[i];
    if (s->stLen >= stReq)
    {
      s->fInUse = true;
      if (szToClone)
        strcpy(s->szBuf, szToClone);
      else
        s->szBuf[0] = '\0';
      return s->szBuf;
    }
  }
  
  s = new SStringBuffer(szToClone, stPad, true);
  if (this->mstStringPool >= this->mstStringPoolAlloc)
  {
    this->mstStringPoolAlloc += INI_STRINGPOOL_ALLOC;
    this->masStringPool = (struct SStringBuffer **)realloc(this->masStringPool, 
      this->mstStringPoolAlloc * sizeof(struct SStringBuffer *));
  }
  this->masStringPool[this->mstStringPool++] = s;
  return s->szBuf;
}

void CINIFile::ReleaseClone(CHAR **szToRelease)
{
  SStringBuffer *s1, *s2;
  if (szToRelease == NULL)
    return;
  if (*szToRelease == NULL)
    return;
  for (size_t i = 0; i < this->mstStringPool; i++)
  {
    s1 = this->masStringPool[i];
    if (s1->szBuf == *szToRelease)
    {
      // mark ready for use again
      s1->fInUse = false;
      s1->szBuf[0] = '\0';
      // move to the top of the pile (or as close as possible) for a quicker search at next request, if possible
      for (size_t j = 0; j < this->mstStringPool; j++)
      {
        s2 = this->masStringPool[j];
        if (s2->fInUse)
        {
          this->masStringPool[i] = s2;
          this->masStringPool[j] = s1;
          *szToRelease = NULL;
          return;
        }
      }
      *szToRelease = NULL;
      return;
    }
  }
}

void CINIFile::QuickSortLookup(SLookup **as, size_t stStart, size_t stElements, SLookup **asPre, SLookup **asPost)
{
  // implementation of the QuickSort algorythm
  //  please see http://en.wikipedia.org/wiki/Quicksort for details on how this works
  if (stElements < 2)
    return;
  bool fFreeStuff = false;
  size_t stPivot = stStart + (stElements / 2);

  if ((asPre == NULL) || (asPost == NULL))
  {
    fFreeStuff = true;
    asPre = (SLookup **)malloc(stElements * sizeof(SLookup *));
    asPost = (SLookup **)malloc(stElements * sizeof(SLookup *));
  }
  size_t stPre = 0, stPost = 0;

  const CHAR *szPivot = as[stPivot]->Name;
  const CHAR *szI;
  for (size_t i = stStart; i < stPivot; i++)
  {
    szI = as[i]->Name;
    if (this->_strcasecmp(szI, szPivot) > 0)
      asPost[stPost++] = as[i];
    else
      asPre[stPre++] = as[i];
  }
  size_t stEnd = stStart + stElements;
  for (size_t i = stPivot + 1; i < stEnd; i++)
  {
    szI = as[i]->Name;
    if (this->_strcasecmp(szI, szPivot) > 0)
      asPost[stPost++] = as[i];
    else
      asPre[stPre++] = as[i];
  }

  as[stStart + stPre] = as[stPivot];
  for (size_t i = 0; i < stPre; i++)
    as[stStart + i] = asPre[i];
  size_t stOffset = stStart + stPre + 1;
  for (size_t i = 0; i < stPost; i++)
    as[stOffset + i] = asPost[i];
    
  this->QuickSortLookup(as, stStart, stPre, asPre, asPost);
  stPre++;
  this->QuickSortLookup(as, stStart + stPre, stElements-stPre, asPre, asPost);

  if (fFreeStuff)
  {
    free(asPre);
    free(asPost);
  }
}

long long CINIFile::SearchLookupArray(SLookup **a, const size_t stLen, const CHAR *szSearch)
{
  // binary search; requires a to be ordered
  // NOTE: this doesn't return the position of the lookup item; rather
  //        it returns the index being held as a lookup
  long long llLower = 0;
  long long llUpper = (long long)(stLen) - 1;
  while (llLower <= llUpper)
  {
    long long idx = llLower + (size_t)((llUpper - llLower)/2);
    int cmp = this->_strcasecmp(a[idx]->Name, szSearch);
    if (cmp == 0)
      return a[idx]->Index;
    if (cmp > 0)
      llUpper = idx-1;
    else
      llLower = idx + 1;
  }
  return -1;
}

void CINIFile::RemoveIndex(SLookup ***a, size_t *stLen, const CHAR *szSearch)
{
  size_t stLower = 0;
  size_t stUpper = *stLen - 1;
  while (stLower <= stUpper)
  {
    size_t idx = stLower + (size_t)((stUpper - stLower)/2);
    int cmp = this->_strcasecmp((*a)[idx]->Name, szSearch);
    if (cmp == 0)
    { // delete this lookup and shift all below up by one
      // leave the lookup array at the same size for later use
      delete (*a)[idx];
      (*stLen)--;
      for (size_t i = idx; i < *stLen; i++)
        (*a)[i] = (*a)[i+1];
    }
    if (cmp < 0)
      stUpper = idx-1;
    else
      stLower = idx + 1;
  }
  
}

bool CINIFile::WriteMemToFile(const CHAR *szFileName, void *vpFileData, size_t stFileSize) 
{
  /*
  * Writes a segment of memory to a file, if possible
  * inputs: szFileName: path to file to write out mem to
  *         vpFileData: pointer to mem block containing data to write out
  *         stFileSize: length of memory block to write out
  * returns: true if the file was written out in its entirety; false on file I/O errors
  * NOTE: this function will persistently try to open a file that it fails to open (up to MAX_FOPEN_ATTEMPTS) times
  *         to cover for temporary access errors; once the file is open, however, write errors cause termination
  *         of the routine.
  */
  if (strlen(szFileName) > MAX_PATH)
    return false;
  bool fRet = true;
  FILE *fp;
  CHAR *szOutDir = this->DirName(szFileName);
  if ((strlen(szOutDir)) && !this->EnsureDirExists(szOutDir))
  {
    this->ReleaseClone(&szOutDir);
    return false;
  }
  this->ReleaseClone(&szOutDir);
  for (unsigned int i = 0; i < INI_MAX_FOPEN_ATTEMPTS; i++)
  {
    fRet = false;
    if ((fp = fopen(szFileName, "wb")))
    {
      fRet = true;
      break;
    }
    #ifdef _LOG_H_
    if ((INI_MAX_FOPEN_ATTEMPTS - i) > 1)
      Log(LS_ERROR, "Unable to open %s for writing, will try again in 1 sec", szFileName);
    #else
      printf("Unable to open %s for writing, will try again in 1 sec", szFileName);
    #endif
    #if defined(WIN32) || defined(WIN64)
    Sleep(INI_SLEEP_FOPEN_FAILS);
    #else
    usleep(INI_SLEEP_FOPEN_FAILS * 1000);
    #endif
  }
  if (fp)
  {
    size_t stWriteNow, stWritten = 0;
    CHAR *ptr = (CHAR *)vpFileData;
    while (stWritten < stFileSize)
    {
      stWriteNow = stFileSize - stWritten;
      if (stWriteNow > INI_MAX_FILE_OPER_BYTES)
        stWriteNow = INI_MAX_FILE_OPER_BYTES;
      if (fwrite(ptr, sizeof(CHAR), stWriteNow, fp) == stWriteNow)
      {
        stWritten += stWriteNow;
        ptr += stWriteNow;
      }
      else
      {
        #ifdef _LOG_H_
        Log(LS_ERROR, "Unable to write %li bytes at offset %li to %s\n", (long)stWriteNow, (long)stWritten, szFileName);
        #else
        printf("ERROR: Unable to write %li bytes at offset %li to %s\n", (long)stWriteNow, (long)stWritten, szFileName);
        #endif
        fRet = false;
        break;
      }
    }
    fclose(fp);
  }
  else
  {
    #ifdef _LOG_H_
    Log(LS_ERROR, "Couldn't open %s for writing\n", szFileName);
    #else
    printf("ERROR: Couldn't open %s for writing\n", szFileName);
    #endif
  }
  
  return fRet;
}

bool CINIFile::ReadFileToMem(const CHAR *szFileName, void **vpFileData, size_t *stFileSize, bool fUseMalloc, bool fNullTerminateTextFile) 
{
  /*
  * Reads a file to a memory block, handling memory allocation for the caller
  *   inputs: szFileName: path to file to read
  *           vpFileData: pointer to block of void mem to write file contents to
  *           stFileSize: pointer to size_t to contain the size of the file read to mem
  *           fUseMalloc: boolean to determine whether to use malloc() (true) or new for mem allocation (default: use malloc)
  *           fNullTerminatTextFile: boolean to determine if the buffer should be null-terminated (useful for text files
  *                         where you would like to treat the contents as one cstring. Note that stFileSize still holds
  *                         the number of chars read (which is now strlen((CHAR*)(*vpFileData)), or, iow, the
  *                         length of the buffer MINUS 1 char (for the null-terminator)
  *   returns: true if the file could be read to mem; false otherwise (eg: file doesn't exist, can't be read, or mem
  *                         can't be allocated)
  */
  size_t stRead, stToRead, stTotalRead = 0;
  struct stat st;
  bool fRet = true;
  if (stat(szFileName, &st) == 0)
  {
    FILE *fp = fopen(szFileName, "rb");
    if (fp == NULL)
    {
      #ifdef _LOG_H_
      Log(LS_ERROR, "Unable to open %s for reading", szFileName);
      #else
      printf("ERROR: Unable to open %s for reading\n", szFileName);
      #endif
      if (stFileSize)
        *stFileSize = 0;
      return false;
    }
    if (fNullTerminateTextFile)
    {
      if (fUseMalloc)
        *vpFileData = malloc((st.st_size + 1) * sizeof(char));
      else
        *vpFileData = new char[st.st_size + sizeof(char)];
      char *a = (char *)(*vpFileData);
      a[st.st_size] = '\0';
    }
    else
    {
      if (fUseMalloc)
        *vpFileData = malloc(st.st_size);
      else
        *vpFileData = new char[st.st_size];
    }
    if (*vpFileData == NULL)
    {
      #ifdef _LOG_H_
      Log(LS_ERROR, "Unable to allocate memory to read '%s' to mem", szFileName);
      #else
      printf("Unable to allocate memory to read '%s' to mem", szFileName);
      #endif
      return false;
    }
    CHAR *ptr = (CHAR *)(*vpFileData);
    if (stFileSize)
      *stFileSize = st.st_size;
    while (stTotalRead < (size_t)st.st_size)
    {
      stToRead = st.st_size - stTotalRead;
      if (stToRead > INI_MAX_FILE_OPER_BYTES)
        stToRead = INI_MAX_FILE_OPER_BYTES;
      if ((stRead = fread(ptr, sizeof(CHAR), stToRead, fp)) == stToRead)
      {
        stTotalRead += stRead;
        ptr += stRead;
      }
      else
      {
        #ifdef _LOG_H_
        Log(LS_ERROR, "Unable to read %li bytes at offset %li from file %s", (long)stToRead, (long)stTotalRead, szFileName);
        #else
        printf("Unable to read %li bytes at offset %li from file %s\n", (long)stToRead, (long)stTotalRead, szFileName);
        #endif
        free(*vpFileData);   // leave nothing for the caller to do on a failure
        *vpFileData = NULL;
        if (stFileSize)
          *stFileSize = 0;
        fRet = false;
        break;
      }
    }
    fclose(fp);
  }
  else
  {
    stFileSize = 0;
    return false;
  }
  return fRet;
}

bool CINIFile::EnsureDirExists(const CHAR *szPath, bool fCheckParentOnly, const CHAR cPathDelimiter)
{
  if (strlen(szPath) >= MAX_PATH)
    return false;   // shouldn't exist & can't check this anyway
  struct stat st;
  const CHAR *szCheckPath;
  CHAR *szParentPath = NULL;
  CHAR *szMyPath = NULL;
#define RET(retval) \
  do { \
    if (szParentPath) \
      this->ReleaseClone(&szParentPath); \
    if (szMyPath) \
      this->ReleaseClone(&szMyPath); \
    return retval; \
  } while (0)
  if (fCheckParentOnly)
  {
    szParentPath = this->DirName(szPath);
    szCheckPath = szParentPath;
  }
  else
    szCheckPath = szPath;
  if (stat(szCheckPath, &st) == 0)
  {
    if (st.st_mode & S_IFDIR)
      RET(true);
  }
  
  szMyPath = this->CloneStr(szCheckPath);
  CHAR *pos = strchr(szMyPath, cPathDelimiter);
  if (pos)
  {
    if ((strlen(pos) > 3) && (pos[1] == cPathDelimiter))    // leading \\ or // : this is a samba share
    {
      pos += 2;
      pos = strchr(pos, cPathDelimiter);                  // don't include the server name in the mkdir rounds
      if (pos)
      {
        pos++;    // get to share name -- can't include that in the mkdir rounds
        pos = strchr(pos, cPathDelimiter);
        if (pos)
        {
          pos++;
          pos = strchr(pos, cPathDelimiter);  // get to top-level dir in share (if it's there) -- this is the first dir to check for
        }
      }
    }
    else if (strlen(pos) != strlen(szMyPath))   // not a leading path delimiter; check if there's a drive spec; don't include the drive in the mkdir rounds
    {
      CHAR *npos = pos;
      --npos;
      if ((*npos == ':') || (*npos == cPathDelimiter))
        pos = strchr(++pos, cPathDelimiter);
    }
  }
  while (pos)
  {
    *pos = '\0';
    if (strlen(szMyPath))
    {
      if (stat(szMyPath, &st) == 0)
      {
        if (st.st_mode & S_IFDIR)
        {
          *pos = cPathDelimiter;
          pos = strchr(++pos, cPathDelimiter);
          continue;
        }
        else
        {
          #ifdef _LOG_H_
          Log(LS_ERROR, "Can't create dir '%s': non-dir '%s' in the way", szCheckPath, szMyPath);
          #else
          fprintf(stderr, "Can't create dir '%s': non-dir '%s' in the way", szCheckPath, szMyPath);
          #endif
          RET(false);
        }
      }
      #if defined(WIN32) || defined(WIN64)
      if (mkdir(szMyPath) != 0)
      #else
      if (mkdir(szMyPath, S_IRWXU | S_IRWXG | S_IROTH) != 0)
      #endif
      {
        if (!this->DirExists(szMyPath))   // perhaps something sneaky (like another thread) made this when we weren't looking....
        #ifdef _LOG_H_
          Log(LS_ERROR, "Can't create dir '%s': mkdir fails", szMyPath);
        #else
          fprintf(stderr, "Can't create dir '%s': mkdir fails", szMyPath);
        #endif
        
        RET(false);
      }
    }
    *pos = cPathDelimiter;
    pos = strchr(++pos, cPathDelimiter);
  }
  
  if (this->DirExists(szCheckPath))
    RET(true);
  #if defined(WIN32) || defined(WIN64)
  if (mkdir(szCheckPath) == 0)
    RET(true);
  #else
  if (mkdir(szCheckPath, S_IRWXU | S_IRWXG | S_IROTH) == 0)
    RET(true);
  #endif
  else
  {
    if (!this->DirExists(szCheckPath))
    {
      #ifdef _LOG_H_
      Log(LS_ERROR, "Unable to create dir %s", szCheckPath);
      #else
      printf("Unable to create dir %s", szCheckPath);
      #endif
      RET(false);
    }
  }
  RET(true);
#undef RET
}

bool CINIFile::DirExists(const CHAR *szPath)
{
  struct stat st;
  if (stat(szPath, &st) == 0)
  {
    if (st.st_mode & S_IFDIR)
      return true;
  }
  return false;
}

void CINIFile::PushIndex(SLookup ***a, size_t *stLen, size_t *stAllocated, const CHAR *szPush, size_t stPush)
{
  if ((*stAllocated - *stLen) < 2)
  {
    *stAllocated += INI_SETTING_ALLOC_CHUNK;
    *a = (SLookup **)realloc(*a, (*stAllocated) * sizeof(SLookup *));
  }
  SLookup *tmp = new SLookup(szPush, stPush);
  (*a)[(*stLen)++] = tmp;
}

// CIniFileSection methods
CINIFile::CIniFileSection::CIniFileSection(const CHAR *szName, const CHAR *szAlias, CINIFile::CIniFileItem **asSettings, size_t stSettings)
{
  this->Name = NULL;
  this->Alias = NULL;
  this->owner = NULL;
  if (szName)
  {
    CLONESTR(this->Name, szName);
    this->ChangedName = true;
  }
  else
    this->ChangedName = false;
  if (szAlias)
  {
    CLONESTR(this->Alias, szAlias);
    this->ChangedAlias = true;
  }
  else
    this->ChangedAlias = false;
  this->Settings = asSettings;
  this->SettingCount = stSettings;
  this->mstSettingAlloc = stSettings;
  this->mstSettingIndex = 0;
  this->mstSettingIndexAlloc = 0;
  this->masSettingIndex = NULL;
  this->mfIndexed = false;
}

CINIFile::CIniFileSection::~CIniFileSection()
{
  if (this->ChangedName)
    RELEASECLONE(this->Name);
  if (this->ChangedAlias)
    RELEASECLONE(this->Alias);
  if (this->SettingCount)
  {
    for (size_t i = 0; i < this->SettingCount; i++)
      delete this->Settings[i];
    free(this->Settings);
    for (size_t i = 0; i < this->mstSettingIndex; i++)
      delete this->masSettingIndex[i];
    free(this->masSettingIndex);
  }
}

void CINIFile::CIniFileSection::PushSetting(CINIFile::CIniFileItem *ii)
{
  if ((this->mstSettingAlloc - this->SettingCount) < 2)
  {
    this->mstSettingAlloc += INI_SETTING_ALLOC_CHUNK;
    this->Settings = (CINIFile::CIniFileItem **)realloc(this->Settings, this->mstSettingAlloc * sizeof(CINIFile::CIniFileItem *));
  }
  this->Settings[this->SettingCount++] = ii;
  this->mfIndexed = false;  // delay re-index until the next required lookup
}

void CINIFile::CIniFileSection::DelSetting(const CHAR *szName)
{
  this->IndexSettings();
  long long idx = this->SettingIndex(szName);
  if (idx > -1)
  {
    delete this->Settings[(size_t)idx];
    for (size_t i = (size_t)idx; i < this->SettingCount; i++)
      this->Settings[i] = this->Settings[i+1];
  }
  this->mfIndexed = false;
}

long long CINIFile::CIniFileSection::SettingIndex(const CHAR *szName)
{
  this->IndexSettings();
  if (this->owner)
    return this->owner->SearchLookupArray(this->masSettingIndex, this->mstSettingIndex, szName);
  else
    return -1;
}

void CINIFile::CIniFileSection::IndexSettings()
{
  if (this->mfIndexed)
    return;
  // pre-sort so we can check for existing items
  if (this->owner)
    this->owner->QuickSortLookup(this->masSettingIndex, 0, this->mstSettingIndex);
  else
    return;
  // generate the index items that are missing from the current index
  unsigned int uiAdded = 0;
  for (size_t i = 0; i < this->SettingCount; i++)
  {
    if (this->Settings[i]->IsLineComment || (this->Settings[i]->Name == NULL))
      continue;
    if (this->owner->SearchLookupArray(this->masSettingIndex, this->mstSettingIndex, this->Settings[i]->Name) == -1)
    {
      this->owner->PushIndex(&(this->masSettingIndex), &(this->mstSettingIndex), &(this->mstSettingIndexAlloc),
          this->Settings[i]->Name, i);
      uiAdded++;
    }
  }
  // now do final sort
  if (uiAdded)  // this should always be > 0!
    this->owner->QuickSortLookup(this->masSettingIndex, 0, this->mstSettingIndex);
  this->mfIndexed = true;
}

