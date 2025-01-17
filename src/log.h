/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <stdio.h>
#if defined(ESP_PLATFORM)
# include <sdkconfig.h>
#endif


#if defined(CONFIG_CWHTTPD_LOG_LEVEL_NONE)
# define LOG_LEVEL LOG_NONE
#elif defined(CONFIG_CWHTTPD_LOG_LEVEL_ERROR)
# define LOG_LEVEL LOG_ERROR
#elif defined(CONFIG_CWHTTPD_LOG_LEVEL_WARN)
# define LOG_LEVEL LOG_WARN
#elif defined(CONFIG_CWHTTPD_LOG_LEVEL_INFO)
# define LOG_LEVEL LOG_INFO
#elif defined(CONFIG_CWHTTPD_LOG_LEVEL_DEBUG)
# define LOG_LEVEL LOG_DEBUG
#elif defined(CONFIG_CWHTTPD_LOG_LEVEL_VERBOSE)
# define LOG_LEVEL LOG_VERBOSE
#else
# define LOG_LEVEL LOG_WARN
#endif

typedef enum {
    LOG_NONE,
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_VERBOSE
} log_level_t;

# define LOG_COLOR_RED     "31"
# define LOG_COLOR_GREEN   "32"
# define LOG_COLOR_BROWN   "33"
# define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
# define LOG_RESET_COLOR   "\033[0m"

# define LOG_COLOR_E       LOG_COLOR(LOG_COLOR_RED)
# define LOG_COLOR_W       LOG_COLOR(LOG_COLOR_BROWN)
# define LOG_COLOR_I       LOG_COLOR(LOG_COLOR_GREEN)
# define LOG_COLOR_D
# define LOG_COLOR_V

# define LOG_FORMAT(letter, format)  LOG_COLOR_ ## letter #letter " %s: " format LOG_RESET_COLOR "\n"

# define LOGE( tag, format, ... )  if (LOG_LEVEL >= LOG_ERROR)   { fprintf(stderr, LOG_FORMAT(E, format), tag, ##__VA_ARGS__); }
# define LOGW( tag, format, ... )  if (LOG_LEVEL >= LOG_WARN)    { fprintf(stderr, LOG_FORMAT(W, format), tag, ##__VA_ARGS__); }
# define LOGI( tag, format, ... )  if (LOG_LEVEL >= LOG_INFO)    { fprintf(stderr, LOG_FORMAT(I, format), tag, ##__VA_ARGS__); }
# define LOGD( tag, format, ... )  if (LOG_LEVEL >= LOG_DEBUG)   { fprintf(stderr, LOG_FORMAT(D, format), tag, ##__VA_ARGS__); }
# define LOGV( tag, format, ... )  if (LOG_LEVEL >= LOG_VERBOSE) { fprintf(stderr, LOG_FORMAT(V, format), tag, ##__VA_ARGS__); }
