#ifndef _UTIL_H_
#define _UTIL_H_

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

/// Remove compiler warnings when not using a function parameter
#define UNUSED_PARAM(x)		(void)x

#define ByteSwapWord(w)		(uint16_t)((((uint16_t)(w))>>8) | (((uint16_t)(w))<<8))

/// Puts a task to sleep for some milliseconds (requires FreeRTOS).
#define vTaskDelayMs(ms)	vTaskDelay((ms)/portTICK_RATE_MS)

#if !defined(MAX)
/// Returns the maximum of two numbers
#define MAX(a, b)	((a)>(b)?(a):(b))
#endif
#if !defined(MIN)
/// Returns the minimum of two numbers
#define MIN(a, b)	((a)<(b)?(a):(b))
#endif

#ifdef _DEBUG_MSGS
/// Prints message if debug is enabled
#define dprintf(...)	do{printf(__VA_ARGS__);}while(0)
/// Prints a msg using perror, and returns from the caller function.
#define HandleError(msg, ret) \
	do {perror(msg); return(ret);} while(0)
/// The same as above, for functions that do not directly set errno vairable.
#define HandleErrorEn(en, msg, ret) \
	do {errno = en; perror(msg); return(ret);} while(0)
#else
#define dprintf(...)
#define HandleError(msg, ret)
#define HandleErrorEn(en, msg, ret)
#endif

/// Similar to strcpy, but returns a pointer to the last character of the
/// src input string (the ending '\0')
static inline const char *StrCpySrc(char *dst, const char *src) {
	while ('\0' != *src) *dst++ = *src++;
	*dst = '\0';

	return src;
}

/// Similar to strcpy, but returns a pointer to the last character of the
/// dst output string (the ending '\0')
static inline char *StrCpyDst(char *dst, const char *src) {
	while ('\0' != *src) *dst++ = *src++;
	*dst = '\0';

	return dst;
}

#endif //_UTIL_H_

