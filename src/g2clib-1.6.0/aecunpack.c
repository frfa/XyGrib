/*
 * aecunpack.c - CCSDS Adaptive Entropy Coding (AEC) unpacker for GRIB2
 * Data Representation Template 5.42
 *
 * Uses libaec (https://gitlab.dkrz.de/dkrz-sw/libaec) for decompression.
 * libaec API is reproduced here to avoid requiring libaec-dev headers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grib2.h"

/* ---- Minimal libaec API reproduced from libaec.h ---- */

/* Flags for aec_stream */
#define AEC_DATA_SIGNED     1   /* signed data values          */
#define AEC_DATA_3BYTE      2   /* 3 byte samples              */
#define AEC_DATA_PREPROCESS 4   /* preprocess input            */
#define AEC_DATA_MSB        8   /* samples in most significant bit (MSB) order */
#define AEC_RESTRICTED     16   /* restricted coding           */
#define AEC_PAD_RSI        32   /* RSI padded to byte boundary */
#define AEC_NOT_ENFORCE    64   /* do not enforce standard block sizes */

#define AEC_OK             0
#define AEC_CONF_ERROR    (-1)
#define AEC_STREAM_END     1
#define AEC_MEM_ERROR    (-2)

struct aec_stream {
    const unsigned char *next_in;   /* next input byte */
    size_t              avail_in;   /* number of bytes available at next_in */
    size_t              total_in;   /* total number of input bytes consumed */
    unsigned char       *next_out;  /* next output byte */
    size_t              avail_out;  /* remaining free space at next_out */
    size_t              total_out;  /* total number of output bytes produced */
    unsigned int        bits_per_sample; /* bits per sample: 1-32 */
    unsigned int        block_size;      /* samples per block: must be power of 2 */
    unsigned int        rsi;             /* reference sample interval */
    unsigned int        flags;
    void                *state;         /* internal state - do not touch */
};

typedef int (*aec_decode_init_fn)(struct aec_stream *);
typedef int (*aec_decode_fn)(struct aec_stream *, int);
typedef int (*aec_decode_end_fn)(struct aec_stream *);
typedef int (*aec_buffer_decode_fn)(struct aec_stream *);

/* ---- Dynamic loading of libaec ---- */
#include <dlfcn.h>

static void *libaec_handle = NULL;
static aec_decode_init_fn   p_aec_decode_init   = NULL;
static aec_decode_fn        p_aec_decode        = NULL;
static aec_decode_end_fn    p_aec_decode_end    = NULL;
static aec_buffer_decode_fn p_aec_buffer_decode = NULL;

static int load_libaec(void)
{
    if (libaec_handle) return 1;

    /* Try multiple known paths */
    const char *paths[] = {
        "libaec.so.0",
        "libaec.so",
        "/usr/lib/x86_64-linux-gnu/libaec.so.0",
        "/usr/lib/x86_64-linux-gnu/libaec.so.0.1.2",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        libaec_handle = dlopen(paths[i], RTLD_LAZY | RTLD_LOCAL);
        if (libaec_handle) break;
    }

    if (!libaec_handle) {
        fprintf(stderr, "[g2clib] Cannot load libaec: %s\n", dlerror());
        fprintf(stderr, "[g2clib] Install libaec: sudo apt-get install libaec0\n");
        return 0;
    }

    p_aec_decode_init   = (aec_decode_init_fn)   dlsym(libaec_handle, "aec_decode_init");
    p_aec_decode        = (aec_decode_fn)         dlsym(libaec_handle, "aec_decode");
    p_aec_decode_end    = (aec_decode_end_fn)     dlsym(libaec_handle, "aec_decode_end");
    p_aec_buffer_decode = (aec_buffer_decode_fn)  dlsym(libaec_handle, "aec_buffer_decode");

    if (!p_aec_decode_init || !p_aec_decode || !p_aec_decode_end) {
        fprintf(stderr, "[g2clib] libaec symbols not found\n");
        dlclose(libaec_handle);
        libaec_handle = NULL;
        return 0;
    }

    return 1;
}

/*
 * aecunpack - Unpack GRIB2 CCSDS-compressed data (DRS Template 5.42)
 *
 * cpack    - pointer to packed data section
 * len      - length of packed data in bytes
 * idrstmpl - DRS template 5.42 values:
 *   [0] = reference value (packed as IEEE float)
 *   [1] = binary scale factor (E)
 *   [2] = decimal scale factor (D)
 *   [3] = number of bits
 *   [4] = type of original field values
 *   [5] = CCSDS compression options mask
 *   [6] = block size
 *   [7] = reference sample interval (RSI)
 * ndpts    - number of data values
 * fld      - output float array (must be pre-allocated, size ndpts)
 *
 * Returns 0 on success, non-zero on error.
 */
g2int aecunpack(unsigned char *cpack, g2int len, g2int *idrstmpl, g2int ndpts, g2float *fld)
{
    g2int j, nbits;
    g2int *ifld;
    float ref, bscale, dscale;
    unsigned int nbytes_per_sample;
    g2int ccsds_flags, block_size, rsi;
    struct aec_stream strm;
    unsigned char *buf;
    size_t buf_size;
    int ret;

    if (!load_libaec()) {
        return 1;
    }

    /* Unpack DRS 5.42 template parameters */
    /* idrstmpl[0]: reference value R (IEEE 32-bit float stored as int) */
    {
        unsigned int iref = (unsigned int)idrstmpl[0];
        memcpy(&ref, &iref, 4);
    }
    bscale = (float)( 1 << (idrstmpl[1] >= 0 ? idrstmpl[1] : -idrstmpl[1]) );
    if (idrstmpl[1] < 0) bscale = 1.0f / bscale;

    dscale = 1.0f;
    if (idrstmpl[2] != 0) {
        float d10 = 1.0f;
        int absD = idrstmpl[2] >= 0 ? idrstmpl[2] : -idrstmpl[2];
        for (j = 0; j < absD; j++) d10 *= 10.0f;
        dscale = (idrstmpl[2] > 0) ? (1.0f / d10) : d10;
    }

    nbits = idrstmpl[3];
    /* idrstmpl[4] = type of original field (0=float, 1=integer) */
    ccsds_flags = idrstmpl[5];   /* CCSDS compression options mask */
    block_size  = idrstmpl[6];   /* block size (samples per block) */
    rsi         = idrstmpl[7];   /* reference sample interval */

    if (nbits == 0 || ndpts == 0) {
        /* Constant field */
        for (j = 0; j < ndpts; j++) fld[j] = ref * dscale;
        return 0;
    }

    /* Compute bytes needed for integer output */
    if (nbits <= 8)       nbytes_per_sample = 1;
    else if (nbits <= 16) nbytes_per_sample = 2;
    else if (nbits <= 24) nbytes_per_sample = 3;
    else                  nbytes_per_sample = 4;

    buf_size = (size_t)ndpts * nbytes_per_sample;
    buf = (unsigned char *)malloc(buf_size);
    if (!buf) {
        fprintf(stderr, "[g2clib] aecunpack: out of memory\n");
        return 2;
    }

    /* Set up libaec stream */
    memset(&strm, 0, sizeof(strm));
    strm.next_in    = (const unsigned char *)cpack;
    strm.avail_in   = (size_t)len;
    strm.next_out   = buf;
    strm.avail_out  = buf_size;
    strm.bits_per_sample = (unsigned int)nbits;
    strm.block_size = block_size > 0 ? (unsigned int)block_size : 8;
    strm.rsi        = rsi > 0 ? (unsigned int)rsi : 128;

    /* Map GRIB2 CCSDS flags to libaec flags
     * GRIB2 DRS 5.42 octet 22 bits:
     *   bit 1 (0x01): preprocess (use bit-grooming / differencing)
     *   bit 2 (0x02): block size != default
     *   bit 3 (0x04): restricted coding
     * libaec flags: AEC_DATA_PREPROCESS=4, AEC_DATA_MSB=8, AEC_RESTRICTED=16, AEC_PAD_RSI=32
     */
    strm.flags = AEC_DATA_MSB;  /* GRIB always stores MSB first */
    if (ccsds_flags & 0x01) strm.flags |= AEC_DATA_PREPROCESS;
    if (ccsds_flags & 0x04) strm.flags |= AEC_RESTRICTED;

    ret = p_aec_decode_init(&strm);
    if (ret != AEC_OK) {
        fprintf(stderr, "[g2clib] aecunpack: aec_decode_init failed (%d)\n", ret);
        free(buf);
        return 3;
    }

    ret = p_aec_decode(&strm, AEC_STREAM_END);
    p_aec_decode_end(&strm);

    if (ret != AEC_OK && ret != AEC_STREAM_END) {
        fprintf(stderr, "[g2clib] aecunpack: aec_decode failed (%d)\n", ret);
        free(buf);
        return 4;
    }

    /* Convert integer packed values to floats using GRIB2 formula:
     * Y = (R + X * 2^E) / 10^D
     * where X = unpacked integer value
     */
    if (nbytes_per_sample == 1) {
        for (j = 0; j < ndpts; j++) {
            fld[j] = (ref + (g2float)buf[j] * bscale) * dscale;
        }
    } else if (nbytes_per_sample == 2) {
        for (j = 0; j < ndpts; j++) {
            unsigned int x = ((unsigned int)buf[j*2] << 8) | buf[j*2+1];
            fld[j] = (ref + (g2float)x * bscale) * dscale;
        }
    } else if (nbytes_per_sample == 3) {
        for (j = 0; j < ndpts; j++) {
            unsigned int x = ((unsigned int)buf[j*3] << 16)
                           | ((unsigned int)buf[j*3+1] << 8)
                           | buf[j*3+2];
            fld[j] = (ref + (g2float)x * bscale) * dscale;
        }
    } else {
        for (j = 0; j < ndpts; j++) {
            unsigned int x = ((unsigned int)buf[j*4] << 24)
                           | ((unsigned int)buf[j*4+1] << 16)
                           | ((unsigned int)buf[j*4+2] << 8)
                           | buf[j*4+3];
            fld[j] = (ref + (g2float)x * bscale) * dscale;
        }
    }

    free(buf);
    return 0;
}
