#include "Utiles.h"

namespace TimeUtils
{
// 获取当前时间戳
std::time_t getCurrentTimestamp()
{
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

// 创建特定日期的时间戳
std::time_t createTimestamp(int year, int month, int day, int hour , int min , int sec )
{
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    return std::mktime(&tm);
}


bool isTimeAfter(int year, int month, int day)
{
    auto target = createTimestamp(year, month, day);
    auto now = getCurrentTimestamp();
    return now >= target;
}
bool isTimer(long t){
    auto now = getCurrentTimestamp();
    return now >= t;
}
// 时间格式化
std::string formatTime(std::time_t timestamp)
{
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&timestamp));
    return std::string(buffer);
}
} // namespace TimeUtils