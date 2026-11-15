/*
 * aecunpack.c
 *
 * Unpacks a data field compressed with CCSDS / AEC (Adaptive Entropy
 * Coding), following GRIB2 Data Representation Template 5.42.
 *
 * Requires libaec (Debian/Ubuntu/antiX: libaec-dev).
 *
 * idrstmpl[0] : reference value (IEEE 32 bits)
 * idrstmpl[1] : binary scale factor
 * idrstmpl[2] : decimal scale factor
 * idrstmpl[3] : number of bits per value
 * idrstmpl[4] : type of original field
 * idrstmpl[5] : CCSDS options mask (libaec flags)
 * idrstmpl[6] : block size
 * idrstmpl[7] : reference sample interval (RSI)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libaec.h>
#include "grib2.h"

g2int aecunpack(unsigned char *cpack, g2int len, g2int *idrstmpl,
                g2int ndpts, g2float *fld)
{
    struct aec_stream strm;
    unsigned char *ctmp;
    g2int j, nbits, bytes_per_sample;
    g2float ref, bscale, dscale;
    unsigned int flags;
    size_t outlen;
    int ret;

    rdieee(idrstmpl + 0, &ref, 1);
    bscale = int_power(2.0, idrstmpl[1]);
    dscale = int_power(10.0, -idrstmpl[2]);
    nbits = idrstmpl[3];
    flags = (unsigned int)idrstmpl[5];

    /* nbits == 0 : constant field equal to the reference value */
    if (nbits == 0) {
        for (j = 0; j < ndpts; j++)
            fld[j] = ref * dscale;
        return 0;
    }

    if (nbits > 32) {
        fprintf(stderr, "aecunpack: %d bits per sample not supported.\n",
                (int)nbits);
        return 1;
    }

    /* Sample size in the output buffer, following libaec's rules */
    if (nbits <= 8)
        bytes_per_sample = 1;
    else if (nbits <= 16)
        bytes_per_sample = 2;
    else if (nbits <= 24)
        bytes_per_sample = (flags & AEC_DATA_3BYTE) ? 3 : 4;
    else
        bytes_per_sample = 4;

    outlen = (size_t)ndpts * (size_t)bytes_per_sample;
    ctmp = (unsigned char *)calloc(outlen, 1);
    if (ctmp == NULL) {
        fprintf(stderr, "aecunpack: out of memory. Field NOT unpacked.\n");
        return 1;
    }

    memset(&strm, 0, sizeof(strm));
    strm.flags = flags;
    strm.bits_per_sample = (unsigned int)nbits;
    strm.block_size = (unsigned int)idrstmpl[6];
    strm.rsi = (unsigned int)idrstmpl[7];
    strm.next_in = (const unsigned char *)cpack;
    strm.avail_in = (size_t)len;
    strm.next_out = ctmp;
    strm.avail_out = outlen;

    ret = aec_buffer_decode(&strm);
    if (ret != AEC_OK) {
        fprintf(stderr, "aecunpack: libaec error %d. "
                        "Field NOT unpacked.\n", ret);
        free(ctmp);
        return 1;
    }

    /* Rebuild the integers and apply the scale factors */
    for (j = 0; j < ndpts; j++) {
        unsigned char *p = ctmp + (size_t)j * (size_t)bytes_per_sample;
        unsigned long v = 0;
        int k;

        if (flags & AEC_DATA_MSB) {
            for (k = 0; k < bytes_per_sample; k++)
                v = (v << 8) | p[k];
        } else {
            for (k = bytes_per_sample - 1; k >= 0; k--)
                v = (v << 8) | p[k];
        }

        fld[j] = (((g2float)v * bscale) + ref) * dscale;
    }

    free(ctmp);
    return 0;
}
