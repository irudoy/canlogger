import datetime

now = datetime.datetime.now()

def to_bcd(value):
  return (value // 10) << 4 | (value % 10)

header_content = f"""
#ifndef GENERATED_RTC_TIME_H
#define GENERATED_RTC_TIME_H

#define INIT_RTC_HOURS    0x{to_bcd(now.hour):X}
#define INIT_RTC_MINUTES  0x{to_bcd(now.minute):X}
#define INIT_RTC_SECONDS  0x{to_bcd(now.second):X}
#define INIT_RTC_WEEKDAY  RTC_WEEKDAY_{now.strftime('%A').upper()}
#define INIT_RTC_MONTH    RTC_MONTH_{now.strftime('%B').upper()}
#define INIT_RTC_DATE     0x{to_bcd(now.day):X}
#define INIT_RTC_YEAR     0x{to_bcd(now.year % 100):X}

#endif // GENERATED_RTC_TIME_H
"""

with open('Inc/generated_rtc_time.h', 'w') as f:
  f.write(header_content)
