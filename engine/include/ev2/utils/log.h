#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>
#include <stdint.h>

#define LOG_ENABLE 1

typedef enum 
{
	LOG_NONE_BIT = 0,
	LOG_INFO_BIT = 0x1,
	LOG_WARN_BIT = 0x2,
	LOG_ERROR_BIT = 0x3
} log_flag_bits;
typedef uint32_t log_flags;

typedef enum
{
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_MAX_ENUM
} log_level_t;

typedef void(* log_callback_t)(uint32_t,const char*,...);

extern log_callback_t _log_callback;
extern log_flags _log_flags;

extern void _log_info(const char *file, const char *line, const char* format, ...);
extern void _log_error(const char *file, const char *line, const char* format, ...);
extern void _log_warn(const char *file, const char *line, const char* format, ...);

extern void _log_function(log_level_t lvl, const char *file, int line, const char *format, ...);

extern void log_set_callback(void * usr, log_callback_t callback);
extern void log_set_flags(log_flags flags);
extern log_flags log_get_flags(log_flags flags);

extern void log_set_file(log_level_t lvl, FILE *file);

#if LOG_ENABLE
#define log_info(format, ...)  _log_function(LOG_LEVEL_INFO, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define log_error(format, ...) _log_function(LOG_LEVEL_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define log_warn(format, ...)  _log_function(LOG_LEVEL_WARN, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else 
#define log_info(format, ...);
#define log_error(format, ...);
#define log_warn(format, ...);
#endif

#endif //__LOG_H__
