/*
 * log.c - Logging facility.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

/* #define DBGLOGGING */

#include "vice.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archdep.h"
#include "cmdline.h"
#include "lib.h"
#include "log.h"
#include "resources.h"
#include "util.h"

#ifdef __LIBRETRO__
#include <libretro.h>
#include "string/stdstring.h"
extern retro_log_printf_t log_cb;
static char log_buf[1024]; /*create this here in case of tiny stack*/
#endif

#ifdef DBGLOGGING
#define DBG(x) printf x
#else
#define DBG(x)
#endif

static FILE *log_file = NULL;

static char **logs = NULL;
static log_t num_logs = 0;

static int log_enabled = 1; /* cv: this flag allows to temporarly disable all logging */
static int verbose = 0;
static int locked = 0;

/* ------------------------------------------------------------------------- */

static char *log_file_name = NULL;

#ifndef __LIBRETRO__
static void log_file_open(void)
{
    if (log_file_name == NULL || *log_file_name == 0) {
        log_file = archdep_open_default_log_file();
    } else {
#ifndef __OS2__
        if (strcmp(log_file_name, "-") == 0) {
            log_file = stdout;
        } else
#endif
        log_file = fopen(log_file_name, MODE_WRITE_TEXT);
    }
    /* flush all data direct to the output stream. */
    if (log_file) {
        setbuf(log_file, NULL);
    }
}

static int set_log_file_name(const char *val, void *param)
{
    if (locked) {
        return 0;
    }

    if (util_string_set(&log_file_name, val) < 0) {
        return 0;
    }

    if (log_file) {
        fclose(log_file);
        log_file_open();
    }

    return 0;
}

static int log_verbose_opt(const char *param, void *extra_param)
{
    verbose = vice_ptr_to_int(extra_param);
    return 0;
}

static int log_silent_opt(const char *param, void *extra_param)
{
    int silent = vice_ptr_to_int(extra_param);
    log_enabled = ! silent;
    return 0;
}


int log_set_verbose(int n)
{
    if (n) {
        return log_verbose_opt(NULL, (void*)1);
    }
    return log_verbose_opt(NULL, (void*)0);
}


int log_set_silent(int n)
{
    log_enabled = !n;
    return 0;
}



int log_verbose_init(int argc, char **argv)
{
    int i;
    DBG(("log_verbose_init: %d %s\n", argc, argv[0]));
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            DBG(("log_verbose_init: %d %s\n", i, argv[i]));
            if (strcmp("-verbose", argv[i]) == 0) {
                log_set_verbose(1);
                break;
            } else if (strcmp("-silent", argv[1]) == 0) {
                log_enabled = 0;
                break;
            }
        }
    }
    return 0;
}

#ifndef __X1541__
static const resource_string_t resources_string[] = {
    { "LogFileName", "", RES_EVENT_NO, NULL,
      &log_file_name, set_log_file_name, NULL },
    RESOURCE_STRING_LIST_END
};

static int log_logfile_opt(const char *param, void *extra_param)
{
    locked = 0;
    set_log_file_name(param, NULL);
    locked = 1;
    return 0;
}

int log_resources_init(void)
{
    return resources_register_string(resources_string);
}

void log_resources_shutdown(void)
{
    lib_free(log_file_name);
}

static const cmdline_option_t cmdline_options[] =
{
    { "-logfile", CALL_FUNCTION, CMDLINE_ATTRIB_NEED_ARGS,
      log_logfile_opt, NULL, NULL, NULL,
      "<Name>", "Specify log file name" },
    { "-verbose", CALL_FUNCTION, CMDLINE_ATTRIB_NONE,
      log_verbose_opt, (void*)1, NULL, NULL,
      NULL, "Enable verbose log output." },
    { "-silent", CALL_FUNCTION, CMDLINE_ATTRIB_NONE,
      log_silent_opt, (void*)1, NULL, NULL,
      NULL, "Disable verbose log output." },
    CMDLINE_LIST_END
};

int log_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}
#endif

/* ------------------------------------------------------------------------- */

int log_init_with_fd(FILE *f)
{
    if (f == NULL) {
        return -1;
    }

    log_file = f;
    return 0;
}

int log_init(void)
{
#if 0
    /*
     * The current calling order in main.c (log_init() after processing
     * resources) makes this break if anything in the resource set_*
     * functions does a log_open().  On platforms that have no regular
     * stdout (e.g win32) no logging will be seen.  On win32 startup will
     * also be preceeded by a modal error requester.  / tlr
     */
    if (logs != NULL) {
        return -1;
    }
#endif

    log_file_open();

    return (log_file == NULL) ? -1 : 0;
}
#endif /* __LIBRETRO__ */

log_t log_open(const char *id)
{
    log_t new_log = 0;
    log_t i;

    for (i = 0; i < num_logs; i++) {
        if (logs[i] == NULL) {
            new_log = i;
            break;
        }
    }

    if (i == num_logs) {
        new_log = num_logs++;
        logs = lib_realloc(logs, sizeof(*logs) * num_logs);
    }

    logs[new_log] = lib_strdup(id);

    /* printf("log_open(%s) = %d\n", id, (int)new_log); */
    return new_log;
}

int log_close(log_t log)
{
    /* printf("log_close(%d)\n", (int)log); */
    if (logs[(unsigned int)log] == NULL) {
        return -1;
    }

    lib_free(logs[(unsigned int)log]);
    logs[(unsigned int)log] = NULL;

    return 0;
}

void log_close_all(void)
{
    log_t i;

    for (i = 0; i < num_logs; i++) {
        log_close(i);
    }

    lib_free(logs);
    logs = NULL;
}

#ifndef __LIBRETRO__
static int log_archdep(const char *logtxt, const char *fmt, va_list ap)
{
    /*
     * ------ Split into single lines ------
     */
    int rc = 0;

    char *txt = lib_mvsprintf(fmt, ap);

    char *beg = txt;
    char *end = txt + strlen(txt) + 1;

    while (beg < end) {
        char *eol = strchr(beg, '\n');

        if (eol) {
            *eol = '\0';
        }

        if (archdep_default_logger(*beg ? logtxt : "", beg) < 0) {
            rc = -1;
            break;
        }

        if (!eol) {
            break;
        }

        beg = eol + 1;
    }

    lib_free(txt);

    return rc;
}

static int log_helper(log_t log, unsigned int level, const char *format,
                      va_list ap)
{
    static const char * const level_strings[3] = {
        "",
        "Warning - ",
        "Error - "
    };

    const signed int logi = (signed int)log;
    int rc = 0;
    char *logtxt = NULL;

    if (!log_enabled) {
        return 0;
    }

    if ((logi != LOG_DEFAULT) && (logi != LOG_ERR)) {
        if ((logs == NULL) || (logi < 0)|| (logi >= num_logs) || (logs[logi] == NULL)) {
#ifdef DEBUG
            log_archdep("log_helper: internal error (invalid id or closed log), messages follows:\n", format, ap);
#endif
            return -1;
        }
    }

    if ((log_file != NULL) && (logi != LOG_DEFAULT) && (logi != LOG_ERR) && (*logs[logi] != '\0')) {
        logtxt = lib_msprintf("%s: %s", logs[logi], level_strings[level]);
    } else {
        logtxt = lib_msprintf("%s", level_strings[level]);
    }

    if (log_file == NULL) {
        rc = log_archdep(logtxt, format, ap);
    } else {
#ifdef ARCHDEP_EXTRA_LOG_CALL
        log_archdep(logtxt, format, ap);
#endif
        if (fputs(logtxt, log_file) == EOF
            || vfprintf(log_file, format, ap) < 0
            || fputc ('\n', log_file) == EOF) {
            rc = -1;
        }
    }

    lib_free(logtxt);

    return rc;
}

int log_message(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, 0, format, ap);
    va_end(ap);

    return rc;
}

int log_warning(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, 1, format, ap);
    va_end(ap);

    return rc;
}

int log_error(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, 2, format, ap);
    va_end(ap);

    return rc;
}

int log_debug(const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(LOG_DEFAULT, 0, format, ap);
    va_end(ap);

    return rc;
}

int log_verbose(const char *format, ...)
{
    va_list ap;
    int rc = 0;

    va_start(ap, format);
    if (verbose) {
        rc = log_helper(LOG_DEFAULT, 0, format, ap);
    }
    va_end(ap);

    return rc;
}

#else /* __LIBRETRO__ */

static int log_helper(unsigned int level, log_t log, const char *format, va_list ap)
{
    signed int logi = (signed int)log;
    int rc;

    if (!log_enabled) {
        return 0;
    }

    rc = vsprintf(log_buf, format, ap);

    if (rc >= 0)
    {
        string_replace_all_chars(log_buf, '`', '\'');
        if ((num_logs > 0) && (logi != LOG_DEFAULT) && (logi != LOG_ERR) && (logs[logi][0] != '\0')) {
            log_cb(level, "%s: %s\n", logs[logi], log_buf);
        } else {
            log_cb(level, "%s\n", log_buf);
        }
        ++rc;
    }

    return rc;
}

int log_message(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(RETRO_LOG_INFO, log, format, ap);
    va_end(ap);

    return rc;
}

int log_warning(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(RETRO_LOG_WARN, log, format, ap);
    va_end(ap);

    return rc;
}

int log_error(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(RETRO_LOG_ERROR, log, format, ap);
    va_end(ap);

    return rc;
}

int log_debug(const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
#if 0
    vprintf(format, ap);
    printf("\n");
#else
    rc = log_helper(RETRO_LOG_DEBUG, 0, format, ap);
#endif
    va_end(ap);

    return rc;
}

int log_verbose(const char *format, ...)
{
    va_list ap;
    int rc = 0;

    va_start(ap, format);
    if (verbose) {
        log_helper(RETRO_LOG_INFO, 0, format, ap);
    }
    va_end(ap);

    return rc;
}

/* stubs */
int log_resources_init(void) {return 0;}
void log_resources_shutdown(void) {}
int log_cmdline_options_init(void) {return 0;}
int log_init_with_fd(FILE *f) {return 0;}
int log_set_verbose(int n) {verbose=n?1:0;return 0;}
int log_verbose_init(int argc, char **argv) {return 0;}
#endif /* __LIBRETRO__ */

int log_init(void)
{
    return (log_cb == NULL) ? -1 : 0;
}

void log_enable(int on)
{
    log_enabled = on;
}
