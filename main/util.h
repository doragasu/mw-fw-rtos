#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>
#include <stdio.h>
#include <esp_log.h>

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

/// Swaps bytes from a word (16 bits)
#define ByteSwapWord(w)		(uint16_t)((((uint16_t)(w))>>8) | (((uint16_t)(w))<<8))

/// Swaps bytes from a dword (32 bits)
#define ByteSwapDWord(dw)	(uint32_t)((((uint32_t)(dw))>>24) |               \
		((((uint32_t)(dw))>>8) & 0xFF00) | ((((uint32_t)(dw)) & 0xFF00)<<8) | \
	  	(((uint32_t)(dw))<<24))

/// Swaps bytes from a qword (64 bits)
#define ByteSwapQWord(qw)	(uint64_t)((((uint64_t)(qw))>>56) |                \
	((((uint64_t)(qw))>>40) & 0xFF00) | ((((uint64_t)(qw))>>24) & 0xFF0000) | \
    ((((uint64_t)(qw))>>8) & 0xFF000000) |                                    \
    ((((uint64_t)(qw)) & 0xFF000000)<<8) |                                    \
    ((((uint64_t)(qw)) & 0xFF0000)<<24)  |                                    \
    ((((uint64_t)(qw)) & 0xFF00)<< 40)   | (((uint64_t)(qw))<<56))

/// Puts a task to sleep for some milliseconds (requires FreeRTOS).
#define vTaskDelayMs(ms)	vTaskDelay((ms)/portTICK_PERIOD_MS)

#if !defined(MAX)
/// Returns the maximum of two numbers
#define MAX(a, b)	((a)>(b)?(a):(b))
#endif
#if !defined(MIN)
/// Returns the minimum of two numbers
#define MIN(a, b)	((a)<(b)?(a):(b))
#endif

#ifdef _DEBUG_MSGS
#define LOGE(...) ESP_LOGE(__func__, __VA_ARGS__)
#define LOGD(...) ESP_LOGD(__func__, __VA_ARGS__)
#define LOGI(...) ESP_LOGI(__func__, __VA_ARGS__)
#define LOGW(...) ESP_LOGW(__func__, __VA_ARGS__)
/// Prints a msg using perror, and returns from the caller function.
#define HandleError(msg, ret) \
	do {LOGE("%s", strerror(errno)); return(ret);} while(0)
/// The same as above, for functions that do not directly set errno vairable.
#define HandleErrorEn(en, msg, ret) \
	do {LOGE("%s", strerror(en)); return(ret);} while(0)
#else
#define LOGE(...)
#define LOGD(...)
#define LOGI(...)
#define LOGW(...)
#define HandleError(msg, ret)
#define HandleErrorEn(en, msg, ret)
#endif


static inline void md5_to_str(const uint8_t hex[16], char str[33])
{
	int i, j;
	const char digits[] = "0123456789abcdef";

	for (i = 0, j = 0; i < 16; i++) {
		str[j++] = digits[hex[i]>>4];
		str[j++] = digits[hex[i] & 0xF];
	}

	str[j] = '\0';
}

static inline int ipv4_to_str(uint32_t ipv4, char str[14])
{
	return sprintf(str, "%d.%d.%d.%d", 0xFF & ipv4, 0xFF & (ipv4>>8),
		    0xFF & (ipv4>>16), ipv4>>24);
}

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

