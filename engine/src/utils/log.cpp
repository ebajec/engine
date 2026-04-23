#include <ev2/utils/log.h>
#include <ev2/utils/ansi_colors.h>

#include <stdarg.h>
#include <stdio.h>

log_callback_t _log_callback{};
log_flags _log_flags = LOG_ERROR_BIT;

static FILE * _log_files[LOG_LEVEL_MAX_ENUM] = {
	stdout, 
	stdout,
	stderr
};

void _log_function(log_level_t lvl, const char *file, int, const char *format, ...)
{
	FILE *out = _log_files[lvl];

	switch(lvl) {
		case LOG_LEVEL_INFO:
		if (_log_flags & LOG_INFO_BIT)
			fprintf(out, ANSI_CYAN([INFO])" ");
		break;

		case LOG_LEVEL_WARN:
		if (_log_flags & LOG_WARN_BIT)
			fprintf(out, ANSI_YELLOW([WARN])" ");
		break;

		case LOG_LEVEL_ERROR:
		if (_log_flags & LOG_ERROR_BIT)
			fprintf(out, ANSI_RED([ERROR])" ");
		break;

		case LOG_LEVEL_MAX_ENUM:
			return;
	}

	va_list args;
    va_start(args, format);   
	vfprintf(out, format, args);      
    va_end(args);

	fprintf(out, "\n");

	if (_log_callback) _log_callback(LOG_WARN_BIT,format,args);
}

void log_set_file(log_level_t lvl, FILE *file)
{
	_log_files[lvl] = file;
}

void log_set_callback(void *, log_callback_t callback)
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
