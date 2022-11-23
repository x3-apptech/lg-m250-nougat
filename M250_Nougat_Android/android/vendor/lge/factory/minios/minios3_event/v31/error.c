/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "config.h"

/* Simple error handling in SDL */

#include "error.h"
#include "error_c.h"

/* Routine to get the thread-specific error variable */
#if THREADS_DISABLED
/* The  arraysize(The ),default (non-thread-safe) global error variable */
static error_t global_error;
#define GetErrBuf()    (&global_error)
#else
extern error_t *GetErrBuf(void);
#endif /* THREADS_DISABLED */

#define ERRBUFIZE    1024

/* Private functions */

static const char *LookupString(const char *key)
{
    /* FIXME: Add code to lookup key in language string hash-table */
    return key;
}

/* Public functions */
/*
void SetError (const char *fmt, ...)
{
    va_list ap;
    error_t *error;

    // Copy in the key, mark error as valid
    error = GetErrBuf();
    error->error = 1;
    strlcpy((char *)error->key, fmt, sizeof(error->key));

    va_start(ap, fmt);
    error->argc = 0;
    while ( *fmt ) {
        if ( *fmt++ == '%' ) {
            while ( *fmt == '.' || (*fmt >= '0' && *fmt <= '9') ) {
                ++fmt;
            }
            switch (*fmt++) {
                case 0:  // Malformed format string..
                --fmt;
                break;
                case 'c':
                case 'i':
                case 'd':
                case 'u':
                case 'o':
                case 'x':
                case 'X':
                error->args[error->argc++].value_i =
                            va_arg(ap, int);
                break;
                case 'f':
                error->args[error->argc++].value_f =
                            va_arg(ap, double);
                break;
                case 'p':
                error->args[error->argc++].value_ptr =
                            va_arg(ap, void *);
                break;
                case 's':
                {
                  int i = error->argc;
                  const char *str = va_arg(ap, const char *);
                  if (str == NULL)
                      str = "(null)";
                  strlcpy((char *)error->args[i].buf, str, ERR_MAX_STRLEN);
                  error->argc++;
                }
                break;
                default:
                break;
            }
            if ( error->argc >= ERR_MAX_ARGS ) {
                break;
            }
        }
    }
    va_end(ap);

    // If we are in debug mode, print out an error message
#ifdef DEBUG_ERROR
    fprintf(stderr, "SetError: %s\n", GetError());
#endif
} */

/* This function has a bit more overhead than most error functions
   so that it supports internationalization and thread-safe errors.
*/
char *GetErrorMsg(char *errstr, unsigned int maxlen)
{
    error_t *error;

    /* Clear the error string */
    *errstr = '\0'; --maxlen;

    /* Get the thread-safe error, and print it out */
    error = GetErrBuf();
    if ( error->error ) {
        const char *fmt;
        char *msg = errstr;
        int len;
        int argi;

        fmt = LookupString(error->key);
        argi = 0;
        while ( *fmt && (maxlen > 0) ) {
            if ( *fmt == '%' ) {
                char tmp[32], *spot = tmp;
                *spot++ = *fmt++;
                while ( (*fmt == '.' || (*fmt >= '0' && *fmt <= '9')) && spot < (tmp+arraysize(tmp)-2) ) {
                    *spot++ = *fmt++;
                }
                *spot++ = *fmt++;
                *spot++ = '\0';
                switch (spot[-2]) {
                    case '%':
                    *msg++ = '%';
                    maxlen -= 1;
                    break;
                    case 'c':
                    case 'i':
                        case 'd':
                        case 'u':
                        case 'o':
                    case 'x':
                    case 'X':
                    len = snprintf(msg, maxlen, tmp, error->args[argi++].value_i);
                    msg += len;
                    maxlen -= len;
                    break;
                    case 'f':
                    len = snprintf(msg, maxlen, tmp, error->args[argi++].value_f);
                    msg += len;
                    maxlen -= len;
                    break;
                    case 'p':
                    len = snprintf(msg, maxlen, tmp, error->args[argi++].value_ptr);
                    msg += len;
                    maxlen -= len;
                    break;
                    case 's':
                    len = snprintf(msg, maxlen, tmp, LookupString(error->args[argi++].buf));
                    msg += len;
                    maxlen -= len;
                    break;
                }
            } else {
                *msg++ = *fmt++;
                maxlen -= 1;
            }
        }
        *msg = 0;    /* NULL terminate the string */
    }
    return(errstr);
}

/* Available for backwards compatibility */
char *GetError (void)
{
    static char errmsg[ERRBUFIZE];

    return((char *)GetErrorMsg(errmsg, ERRBUFIZE));
}

void ClearError(void)
{
    error_t *error;

    error = GetErrBuf();
    error->error = 0;
}

/* Very common errors go here */
void Error(errorcode code)
{
    switch (code) {
        case EENOMEM:
            SetError("Out of memory");
            break;
        case EFREAD:
            SetError("Error reading from datastream");
            break;
        case EFWRITE:
            SetError("Error writing to datastream");
            break;
        case EFSEEK:
            SetError("Error seeking in datastream");
            break;
        default:
            SetError("Unknown SDL error");
            break;
    }
}

#ifdef TEST_ERROR
int main(int argc, char *argv[])
{
    char buffer[BUFSIZ+1];

    SetError("Hi there!");
    printf("Error 1: %s\n", GetError());
    ClearError();
    memset(buffer, '1', BUFSIZ);
    buffer[BUFSIZ] = 0;
    SetError("This is the error: %s (%f)", buffer, 1.0);
    printf("Error 2: %s\n", GetError());
    exit(0);
}
#endif
