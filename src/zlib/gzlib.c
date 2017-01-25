/* gzlib.c -- zlib functions common to reading and writing gzip files
 * Copyright (C) 2004, 2010, 2011, 2012, 2013, 2016 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "gzguts.h"

#if defined(_WIN32) && !defined(__BORLANDC__) && !defined(__MINGW32__)
#define LSEEK _lseeki64
#else
#if defined(_LARGEFILE64_SOURCE) && _LFS64_LARGEFILE-0
#define LSEEK lseek64
#else
#define LSEEK lseek
#endif
#endif

/* Local functions */
local void gz_reset OF((gz_statep));
local gzFile gz_open OF((const void *, const char *));

/* Reset gzip file state */
local void gz_reset(state)
    gz_statep state;
{
    state->x.have = 0;              /* no output data available */
    if (state->mode == GZ_READ) {   /* for reading ... */
        state->eof = 0;             /* not at end of file */
        state->past = 0;            /* have not read past end yet */
        state->how = LOOK;          /* look for gzip header */
    }
    state->seek = 0;                /* no seek request pending */
    gz_error(state, Z_OK, NULL);    /* clear error */
    state->x.pos = 0;               /* no uncompressed data yet */
    state->strm.avail_in = 0;       /* no input data yet */
}

/* Open a gzip file either by name or file descriptor. */
local gzFile gz_open(path, mode)
    const void *path;
    const char *mode;
{
    gz_statep state;
    size_t len;
    int oflag;
#ifdef O_CLOEXEC
    int cloexec = 0;
#endif
#ifdef O_EXCL
    int exclusive = 0;
#endif

    /* check input */
    if (path == NULL)
        return NULL;

    /* allocate gzFile structure to return */
    state = (gz_statep)malloc(sizeof(gz_state));
    if (state == NULL)
        return NULL;
    state->size = 0;            /* no buffers allocated yet */
    state->want = GZBUFSIZE;    /* requested buffer size */
    state->msg = NULL;          /* no error message yet */

    /* interpret mode */
    state->mode = GZ_NONE;
    state->level = Z_DEFAULT_COMPRESSION;
    state->strategy = Z_DEFAULT_STRATEGY;
    state->direct = 0;
    while (*mode) {
        if (*mode >= '0' && *mode <= '9')
            state->level = *mode - '0';
        else
            switch (*mode) {
            case 'r':
                state->mode = GZ_READ;
                break;
            case '+':       /* can't read and write at the same time */
                free(state);
                return NULL;
            case 'b':       /* ignore -- will request binary anyway */
                break;
#ifdef O_CLOEXEC
            case 'e':
                cloexec = 1;
                break;
#endif
#ifdef O_EXCL
            case 'x':
                exclusive = 1;
                break;
#endif
            case 'f':
                state->strategy = Z_FILTERED;
                break;
            case 'h':
                state->strategy = Z_HUFFMAN_ONLY;
                break;
            case 'R':
                state->strategy = Z_RLE;
                break;
            case 'F':
                state->strategy = Z_FIXED;
                break;
            case 'T':
                state->direct = 1;
                break;
            default:        /* could consider as an error, but just ignore */
                ;
            }
        mode++;
    }

    /* must provide an "r", "w", or "a" */
    if (state->mode == GZ_NONE) {
        free(state);
        return NULL;
    }

    /* can't force transparent read */
    if (state->mode == GZ_READ) {
        if (state->direct) {
            free(state);
            return NULL;
        }
        state->direct = 1;      /* for empty file */
    }

    /* save the path name for error messages */
    len = strlen((const char *)path);
    state->path = (char *)malloc(len + 1);
    if (state->path == NULL) {
        free(state);
        return NULL;
    }

#if !defined(NO_snprintf) && !defined(NO_vsnprintf)
        snprintf(state->path, len + 1, "%s", (const char *)path);
#else
        strcpy(state->path, path);
#endif

    /* compute the flags for open() */
    oflag =
#ifdef O_LARGEFILE
        O_LARGEFILE |
#endif
#ifdef O_BINARY
        O_BINARY |
#endif
#ifdef O_CLOEXEC
        (cloexec ? O_CLOEXEC : 0) |
#endif
        (state->mode == GZ_READ ?
         O_RDONLY :
         (O_WRONLY | O_CREAT |
#ifdef O_EXCL
          (exclusive ? O_EXCL : 0) |
#endif
          (state->mode == GZ_WRITE ?
           O_TRUNC :
           O_APPEND)));

    /* open the file with the appropriate flags (or just use fd) */
    state->fd = open((const char *)path, oflag, 0666);
    if (state->fd == -1) {
        free(state->path);
        free(state);
        return NULL;
    }
    if (state->mode == GZ_APPEND)
        state->mode = GZ_WRITE;         /* simplify later checks */

    /* save the current position for rewinding (only if reading) */
    if (state->mode == GZ_READ) {
        state->start = LSEEK(state->fd, 0, SEEK_CUR);
        if (state->start == -1) state->start = 0;
    }

    /* initialize stream */
    gz_reset(state);

    /* return stream */
    return (gzFile)state;
}

/* -- see zlib.h -- */
gzFile ZEXPORT gzopen(path, mode)
    const char *path;
    const char *mode;
{
    return gz_open(path, mode);
}

/* -- see zlib.h -- */
int gzeof(gzFile file)
{
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return 0;
    state = (gz_statep)file;
    if (state->mode != GZ_READ && state->mode != GZ_WRITE)
        return 0;

    /* return end-of-file state */
    return state->mode == GZ_READ ? state->past : 0;
}

/* Create an error message in allocated memory and set state->err and
   state->msg accordingly.  Free any previous error message already there.  Do
   not try to free or allocate space if the error is Z_MEM_ERROR (out of
   memory).  Simply save the error message as a static string.  If there is an
   allocation failure constructing the error message, then convert the error to
   out of memory. */
void ZLIB_INTERNAL gz_error(state, err, msg)
    gz_statep state;
    int err;
    const char *msg;
{
    /* free previously allocated message and clear */
    if (state->msg) {
        if (state->err != Z_MEM_ERROR)
            free(state->msg);
        state->msg = NULL;
    }

    /* if fatal, set state->x.have to 0 so that the gzgetc() macro fails */
    if (err != Z_OK && err != Z_BUF_ERROR)
        state->x.have = 0;

    /* set error code, and if no message, then done */
    state->err = err;
    if (!msg)
        return;

    /* for an out of memory error, return literal string when requested */
    if (err == Z_MEM_ERROR)
        return;

    /* construct error message with path */
    if ((state->msg = (char *)malloc(strlen(state->path) + strlen(msg) + 3)) ==
            NULL) {
        state->err = Z_MEM_ERROR;
        return;
    }
    snprintf(state->msg, strlen(state->path) + strlen(msg) + 3,
             "%s%s%s", state->path, ": ", msg);
}
