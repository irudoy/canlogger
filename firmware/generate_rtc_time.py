import datetime

now = datetime.datetime.now()

header_content = f"""
#ifndef GENERATED_RTC_TIME_H
#define GENERATED_RTC_TIME_H

#define INIT_RTC_HOURS    0x{now.hour:X}
#define INIT_RTC_MINUTES  0x{now.minute:X}
#define INIT_RTC_SECONDS  0x{now.second:X}
#define INIT_RTC_WEEKDAY  RTC_WEEKDAY_{now.strftime('%A').upper()}
#define INIT_RTC_MONTH    RTC_MONTH_{now.strftime('%B').upper()}
#define INIT_RTC_DATE     0x{now.day:X}
#define INIT_RTC_YEAR     0x{(now.year % 100):X}

#endif // GENERATED_RTC_TIME_H
"""

with open('Inc/generated_rtc_time.h', 'w') as f:
  f.write(header_content)
