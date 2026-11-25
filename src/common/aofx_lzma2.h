/*
 * aofx_lzma2.h -- Thin wrappers around the LZMA SDK (LZMA2 codec).
 *
 * Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
 * SPDX-License-Identifier: MIT
 *
 * The LZMA SDK (third_party/lzma) is public domain software by Igor Pavlov.
 * We only use two of its features, both working on memory buffers:
 *   - aofx_lzma2_compress()    (needs Lzma2Enc.c, used by the creator only)
 *   - aofx_lzma2_decompress()  (needs Lzma2Dec.c, used by the stub only)
 */
#ifndef AOFX_LZMA2_H
#define AOFX_LZMA2_H

#include <stddef.h>
#include <stdint.h>

/* Compresses `input` with LZMA2. `level` is 0 (fastest) to 9 (smallest).
 * On success returns 0, stores a malloc'ed buffer in *output (the caller must
 * free() it), its size in *outputSize, and the LZMA2 property byte that the
 * decoder needs in *prop. */
int aofx_lzma2_compress(const uint8_t *input, size_t inputSize, int level,
                        uint8_t **output, size_t *outputSize, uint8_t *prop);

/* Decompresses exactly `packedSize` bytes into `output`, which must be able to
 * hold `rawSize` bytes. Returns 0 only if the stream is complete and produces
 * exactly `rawSize` bytes. */
int aofx_lzma2_decompress(const uint8_t *packed, size_t packedSize,
                          uint8_t prop, uint8_t *output, size_t rawSize);

#endif /* AOFX_LZMA2_H */
