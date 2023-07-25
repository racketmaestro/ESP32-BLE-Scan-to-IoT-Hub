#ifndef LIB_REALTIMECLOCK_REALTIMECLOCK_H_
#define LIB_REALTIMECLOCK_REALTIMECLOCK_H_

#include <RTClib.h>  // Include the necessary library for the RTC module

extern RTC_DS3231 rtc;  // Declare the external instance of the RTC_DS3231 class

void setupRealTimeClock();

#endif  // LIB_REALTIMECLOCK_REALTIMECLOCK_H_
