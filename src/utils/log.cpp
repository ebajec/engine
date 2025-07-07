#include "utils/log.h"

#include <stdarg.h>
#include <stdio.h>

log_callback_t _log_callback{};
log_flags _log_flags = LOG_ERROR_BIT;

void _log_info(const char* format, ...)
{
	va_list args;
    va_start(args, format);   

	if (_log_flags & LOG_INFO_BIT) 
	{
		printf("[INFO] ");
    	vprintf(format, args);      
		printf("\n");
	}
    va_end(args);

	if (_log_callback) _log_callback(LOG_INFO_BIT,format,args);
}

void _log_error(const char* format, ...)
{

	va_list args;
    va_start(args, format);   

	if (_log_flags & LOG_ERROR_BIT) 
	{
		printf("[ERROR] ");
    	vprintf(format, args);      
		printf("\n");
	}
    va_end(args);

	if (_log_callback) _log_callback(LOG_ERROR_BIT,format,args);
}

void _log_warn(const char* format, ...)
{

	va_list args;
    va_start(args, format);   

	if (_log_flags & LOG_WARN_BIT) 
	{
		printf("[WARN] ");
    	vprintf(format, args);      
		printf("\n");
	}
    va_end(args);

	if (_log_callback) _log_callback(LOG_WARN_BIT,format,args);
}

void log_set_callback(void * usr, log_callback_t callback)
{
	_log_callback = callback;
}


void log_set_flags(log_flags flags)
{
	_log_flags = flags;
}

log_flags log_get_flags(log_flags flags)
{
	return _log_flags & flags;
}
