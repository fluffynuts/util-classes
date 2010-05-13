#include "Date.h"
#define FN_START \
  if (ttBase == 0) \
    ttBase = this->mttBase; \
  struct tm *t = localtime(&ttBase)
 
#define FN_START_LITE \
	if (ttBase == 0) \
		ttBase = this->mttBase
  
#define FN_START2 \
  bool fInternalDate = false; \
  if (ttBase == 0) \
  { \
    fInternalDate = true; \
    ttBase = this->mttBase; \
  } \
  struct tm *t = localtime(&ttBase)

#define FN_START2_LITE \
  bool fInternalDate = false; \
  if (ttBase == 0) \
  { \
    fInternalDate = true; \
    ttBase = this->mttBase; \
  }
  
#define FN_END \
  if (fInternalDate) \
    this->mttBase = ttBase; \
  return ttBase
    
#define SECS_PER_WEEK   604800
#define SECS_PER_DAY    86400
#define SECS_PER_HOUR   3600

CDate::CDate(void)
{
  this->mttBase = time(NULL);
  #define AF(s, i) \
    { \
      this->mvDateFormats.push_back(SDateFormat(s, i)); \
    }
  AF("%i-%i-%i %i:%i:%i", 6);
  AF("%i/%i/%i %i:%i:%i", 6);
  AF("%i-%i-%i %i:%i", 5);
  AF("%i/%i/%i %i:%i", 5);
  AF("%i-%i-%i 0%i:%i:%i", 6);
  AF("%i/%i/%i 0%i:%i:%i", 6);
  AF("%i-%i-%i 0%i:%i", 5);
  AF("%i/%i/%i 0%i:%i", 5);
  AF("%i-%i-%i", 3);
  AF("%i/%i/%i", 3);
  AF("%i:%i", 2);
  AF("%i:%i:%i", 2);
  AF("0%i:%i", 2);
  AF("0%i:%i:%i", 2);
  #undef AF
}

CDate::~CDate(void)
{
}

CDate::CDate(time_t ttBase)
{
  this->mttBase = ttBase;
}

time_t CDate::StartOfMinute(time_t ttBase)
{
  FN_START;
  t->tm_sec = 0;
  return mktime(t);
}

time_t CDate::StartOfHour(time_t ttBase)
{
  FN_START;
  t->tm_sec = 0;
  t->tm_min = 0;
  return mktime(t);
}

time_t CDate::StartOfDay(time_t ttBase)
{
  FN_START;
  t->tm_sec = 0;
  t->tm_min = 0;
  t->tm_hour = 0;
  return mktime(t);
}

time_t CDate::StartOfWeek(time_t ttBase)
{
  FN_START2;
	// 0 is sunday...
	ttBase = this->StartOfDay(ttBase);
	ttBase = this->AddDays((-1) * t->tm_wday, ttBase);
	FN_END;
}

time_t CDate::StartOfMonth(time_t ttBase)
{
  FN_START;
  t->tm_sec = 0;
  t->tm_min = 0;
  t->tm_hour = 0;
  t->tm_mday = 1;
  return mktime(t);
}

time_t CDate::StartOfYear(time_t ttBase)
{
  FN_START;
  t->tm_sec = 0;
  t->tm_min = 0;
  t->tm_hour = 0;
  t->tm_mday = 1;
  t->tm_mon = 0;  
  return mktime(t);
}

time_t CDate::NextMinuteStart(time_t ttBase)
{
  FN_START;
  return ttBase + (60 - t->tm_sec);
}

time_t CDate::NextHourStart(time_t ttBase)
{
  FN_START;
  return (60 - t->tm_sec) + (60 * (59 - t->tm_min)) + ttBase;
}

time_t CDate::NextDayStart(time_t ttBase)
{
  FN_START;
  return (60 - t->tm_sec) + (60 * (59 - t->tm_min)) + (SECS_PER_HOUR * (23 - t->tm_hour)) + ttBase;
}

time_t CDate::NextWeekStart(time_t ttBase)
{
  FN_START_LITE;
  return this->StartOfWeek(ttBase) + SECS_PER_WEEK;
}

time_t CDate::NextMonthStart(time_t ttBase)
{
  FN_START;
  t->tm_mday = 1;
  if (t->tm_mon == 11)
  {
    t->tm_mon = 0;
    t->tm_year++;
  }
  else
    t->tm_mon++;
    
  return mktime(t);
}

time_t CDate::NextYearStart(time_t ttBase)
{
  FN_START;
  time_t ttYearStart = this->StartOfYear(ttBase);
  t = localtime(&ttYearStart);
  t->tm_year++;
  return mktime(t);
}

// following functions modify internal datestamp if no datestamp is set in the call    
time_t CDate::AddMinutes(long lngAdd, time_t ttBase)
{
  FN_START2_LITE;
  ttBase = ttBase + (60 * lngAdd);
  FN_END;
}

time_t CDate::AddHours(long lngAdd, time_t ttBase)
{
  FN_START2_LITE;
  ttBase = ttBase + (SECS_PER_HOUR * lngAdd);
  FN_END;
}

time_t CDate::AddDays(long lngAdd, time_t ttBase)
{
  FN_START2_LITE;
  ttBase = ttBase + (SECS_PER_DAY * lngAdd);
  FN_END;
}

time_t CDate::AddWeeks(long lngAdd, time_t ttBase)
{
  FN_START2_LITE;
  ttBase = ttBase + (SECS_PER_WEEK * lngAdd);
  FN_END;
}

time_t CDate::AddMonths(long lngAdd, time_t ttBase)
{
  FN_START2;
  long lngAddYears = lngAdd / 12;
  
  lngAdd %= 12;
  
  if ((t->tm_mon + lngAdd) > 11)
  {
    t->tm_mon = (t->tm_mon + lngAdd - 11);
    t->tm_year++;
  }
  else if ((t->tm_mon + lngAdd) < 0)
  {
    t->tm_year--;
    t->tm_mon = 12 + lngAdd;
  }
  else
    t->tm_mon += lngAdd;
  t->tm_year += lngAddYears;
  ttBase = mktime(t);
  FN_END;
}

time_t CDate::AddYears(long lngAdd, time_t ttBase)
{
  FN_START2;
  t->tm_year += lngAdd;
  ttBase = mktime(t);
  FN_END;
}

time_t CDate::GrokString(const CHAR *szDateTimeString, time_t ttFailValue)
{
  time_t ttRet = ttFailValue;
  long lngMatchIDX = -1;
  int sret;
  #define TOP_ITEMS 6
  int a[TOP_ITEMS];
  const CHAR *fmt;
  for (size_t i = 0; i < this->mvDateFormats.size(); i++)
  {
    for (unsigned int j = 0; j < TOP_ITEMS; j++)
      a[j] = 0;
    fmt = this->mvDateFormats[i].Format.c_str();
    sret = sscanf(szDateTimeString, fmt,
      &(a[0]),&(a[1]),&(a[2]),&(a[3]),&(a[4]),&(a[5]),&(a[6]),&(a[7]),&(a[8]),&(a[9]));
    if ((unsigned int)sret == this->mvDateFormats[i].Parts)
    {
      lngMatchIDX = (long)i;
      break;
    }
  }
  #undef TOP_ITEMS
  if (lngMatchIDX > -1)
  {
    // get a starting base (time-only formats will need this)
    time_t ttNow = time(NULL);
    struct tm *mytm = localtime(&ttNow);
    mytm->tm_yday = 999;
    mytm->tm_wday = 999;
    int intYear, intDay;
    switch (lngMatchIDX)
    {
      case 0:        // "%i-%i-%i %i:%i:%i"
      case 1:        // "%i/%i/%i %i:%i:%i"
      case 2:        // "%i-%i-%i %i:%i"
      case 3:        // "%i/%i/%i %i:%i"
      case 4:        // "%i-%i-%i 0%i:%i:%i"
      case 5:        // "%i/%i/%i 0%i:%i:%i"
      case 6:        // "%i-%i-%i 0%i:%i"
      case 7:        // "%i/%i/%i 0%i:%i"
      {
        if (a[0] > 31)
        {
          intYear = a[0];
          intDay = a[2];
        }
        else
        {
          intYear = a[2];
          intDay = a[0];
        }
        if (intYear > 1900)
          intYear -= 1900;
        if (a[1] < 1)
          a[1] = 1;
          
        // reload tm struct
        mytm->tm_year = intYear;
        mytm->tm_mon = a[1] - 1;
        mytm->tm_mday = intDay;
        mytm->tm_hour = a[3];
        mytm->tm_min = a[4];
        mytm->tm_sec = a[5];
        ttRet = mktime(mytm);
        break;
      }
      case 8:         //  "%i-%i-%i"
      case 9:         //  "%i/%i/%i"
      case 10:       //  "%i:%i"
      case 11:        //  "%i:%i:%i"
      case 12:        //  "0%i:%i"
      case 13:        //  "0%i:%i:%i"
      {
        mytm->tm_hour = a[0];
        mytm->tm_min = a[1];
        mytm->tm_sec = a[2];
        ttRet = mktime(mytm);
        break;
      }
      default:
      {
        fprintf(stderr, "Unhandled date format match index %li\n", lngMatchIDX);
      }
    }
  }
  if (ttRet == -1)
    ttRet = ttFailValue;
  return ttRet;
}

const char * CDate::ToString(time_t ttBase, const char *szFormat)
{
	FN_START;
	strftime(this->mszBuf, 128, szFormat, t);
	return (const char *)(this->mszBuf);
}

#undef FN_START
#undef FN_START2
#undef FN_END    
