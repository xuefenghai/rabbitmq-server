#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"
#include "misc1.h"
#include "types.h"
#include "os/os.h"

#define USR_LOG_FILE "$HOME/.nvimlog"


static FILE *open_log_file(void);
static bool do_log_to_file(FILE *log_file, int log_level,
                           const char *func_name, int line_num,
                           const char* fmt, ...);
static bool v_do_log_to_file(FILE *log_file, int log_level,
                             const char *func_name, int line_num,
                             const char* fmt, va_list args);

bool do_log(int log_level, const char *func_name, int line_num,
            const char* fmt, ...)
{
  FILE *log_file = open_log_file();

  if (log_file == NULL) {
    return false;
  }

  va_list args;
  va_start(args, fmt);
  bool ret = v_do_log_to_file(log_file, log_level, func_name, line_num, fmt,
                              args);
  va_end(args);

  if (log_file != stderr && log_file != stdout) {
    fclose(log_file);
  }
  return ret;
}

/// Open the log file for appending.
///
/// @return The FILE* specified by the USR_LOG_FILE path or stderr in case of
///         error
static FILE *open_log_file(void)
{
  static bool opening_log_file = false;

  // check if it's a recursive call
  if (opening_log_file) {
    do_log_to_file(stderr, ERROR_LOG_LEVEL, __func__, __LINE__,
                   "Trying to LOG() recursively! Please fix it.");
    return stderr;
  }

  // expand USR_LOG_FILE and open the file
  FILE *log_file;
  opening_log_file = true;
  {
    static char expanded_log_file_path[MAXPATHL + 1];

    expand_env((char_u *)USR_LOG_FILE, (char_u *)expanded_log_file_path,
               MAXPATHL);
    // if the log file path expansion failed then fall back to stderr
    if (strcmp(USR_LOG_FILE, expanded_log_file_path) == 0) {
      goto open_log_file_error;
    }

    log_file = fopen(expanded_log_file_path, "a");
    if (log_file == NULL) {
      goto open_log_file_error;
    }
  }
  opening_log_file = false;

  return log_file;

open_log_file_error:
  opening_log_file = false;

  do_log_to_file(stderr, ERROR_LOG_LEVEL, __func__, __LINE__,
                 "Couldn't open USR_LOG_FILE, logging to stderr! This may be "
                 "caused by attempting to LOG() before initialization "
                 "functions are called (e.g. init_homedir()).");
  return stderr;
}

static bool do_log_to_file(FILE *log_file, int log_level,
                           const char *func_name, int line_num,
                           const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  bool ret = v_do_log_to_file(log_file, log_level, func_name, line_num, fmt,
                              args);
  va_end(args);

  return ret;
}

static bool v_do_log_to_file(FILE *log_file, int log_level,
                             const char *func_name, int line_num,
                             const char* fmt, va_list args)
{
  static const char *log_levels[] = {
    [DEBUG_LOG_LEVEL] = "debug",
    [INFO_LOG_LEVEL] = "info",
    [WARNING_LOG_LEVEL] = "warning",
    [ERROR_LOG_LEVEL] = "error"
  };
  assert(log_level >= DEBUG_LOG_LEVEL && log_level <= ERROR_LOG_LEVEL);

  // format current timestamp in local time
  struct timeval tv;
  if (gettimeofday(&tv, NULL) < 0) {
    return false;
  }
#ifdef UNIX
  // localtime() is not thread-safe. POSIX provides localtime_r() as a
  // thread-safe version.
  struct tm local_time_allocated;
  struct tm *local_time = localtime_r(&tv.tv_sec, &local_time_allocated);
#else
  // Windows version of localtime() is thread-safe.
  // See http://msdn.microsoft.com/en-us/library/bf12f0hc%28VS.80%29.aspx
  struct tm *local_time = localtime(&tv.tv_sec);  // NOLINT
#endif
  char date_time[20];
  if (strftime(date_time, sizeof(date_time), "%Y/%m/%d %H:%M:%S",
               local_time) == 0) {
    return false;
  }

  // print the log message prefixed by the current timestamp and pid
  int64_t pid = os_get_pid();
  if (fprintf(log_file, "%s [%s @ %s:%d] %" PRId64 " - ", date_time,
              log_levels[log_level], func_name, line_num, pid) < 0) {
    return false;
  }
  if (vfprintf(log_file, fmt, args) < 0) {
    return false;
  }
  fputc('\n', log_file);
  if (fflush(log_file) == EOF) {
    return false;
  }

  return true;
}

