/* mz_strm_libcomp.c -- Stream for apple compression
   Version 2.6.0, October 8, 2018
   part of the MiniZip project

   Copyright (C) 2010-2018 Nathan Moinvaziri
      https://github.com/nmoinvaz/minizip

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.
*/


#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <compression.h>

#include "zlib.h"

#include "mz.h"
#include "mz_strm.h"
#include "mz_strm_libcomp.h"

/***************************************************************************/

static mz_stream_vtbl mz_stream_libcomp_vtbl = {
    mz_stream_libcomp_open,
    mz_stream_libcomp_is_open,
    mz_stream_libcomp_read,
    mz_stream_libcomp_write,
    mz_stream_libcomp_tell,
    mz_stream_libcomp_seek,
    mz_stream_libcomp_close,
    mz_stream_libcomp_error,
    mz_stream_libcomp_create,
    mz_stream_libcomp_delete,
    mz_stream_libcomp_get_prop_int64,
    mz_stream_libcomp_set_prop_int64
};

/***************************************************************************/

typedef struct mz_stream_libcomp_s {
    mz_stream   stream;
    compression_stream
                cstream;
    uint8_t     buffer[INT16_MAX];
    int32_t     buffer_len;
    int64_t     total_in;
    int64_t     total_out;
    int64_t     max_total_in;
    int8_t      initialized;
    int32_t     mode;
    int32_t     error;
    int16_t     algorithm;
} mz_stream_libcomp;

/***************************************************************************/

int32_t mz_stream_libcomp_open(void *stream, const char *path, int32_t mode)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    int32_t err = 0;
    int16_t operation = 0;
    
    MZ_UNUSED(path);

    if (libcomp->algorithm == 0)
        return MZ_PARAM_ERROR;

    libcomp->total_in = 0;
    libcomp->total_out = 0;

    if (mode & MZ_OPEN_MODE_WRITE)
    {
#ifdef MZ_ZIP_NO_COMPRESSION
        return MZ_SUPPORT_ERROR;
#else
        operation = COMPRESSION_STREAM_ENCODE;
#endif
    }
    else if (mode & MZ_OPEN_MODE_READ)
    {
#ifdef MZ_ZIP_NO_DECOMPRESSION
        return MZ_SUPPORT_ERROR;
#else
        operation = COMPRESSION_STREAM_DECODE;
#endif
    }
    
    err = compression_stream_init(&libcomp->cstream, (compression_stream_operation)operation,
        (compression_algorithm)libcomp->algorithm);
    
    if (err == COMPRESSION_STATUS_ERROR)
    {
        libcomp->error = err;
        return MZ_STREAM_ERROR;
    }
    
    libcomp->initialized = 1;
    libcomp->mode = mode;
    return MZ_OK;
}

int32_t mz_stream_libcomp_is_open(void *stream)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    if (libcomp->initialized != 1)
        return MZ_STREAM_ERROR;
    return MZ_OK;
}

int32_t mz_stream_libcomp_read(void *stream, void *buf, int32_t size)
{
#ifdef MZ_ZIP_NO_DECOMPRESSION
    return MZ_SUPPORT_ERROR;
#else
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    uint64_t total_in_before = 0;
    uint64_t total_in_after = 0;
    uint64_t total_out_before = 0;
    uint64_t total_out_after = 0;
    int32_t total_in = 0;
    int32_t total_out = 0;
    int32_t in_bytes = 0;
    int32_t out_bytes = 0;
    int32_t bytes_to_read = 0;
    int32_t read = 0;
    int32_t err = Z_OK;


    libcomp->cstream.dst_ptr = buf;
    libcomp->cstream.dst_size = (size_t)size;

    do
    {
        if (libcomp->cstream.src_size == 0)
        {
            bytes_to_read = sizeof(libcomp->buffer);
            if (libcomp->max_total_in > 0)
            {
                if ((libcomp->max_total_in - libcomp->total_in) < (int64_t)sizeof(libcomp->buffer))
                    bytes_to_read = (int32_t)(libcomp->max_total_in - libcomp->total_in);
            }

            read = mz_stream_read(libcomp->stream.base, libcomp->buffer, bytes_to_read);

            if (read < 0)
            {
                libcomp->error = read;
                break;
            }
            if (read == 0)
                break;

            libcomp->cstream.src_ptr = libcomp->buffer;
            libcomp->cstream.src_size = (size_t)read;
        }

        total_in_before = libcomp->cstream.src_size;
        total_out_before = libcomp->cstream.dst_size;

        err = compression_stream_process(&libcomp->cstream, 0);
        if (err == COMPRESSION_STATUS_ERROR)
        {
            libcomp->error = err;
            break;
        }

        total_in_after = libcomp->cstream.src_size;
        total_out_after = libcomp->cstream.dst_size;

        in_bytes = (int32_t)(total_in_before - total_in_after);
        out_bytes = (int32_t)(total_out_before - total_out_after);

        total_in += in_bytes;
        total_out += out_bytes;

        libcomp->total_in += in_bytes;
        libcomp->total_out += out_bytes;

        if (err == COMPRESSION_STATUS_END)
            break;

        if (err != COMPRESSION_STATUS_OK)
        {
            libcomp->error = err;
            break;
        }
    }
    while (libcomp->cstream.dst_size > 0);

    if (libcomp->error != 0)
        return libcomp->error;

    return total_out;
#endif
}

static int32_t mz_stream_libcomp_flush(void *stream)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    if (mz_stream_write(libcomp->stream.base, libcomp->buffer, libcomp->buffer_len) != libcomp->buffer_len)
        return MZ_STREAM_ERROR;
    return MZ_OK;
}

static int32_t mz_stream_libcomp_deflate(void *stream, int flush)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    uint64_t total_out_before = 0;
    uint64_t total_out_after = 0;
    uint32_t out_bytes = 0;
    int32_t err = Z_OK;


    do
    {
        if (libcomp->cstream.dst_size == 0)
        {
            err = mz_stream_libcomp_flush(libcomp);
            if (err != MZ_OK)
            {
                libcomp->error = err;
                return MZ_STREAM_ERROR;
            }

            libcomp->cstream.dst_size = sizeof(libcomp->buffer);
            libcomp->cstream.dst_ptr = libcomp->buffer;

            libcomp->buffer_len = 0;
        }

        total_out_before = libcomp->cstream.dst_size;
        err = compression_stream_process(&libcomp->cstream, flush);
        total_out_after = libcomp->cstream.dst_size;

        out_bytes = (uint32_t)(total_out_before - total_out_after);

        libcomp->buffer_len += out_bytes;
        libcomp->total_out += out_bytes;

        if (err == COMPRESSION_STATUS_END)
            break;
        if (err != COMPRESSION_STATUS_OK)
        {
            libcomp->error = err;
            return MZ_STREAM_ERROR;
        }
    }
    while ((libcomp->cstream.src_size > 0) || (flush == COMPRESSION_STREAM_FINALIZE && err == COMPRESSION_STATUS_OK));

    return MZ_OK;
}

int32_t mz_stream_libcomp_write(void *stream, const void *buf, int32_t size)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    int32_t err = size;

#ifdef MZ_ZIP_NO_COMPRESSION
    MZ_UNUSED(libcomp);
    err = MZ_SUPPORT_ERROR;
#else
    libcomp->cstream.src_ptr = buf;
    libcomp->cstream.src_size = (size_t)size;

    mz_stream_libcomp_deflate(stream, 0);

    libcomp->total_in += size;
#endif
    return err;
}

int64_t mz_stream_libcomp_tell(void *stream)
{
    MZ_UNUSED(stream);

    return MZ_STREAM_ERROR;
}

int32_t mz_stream_libcomp_seek(void *stream, int64_t offset, int32_t origin)
{
    MZ_UNUSED(stream);
    MZ_UNUSED(offset);
    MZ_UNUSED(origin);

    return MZ_STREAM_ERROR;
}

int32_t mz_stream_libcomp_close(void *stream)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;


    if (libcomp->mode & MZ_OPEN_MODE_WRITE)
    {
#ifdef MZ_ZIP_NO_COMPRESSION
        return MZ_SUPPORT_ERROR;
#else
        mz_stream_libcomp_deflate(stream, COMPRESSION_STREAM_FINALIZE);
        mz_stream_libcomp_flush(stream);
#endif
    }
    else if (libcomp->mode & MZ_OPEN_MODE_READ)
    {
#ifdef MZ_ZIP_NO_DECOMPRESSION
        return MZ_SUPPORT_ERROR;
#endif
    }
    
    compression_stream_destroy(&libcomp->cstream);
    
    libcomp->initialized = 0;

    if (libcomp->error != Z_OK)
        return MZ_STREAM_ERROR;
    return MZ_OK;
}

int32_t mz_stream_libcomp_error(void *stream)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    return libcomp->error;
}

int32_t mz_stream_libcomp_get_prop_int64(void *stream, int32_t prop, int64_t *value)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    switch (prop)
    {
    case MZ_STREAM_PROP_TOTAL_IN:
        *value = libcomp->total_in;
        break;
    case MZ_STREAM_PROP_TOTAL_IN_MAX:
        *value = libcomp->max_total_in;
        break;
    case MZ_STREAM_PROP_TOTAL_OUT:
        *value = libcomp->total_out;
        break;
    case MZ_STREAM_PROP_HEADER_SIZE:
        *value = 0;
        break;
    default:
        return MZ_EXIST_ERROR;
    }
    return MZ_OK;
}

int32_t mz_stream_libcomp_set_prop_int64(void *stream, int32_t prop, int64_t value)
{
    mz_stream_libcomp *libcomp = (mz_stream_libcomp *)stream;
    switch (prop)
    {
    case MZ_STREAM_PROP_COMPRESS_ALGORITHM:
        libcomp->algorithm = (int16_t)value;
        break;
    case MZ_STREAM_PROP_TOTAL_IN_MAX:
        libcomp->max_total_in = value;
        break;
    default:
        return MZ_EXIST_ERROR;
    }
    return MZ_OK;
}

void *mz_stream_libcomp_create(void **stream)
{
    mz_stream_libcomp *libcomp = NULL;
    
    libcomp = (mz_stream_libcomp *)MZ_ALLOC(sizeof(mz_stream_libcomp));
    if (libcomp != NULL)
    {
        memset(libcomp, 0, sizeof(mz_stream_libcomp));
        libcomp->stream.vtbl = &mz_stream_libcomp_vtbl;
    }
    if (stream != NULL)
        *stream = libcomp;
    
    return libcomp;
}

void mz_stream_libcomp_delete(void **stream)
{
    mz_stream_libcomp *libcomp = NULL;
    if (stream == NULL)
        return;
    libcomp = (mz_stream_libcomp *)*stream;
    if (libcomp != NULL)
        MZ_FREE(libcomp);
    *stream = NULL;
}

/***************************************************************************/

// Define z_crc_t in zlib 1.2.5 and less
#if (ZLIB_VERNUM < 0x1270)
typedef unsigned long z_crc_t;
#endif

/***************************************************************************/

static mz_stream_vtbl mz_stream_zlib_vtbl = {
    mz_stream_libcomp_open,
    mz_stream_libcomp_is_open,
    mz_stream_libcomp_read,
    mz_stream_libcomp_write,
    mz_stream_libcomp_tell,
    mz_stream_libcomp_seek,
    mz_stream_libcomp_close,
    mz_stream_libcomp_error,
    mz_stream_zlib_create,
    mz_stream_libcomp_delete,
    mz_stream_libcomp_get_prop_int64,
    mz_stream_libcomp_set_prop_int64
};

void *mz_stream_zlib_create(void **stream)
{
    mz_stream_libcomp *libcomp = NULL;
    void *stream_int = NULL;
    mz_stream_libcomp_create(&stream_int);
    if (stream_int != NULL)
    {
        libcomp = (mz_stream_libcomp *)stream_int;
        libcomp->stream.vtbl = &mz_stream_zlib_vtbl;
        libcomp->algorithm = COMPRESSION_ZLIB;
    }
    if (stream != NULL)
        *stream = stream_int;
    return stream_int;
}

void *mz_stream_zlib_get_interface(void)
{
    return (void *)&mz_stream_zlib_vtbl;
}

static int64_t mz_stream_zlib_crc32(int64_t value, const void *buf, int32_t size)
{
    return (int64_t)crc32((z_crc_t)value, buf, (uint32_t)size);
}

void *mz_stream_zlib_get_crc32_update(void)
{
    return (void *)mz_stream_zlib_crc32;
}
