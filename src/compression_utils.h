#ifndef _COMPRESSION_UTILS_H_
#define _COMPRESSION_UTILS_H_

#if 0
#include "compressor.h" // int64_t
#endif

extern const int64_t g_defaultDistribution[];
#if 0
void CompressCallback(void *context, int channel, unsigned char *buf, int n);
void DecompressCallback(void *context, int channel, unsigned char *buf, int n);
#endif
void EJCompressCallback(void *context, unsigned char *buf, int n);
void EJDecompressCallback(void *context, unsigned char *buf, int n);

#endif
