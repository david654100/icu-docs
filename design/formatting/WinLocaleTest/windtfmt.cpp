/*
********************************************************************************
*   Copyright (C) 2005-2006, International Business Machines
*   Corporation and others.  All Rights Reserved.
********************************************************************************
*
* File WINDTFMT.CPP
*
********************************************************************************
*/

#include "unicode/utypes.h"

#ifdef U_WINDOWS

#if !UCONFIG_NO_FORMATTING

#include <iostream>
#include <tchar.h>
#include <windows.h>

#include "windtfmt.h"
#include "win32tz.h"

#include "unicode/ures.h"
#include "unicode/format.h"
#include "unicode/fmtable.h"
#include "unicode/datefmt.h"
#include "unicode/msgfmt.h"
#include "unicode/calendar.h"
#include "unicode/gregocal.h"
#include "unicode/locid.h"
#include "unicode/unistr.h"
#include "unicode/ustring.h"
#include "unicode/timezone.h"
#include "unicode/utmscale.h"

#include "uresimp.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(Win32DateFormat)

#define ARRAY_SIZE(array) (sizeof array / sizeof array[0])

#define STACK_BUFFER_SIZE 64

UnicodeString *getTimeDateFormat(const Calendar *cal, const Locale *locale, UErrorCode &status)
{
    UnicodeString *result = NULL;
    const char *type = cal->getType();
    const char *base = locale->getBaseName();
    UResourceBundle *topBundle = ures_open((char *) 0, base, &status);
    UResourceBundle *calBundle = ures_getByKey(topBundle, "calendar", NULL, &status);
    UResourceBundle *typBundle = ures_getByKeyWithFallback(calBundle, type, NULL, &status);
    UResourceBundle *patBundle = ures_getByKeyWithFallback(typBundle, "DateTimePatterns", NULL, &status);

    if (status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        typBundle = ures_getByKeyWithFallback(calBundle, "gregorian", typBundle, &status);
        patBundle = ures_getByKeyWithFallback(typBundle, "DateTimePatterns", patBundle, &status);
    }

    if (U_FAILURE(status)) {
        UChar defaultPattern[] = {0x007B, 0x0031, 0x007D, 0x0020, 0x007B, 0x0030, 0x007D, 0x0000}; // "{1} {0}"
        return new UnicodeString(defaultPattern, ARRAY_SIZE(defaultPattern));
    }

    int32_t resStrLen = 0;
    const UChar *resStr = ures_getStringByIndex(patBundle, (int32_t)DateFormat::kDateTime, &resStrLen, &status);

    result = new UnicodeString(TRUE, resStr, resStrLen);

    ures_close(patBundle);
    ures_close(typBundle);
    ures_close(calBundle);
    ures_close(topBundle);

    return result;
}

// TODO: Range-check timeStyle, dateStyle
Win32DateFormat::Win32DateFormat(DateFormat::EStyle timeStyle, DateFormat::EStyle dateStyle, const Locale &locale, UErrorCode &status)
  : DateFormat(), fDateTimeMsg(NULL), fTimeStyle(timeStyle), fDateStyle(dateStyle), fLocale(&locale), fZoneID(NULL)
{
    if (!U_FAILURE(status)) {
        fLCID = locale.getLCID();
        adoptCalendar(Calendar::createInstance(locale, status));
    }
}

Win32DateFormat::Win32DateFormat(const Win32DateFormat &other)
  : DateFormat(other)
{
    *this = other;
}

Win32DateFormat::~Win32DateFormat()
{
    delete fCalendar;
    delete fDateTimeMsg;
}

Win32DateFormat &Win32DateFormat::operator=(const Win32DateFormat &other)
{
    DateFormat::operator=(other);

    delete fCalendar;

    this->fDateTimeMsg = other.fDateTimeMsg;
    this->fTimeStyle   = other.fTimeStyle;
    this->fDateStyle   = other.fDateStyle;
    this->fLCID        = other.fLCID;
    this->fCalendar    = other.fCalendar->clone();
    this->fZoneID      = other.fZoneID;

    return *this;
}

Format *Win32DateFormat::clone(void) const
{
    return new Win32DateFormat(*this);
}

// TODO: Is just ignoring pos the right thing?
UnicodeString &Win32DateFormat::format(Calendar &cal, UnicodeString &appendTo, FieldPosition &pos) const
{
    FILETIME ft;
    SYSTEMTIME st_gmt;
    SYSTEMTIME st_local;
    UnicodeString zoneID;
    TIME_ZONE_INFORMATION tzi = fTZI;
    UErrorCode status = U_ZERO_ERROR;
    const TimeZone &tz = cal.getTimeZone();
    int64_t uct, uft;

    tz.getID(zoneID);

    if (zoneID.compare(fZoneID) != 0) {
        Win32TimeZone::getWindowsTimeZoneInfo(&tzi, &tz);
    }

    uct = utmscale_fromInt64((int64_t) cal.getTime(status), UDTS_ICU4C_TIME, &status);
    uft = utmscale_toInt64(uct, UDTS_WINDOWS_FILE_TIME, &status);

    ft.dwLowDateTime =  (DWORD) (uft & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD) ((uft >> 32) & 0xFFFFFFFF);

    FileTimeToSystemTime(&ft, &st_gmt);
    SystemTimeToTzSpecificLocalTime(&tzi, &st_gmt, &st_local);


    if (fDateStyle != DateFormat::kNone && fTimeStyle != DateFormat::kNone) {
        UnicodeString *date = new UnicodeString();
        UnicodeString *time = new UnicodeString();
        UnicodeString *pattern = fDateTimeMsg;
        Formattable timeDateArray[2];

        formatDate(&st_local, *date);
        formatTime(&st_local, *time);

        timeDateArray[0].adoptString(time);
        timeDateArray[1].adoptString(date);

        if (strcmp(fCalendar->getType(), cal.getType()) != 0) {
            pattern = getTimeDateFormat(&cal, fLocale, status);
        }

        MessageFormat::format(*pattern, timeDateArray, 2, appendTo, status);
    } else if (fDateStyle != DateFormat::kNone) {
        formatDate(&st_local, appendTo);
    } else if (fDateStyle != DateFormat::kNone) {
        formatTime(&st_local, appendTo);
    }

    return appendTo;
}

void Win32DateFormat::parse(const UnicodeString& text, Calendar& cal, ParsePosition& pos) const
{
    pos.setErrorIndex(pos.getIndex());
}

void Win32DateFormat::adoptCalendar(Calendar *newCalendar)
{
    if (fCalendar == NULL || strcmp(fCalendar->getType(), newCalendar->getType()) != 0) {
        UErrorCode status = U_ZERO_ERROR;

        if (fDateStyle != DateFormat::kNone && fTimeStyle != DateFormat::kNone) {
            delete fDateTimeMsg;
            fDateTimeMsg = getTimeDateFormat(newCalendar, fLocale, status);
        }
    }

    delete fCalendar;
    fCalendar = newCalendar;

    setTimeZoneInfo(fCalendar->getTimeZone());
}

void Win32DateFormat::setCalendar(const Calendar &newCalendar)
{
    adoptCalendar(newCalendar.clone());
}

void Win32DateFormat::adoptTimeZone(TimeZone *zoneToAdopt)
{
    setTimeZoneInfo(*zoneToAdopt);
    fCalendar->adoptTimeZone(zoneToAdopt);
}

void Win32DateFormat::setTimeZone(const TimeZone& zone)
{
    setTimeZoneInfo(zone);
    fCalendar->setTimeZone(zone);
}

static DWORD dfFlags[] = {DATE_LONGDATE, DATE_LONGDATE, DATE_SHORTDATE, DATE_SHORTDATE};

void Win32DateFormat::formatDate(const SYSTEMTIME *st, UnicodeString &appendTo) const
{
    int result;
    UChar stackBuffer[STACK_BUFFER_SIZE];
    UChar *buffer = stackBuffer;

    result = GetDateFormatW(fLCID, dfFlags[fDateStyle], st, NULL, buffer, STACK_BUFFER_SIZE);

    if (result == 0) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            int newLength = GetDateFormatW(fLCID, dfFlags[fDateStyle], st, NULL, NULL, 0);

            buffer = new UChar[newLength];
            GetDateFormatW(fLCID, dfFlags[fDateStyle], st, NULL, buffer, newLength);
        }
    }

    appendTo.append(buffer, (int32_t) wcslen(buffer));

    if (buffer != stackBuffer) {
        delete[] buffer;
    }
}

static DWORD tfFlags[] = {0, 0, 0, TIME_NOSECONDS};

void Win32DateFormat::formatTime(const SYSTEMTIME *st, UnicodeString &appendTo) const
{
    int result;
    UChar stackBuffer[STACK_BUFFER_SIZE];
    UChar *buffer = stackBuffer;

    result = GetTimeFormatW(fLCID, tfFlags[fTimeStyle], st, NULL, buffer, STACK_BUFFER_SIZE);

    if (result == 0) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            int newLength = GetTimeFormatW(fLCID, tfFlags[fTimeStyle], st, NULL, NULL, 0);

            buffer = new UChar[newLength];
            GetDateFormatW(fLCID, tfFlags[fTimeStyle], st, NULL, buffer, newLength);
        }
    }

    appendTo.append(buffer, (int32_t) wcslen(buffer));

    if (buffer != stackBuffer) {
        delete[] buffer;
    }
}

void Win32DateFormat::setTimeZoneInfo(const TimeZone &zone)
{
    UnicodeString zoneID;

    zone.getID(zoneID);

    if (zoneID.compare(fZoneID) != 0) {
        fZoneID = zoneID;
        Win32TimeZone::getWindowsTimeZoneInfo(&fTZI, &zone);
    }
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif // #ifdef U_WINDOWS

