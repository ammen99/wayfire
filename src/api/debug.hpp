#ifndef DEBUG_HPP
#define DEBUG_HPP

#include "config.h"

extern "C"
{
#include  <wlr/util/log.h>
}

const char *wf_strip_path(const char *path);
#define wf_log(verb, fmt, ...) \
    _wlr_log(verb, "[%s:%d] " fmt, wf_strip_path(__FILE__), __LINE__, ##__VA_ARGS__)

#define log_error(...) wf_log(L_ERROR, __VA_ARGS__)
#define log_info(...)  wf_log(L_INFO,  __VA_ARGS__)

#if WAYFIRE_DEBUG_ENABLED
#define log_debug(...) wf_log(L_DEBUG, __VA_ARGS__)
#else
#define log_debug(...)
#endif

#define nonull(x) ((x) ?: ("nil"))

#endif
