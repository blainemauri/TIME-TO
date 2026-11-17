// Calendar arithmetic. Days are counted from 1970-01-01 so that whole-day
// maths never touches time zones; only tt_local_to_epoch / tt_epoch_to_local
// cross between local wall-clock time and UTC seconds.
#include "tt_core.h"

#include <string.h>

const char *const tt_wday_abbr[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char *const tt_wday_code[7] = {"mo", "tu", "we", "th", "fr", "sa", "su"};
const char *const tt_month_abbr[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool tt_is_leap(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int tt_days_in_month(int y, int m) {
  static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) {
    return 30;
  }
  return (m == 2 && tt_is_leap(y)) ? 29 : dim[m - 1];
}

// Howard Hinnant's days_from_civil / civil_from_days.
int32_t tt_days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

TTDate tt_civil_from_days(int32_t z) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = yoe + era * 400;
  const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int32_t mp = (5 * doy + 2) / 153;
  const int32_t d = doy - (153 * mp + 2) / 5 + 1;
  const int32_t m = mp < 10 ? mp + 3 : mp - 9;
  TTDate date = {(int16_t)(y + (m <= 2)), (uint8_t)m, (uint8_t)d};
  return date;
}

int32_t tt_date_to_days(TTDate date) {
  return tt_days_from_civil(date.y, date.m, date.d);
}

int tt_dow(int32_t days) {
  // 1970-01-01 was a Thursday (index 3 with Monday = 0).
  int32_t r = (days + 3) % 7;
  return (int)(r < 0 ? r + 7 : r);
}

bool tt_date_is_set(TTDate date) {
  return date.y != 0;
}

bool tt_date_valid(int y, int m, int d) {
  return y >= 1970 && y <= 2199 && m >= 1 && m <= 12 && d >= 1 && d <= tt_days_in_month(y, m);
}

time_t tt_local_to_epoch(int32_t day, int minute_of_day) {
  TTDate date = tt_civil_from_days(day);
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = date.y - 1900;
  tm.tm_mon = date.m - 1;
  tm.tm_mday = date.d;
  tm.tm_hour = minute_of_day / 60;
  tm.tm_min = minute_of_day % 60;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

void tt_epoch_to_local(time_t t, int32_t *day, int *minute_of_day, int *second) {
  struct tm *tm = localtime(&t);
  if (!tm) {
    if (day) *day = (int32_t)(t / 86400);
    if (minute_of_day) *minute_of_day = (int)((t % 86400) / 60);
    if (second) *second = (int)(t % 60);
    return;
  }
  if (day) *day = tt_days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
  if (minute_of_day) *minute_of_day = tm->tm_hour * 60 + tm->tm_min;
  if (second) *second = tm->tm_sec;
}
