/*
 * lzma2_encode.c -- LZMA2 compression (creator side).
 *
 * Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "../common/aofx_lzma2.h"
#include "Alloc.h"
#include "Lzma2Enc.h"

int aofx_lzma2_compress(const uint8_t *input, size_t inputSize, int level,
                        uint8_t **output, size_t *outputSize, uint8_t *prop) {
  int result = 1;
  CLzma2EncHandle encoder = Lzma2Enc_Create(&g_Alloc, &g_BigAlloc);
  if (encoder == NULL) return 1;

  /* Start from the SDK defaults, then tune them for a one-shot compression of
   * a buffer whose size we know. */
  CLzma2EncProps props;
  Lzma2EncProps_Init(&props);
  props.lzmaProps.level = level;
  /* Never allocate a dictionary bigger than the data: it would only waste
   * memory. The decoder reads the real size from the property byte. */
  if (inputSize < ((size_t)1 << 26)) {
    UInt32 dict = 1u << 12; /* start at 4 KiB, grow to the next power of two */
    while (dict < inputSize) dict <<= 1;
    props.lzmaProps.dictSize = dict;
  } else {
    props.lzmaProps.dictSize = 1u << 26; /* 64 MiB cap */
  }
  /* The SDK is built single-threaded (Z7_ST): one block, one thread. */
  props.numTotalThreads = 1;
  Lzma2EncProps_Normalize(&props);
  if (Lzma2Enc_SetProps(encoder, &props) != SZ_OK) goto done;

  /* LZMA2 can store incompressible data raw, so the output is never much
   * larger than the input. This bound (input + 1 % + 64 KiB) is generous. */
  size_t capacity = inputSize + inputSize / 100 + (1u << 16);
  uint8_t *buffer = (uint8_t *)malloc(capacity);
  if (buffer == NULL) goto done;

  size_t produced = capacity;
  /* Buffer-to-buffer mode: stream arguments are NULL, data pointers are used. */
  if (Lzma2Enc_Encode2(encoder, NULL, buffer, &produced, NULL, input,
                       inputSize, NULL) != SZ_OK) {
    free(buffer);
    goto done;
  }

  *prop = Lzma2Enc_WriteProperties(encoder);
  *output = buffer;
  *outputSize = produced;
  result = 0;

done:
  Lzma2Enc_Destroy(encoder);
  return result;
}
