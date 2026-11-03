#include "rtc_clock.h"

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>

namespace rtc_clock {

static constexpr uint8_t PCF8563_ADDR = 0x51;
static constexpr uint8_t REG_SECONDS  = 0x02;

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// Liefert -1, wenn die RTC Spannungsverlust gemeldet hat (VL-Bit) oder nicht antwortet.
static time_t readRtcUtc() {
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(PCF8563_ADDR, (uint8_t)7) != 7) return -1;

    uint8_t r[7];
    for (auto& b : r) b = Wire.read();
    if (r[0] & 0x80) return -1;  // VL: Zeit ungültig

    struct tm t = {};
    t.tm_sec  = bcd2dec(r[0] & 0x7F);
    t.tm_min  = bcd2dec(r[1] & 0x7F);
    t.tm_hour = bcd2dec(r[2] & 0x3F);
    t.tm_mday = bcd2dec(r[3] & 0x3F);
    t.tm_mon  = bcd2dec(r[5] & 0x1F) - 1;
    t.tm_year = bcd2dec(r[6]) + 100;  // 20xx

    // mktime() interpretiert lokal -> kurz auf UTC umschalten
    char* tz = getenv("TZ");
    String saved = tz ? tz : "";
    setenv("TZ", "UTC0", 1); tzset();
    time_t utc = mktime(&t);
    setenv("TZ", saved.c_str(), 1); tzset();
    return utc;
}

static void writeRtcUtc(time_t utc) {
    struct tm t;
    gmtime_r(&utc, &t);
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(dec2bcd(t.tm_sec));   // VL-Bit wird dabei gelöscht
    Wire.write(dec2bcd(t.tm_min));
    Wire.write(dec2bcd(t.tm_hour));
    Wire.write(dec2bcd(t.tm_mday));
    Wire.write(t.tm_wday);
    Wire.write(dec2bcd(t.tm_mon + 1));
    Wire.write(dec2bcd(t.tm_year % 100));
    Wire.endTransmission();
}

static void setSystem(time_t utc) {
    struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
    settimeofday(&tv, nullptr);
}

void begin(const char* posixTz) {
    setenv("TZ", posixTz, 1);
    tzset();
    time_t utc = readRtcUtc();
    if (utc > 0) setSystem(utc);
}

void setEpoch(time_t utc) {
    setSystem(utc);
    writeRtcUtc(utc);
}

struct tm now() {
    time_t t = time(nullptr);
    struct tm local;
    localtime_r(&t, &local);
    return local;
}

bool isValid() { return time(nullptr) > 1700000000; }  // nach Nov 2023

}  // namespace rtc_clock
