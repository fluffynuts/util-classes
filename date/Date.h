#ifndef _RELATIVEDATE_H_
#define _RELATIVEDATE_H_

#include <time.h>
#include <stdio.h>
#include <string>
#include <vector>
#ifdef WIN32
#include <windows.h>
#else
#ifndef CHAR
#define CHAR char
#endif
#endif

class CDate
{
  public:
    CDate(void);
    CDate(time_t ttBase);
    ~CDate(void);
    
    time_t Value() {return this->mttBase;};
    void Set(time_t ttNew) {this->mttBase = ttNew;};
    
    time_t StartOfMinute(time_t ttBase = 0);
    time_t StartOfHour(time_t ttBase = 0);
    time_t StartOfDay(time_t ttBase = 0);
    time_t StartOfWeek(time_t ttBase = 0);
    time_t StartOfMonth(time_t ttBase = 0);
    time_t StartOfYear(time_t ttBase = 0);
    
    time_t NextMinuteStart(time_t ttBase = 0);
    time_t NextHourStart(time_t ttBase = 0);
    time_t NextDayStart(time_t ttBase = 0);
    time_t NextWeekStart(time_t ttBase = 0);
    time_t NextMonthStart(time_t ttBase = 0);
    time_t NextYearStart(time_t ttBase = 0);
    
    time_t AddMinutes(long lngAdd, time_t ttBase = 0);
    time_t AddHours(long lngAdd, time_t ttBase = 0);
    time_t AddDays(long lngAdd, time_t ttBase = 0);
    time_t AddWeeks(long lngAdd, time_t ttBase = 0);
    time_t AddMonths(long lngAdd, time_t ttBase = 0);
    time_t AddYears(long lngAdd, time_t ttBase = 0);
    
    time_t GrokString(const CHAR *szDateTimeString, time_t ttFailValue = 0);
		const char *ToString(time_t ttBase = 0, const char *szFormat = "%Y-%m-%d %H:%M:%S");
    
  private:
    time_t mttBase;
		char mszBuf[128];
    struct SDateFormat
    {
      std::string Format;
      unsigned int Parts;
      SDateFormat(std::string strFormat, unsigned int uiParts)
      {
        this->Format = strFormat;
        this->Parts = uiParts;
      }
    };
    std::vector<SDateFormat> mvDateFormats;
};

#endif
