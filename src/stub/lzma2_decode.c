/*
 * lzma2_decode.c -- LZMA2 decompression (stub side).
 *
 * Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
 * SPDX-License-Identifier: MIT
 */
#include "../common/aofx_lzma2.h"
#include "Alloc.h"
#include "Lzma2Dec.h"

int aofx_lzma2_decompress(const uint8_t *packed, size_t packedSize,
                          uint8_t prop, uint8_t *output, size_t rawSize) {
  SizeT destLen = rawSize;
  SizeT srcLen = packedSize;
  ELzmaStatus status;

  /* One-shot decoder: LZMA_FINISH_END tells it that the input is complete and
   * that the output must end exactly at `rawSize`. */
  SRes res = Lzma2Decode(output, &destLen, packed, &srcLen, prop,
                         LZMA_FINISH_END, &status, &g_Alloc);
  if (res != SZ_OK) return 1;
  /* Reject truncated streams and streams with unused trailing bytes. */
  if (destLen != rawSize || srcLen != packedSize) return 1;
  if (status != LZMA_STATUS_FINISHED_WITH_MARK &&
      status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)
    return 1;
  return 0;
}
