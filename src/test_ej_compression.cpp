// Need to test having a previous set of values.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ej_compress.h"

static unsigned char *g_store = nullptr, *g_store_ptr = nullptr;

static void EJWriteCallback(void *context, unsigned char *buf, int n) {
  // int pos = (int)(g_store_ptr - g_store);
  memcpy(g_store_ptr, buf, n);
  g_store_ptr += n;
}

static void EJReadCallback(void *context, unsigned char *buf, int n) {
  memcpy(buf, g_store_ptr, n);
  g_store_ptr += n;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <stride>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 2) Usage(argv[0]);
  int stride;
  if (sscanf(argv[1], "%i", &stride) != 1) Usage(argv[0]);
  int *xs = new int[100];
  int *ys = new int[100];
  for (unsigned int i = 0; i < 16; ++i) {
    xs[i] = 0;
  }
  for (unsigned int i = 16; i < 99; ++i) {
    if (i % 2 == 0)      xs[i] = 2;
    else if (i % 3 == 0) xs[i] = 3;
    else if (i % 5 == 0) xs[i] = 5;
    else if (i % 7 == 0) xs[i] = 7;
    else                 xs[i] = i;
  }
  xs[99] = 99999;

  g_store = new unsigned char[500000];
  g_store_ptr = g_store;
  
  long long int new_distribution[COMPRESSOR_DISTRIBUTION_SIZE];
  for (unsigned int i = 0; i < COMPRESSOR_DISTRIBUTION_SIZE; ++i) {
    new_distribution[i] = 0;
  }
  // SetWriteCallback(EJWriteCallback);
  EJCompressor *compressor =
    new EJCompressor(g_ej_defaultDistribution, new_distribution,
		     EJWriteCallback, nullptr);
  compressor->Compress(xs, NULL, 100, stride);
  delete compressor;
  unsigned int sz_compressed = g_store_ptr - g_store;
  printf("Compressed bytes: %i\n", (int)sz_compressed);

  // SetReadContext((void *)ys);
  // SetReadCallback(EJReadCallback);
  g_store_ptr = g_store;
  EJDecompressor decompressor(EJReadCallback, NULL, NULL);
  decompressor.Decompress(ys, NULL, 100, stride);
  for (int i = 0; i < 100; ++i) {
    if (xs[i] != ys[i]) {
      fprintf(stderr, "Mismatch at i %i (x %u y %u)\n", i, xs[i], ys[i]);
      exit(-1);
    }
  }
  fprintf(stderr, "Match!\n");

  int *x1s = new int[100];
  for (unsigned int i = 0; i < 100; ++i) {
    x1s[i] = xs[i] + i % 5;
  }
  g_store_ptr = g_store;
  EJCompressor *compressor2 =
    new EJCompressor(g_ej_defaultDistribution, new_distribution,
		     EJWriteCallback, nullptr);
  compressor2->Compress(x1s, xs, 100, stride);
  delete compressor2;
  sz_compressed = g_store_ptr - g_store;
  printf("Compressed bytes: %i\n", (int)sz_compressed);

  g_store_ptr = g_store;
  EJDecompressor decompressor2(EJReadCallback, NULL, NULL);
  decompressor2.Decompress(ys, xs, 100, stride);
  for (int i = 0; i < 100; ++i) {
    if (x1s[i] != ys[i]) {
      fprintf(stderr, "Mismatch at i %i (x %u y %u)\n", i, x1s[i], ys[i]);
      exit(-1);
    }
  }
  fprintf(stderr, "Match!\n");
  
  delete [] xs;
  delete [] ys;
  delete [] g_store;
}
