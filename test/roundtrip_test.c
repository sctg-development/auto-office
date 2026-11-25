/*
 * roundtrip_test.c -- Native (Linux) unit test of the portable archive code.
 *
 * Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
 * SPDX-License-Identifier: MIT
 *
 * It exercises exactly the code that ends up in auto-office.exe and in the
 * stub: container build/parse, LZMA2 compression/decompression, CRC-32 and the
 * rejection of malicious input. Run it with test/run-tests.sh.
 * Optional arguments: two real files (e.g. setup.exe and a config XML) to use
 * as test data instead of synthetic buffers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/common/aofx_format.h"
#include "../src/common/aofx_lzma2.h"

static int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      printf("FAIL line %d: %s\n", __LINE__, #condition);                  \
      failures++;                                                          \
    }                                                                      \
  } while (0)

static uint8_t *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
  if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
  fclose(f);
  *size = (size_t)n;
  return b;
}

/* Full pipeline: entries -> container -> LZMA2 -> decompress -> parse. */
static void roundtrip(const uint8_t *a, size_t aSize, const uint8_t *b, size_t bSize) {
  AofxEntry in[2] = {{AOFX_ENTRY_SETUP, a, aSize}, {AOFX_ENTRY_XML, b, bSize}};
  uint8_t *raw = NULL, *packed = NULL, *back = NULL;
  size_t rawSize = 0, packedSize = 0;
  uint8_t prop = 0;

  CHECK(aofx_container_build(in, 2, &raw, &rawSize) == 0);
  CHECK(aofx_lzma2_compress(raw, rawSize, 9, &packed, &packedSize, &prop) == 0);
  printf("  raw %zu -> packed %zu bytes\n", rawSize, packedSize);

  back = malloc(rawSize);
  CHECK(aofx_lzma2_decompress(packed, packedSize, prop, back, rawSize) == 0);
  CHECK(memcmp(raw, back, rawSize) == 0);
  CHECK(aofx_crc32(raw, rawSize) == aofx_crc32(back, rawSize));

  AofxEntry out[AOFX_MAX_ENTRIES];
  char *names = malloc(AOFX_MAX_ENTRIES * (AOFX_MAX_NAME_LENGTH + 1));
  CHECK(aofx_container_parse(back, rawSize, 2, out, AOFX_MAX_ENTRIES, names) == 0);
  CHECK(strcmp(out[0].name, AOFX_ENTRY_SETUP) == 0 && out[0].size == aSize &&
        (aSize == 0 || memcmp(out[0].data, a, aSize) == 0));
  CHECK(strcmp(out[1].name, AOFX_ENTRY_XML) == 0 && out[1].size == bSize &&
        (bSize == 0 || memcmp(out[1].data, b, bSize) == 0));

  /* Tampering must be detected: truncated stream, flipped bit, wrong size. */
  if (packedSize > 16) {
    CHECK(aofx_lzma2_decompress(packed, packedSize - 5, prop, back, rawSize) != 0);
    packed[packedSize / 2] ^= 0x40;
    int rc = aofx_lzma2_decompress(packed, packedSize, prop, back, rawSize);
    /* Either the decoder complains, or the CRC no longer matches. */
    CHECK(rc != 0 || aofx_crc32(back, rawSize) != aofx_crc32(raw, rawSize));
  }
  /* A container with the wrong entry count or trailing bytes is refused. */
  CHECK(aofx_container_parse(raw, rawSize, 1, out, AOFX_MAX_ENTRIES, names) != 0);
  CHECK(aofx_container_parse(raw, rawSize, 3, out, AOFX_MAX_ENTRIES, names) != 0);
  CHECK(aofx_container_parse(raw, rawSize - 1, 2, out, AOFX_MAX_ENTRIES, names) != 0);

  free(raw); free(packed); free(back); free(names);
}

int main(int argc, char **argv) {
  /* 1. CRC-32 known answer ("123456789" -> 0xCBF43926). */
  CHECK(aofx_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);

  /* 2. Name validation: path traversal and friends must be rejected. */
  CHECK(aofx_is_safe_name("setup.exe"));
  CHECK(!aofx_is_safe_name(""));
  CHECK(!aofx_is_safe_name("."));
  CHECK(!aofx_is_safe_name(".."));
  CHECK(!aofx_is_safe_name("..\\evil.dll"));
  CHECK(!aofx_is_safe_name("a/b"));
  CHECK(!aofx_is_safe_name("C:evil"));
  CHECK(!aofx_is_safe_name("file:stream"));

  /* 3. A hand-made hostile container with a traversal name is refused. */
  {
    uint8_t evil[] = {9, 0, '.', '.', '\\', 'e', 'v', 'i', 'l', 0, 0, 0, 0, 0, 0, 0, 0};
    AofxEntry out[AOFX_MAX_ENTRIES];
    char *names = malloc(AOFX_MAX_ENTRIES * (AOFX_MAX_NAME_LENGTH + 1));
    CHECK(aofx_container_parse(evil, sizeof evil, 1, out, AOFX_MAX_ENTRIES, names) != 0);
    /* Declared data size larger than what remains in the buffer. */
    uint8_t huge[] = {1, 0, 'a', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
    CHECK(aofx_container_parse(huge, sizeof huge, 1, out, AOFX_MAX_ENTRIES, names) != 0);
    free(names);
  }

  /* 4. Round trips: synthetic data (compressible + random + empty XML). */
  puts("synthetic data");
  {
    size_t n = 3 * 1024 * 1024;
    uint8_t *a = malloc(n), *b = (uint8_t *)"<Configuration/>";
    for (size_t i = 0; i < n; i++) a[i] = (uint8_t)((i * 31) ^ (i >> 7) ^ (rand() & 3));
    roundtrip(a, n, b, strlen((char *)b));
    free(a);
    roundtrip((const uint8_t *)"MZ", 2, (const uint8_t *)"x", 1); /* tiny */
  }

  /* 5. Round trip on real files when given on the command line. */
  if (argc == 3) {
    size_t sa, sb;
    uint8_t *a = read_file(argv[1], &sa), *b = read_file(argv[2], &sb);
    if (a && b) {
      printf("real files: %s (%zu bytes), %s (%zu bytes)\n", argv[1], sa, argv[2], sb);
      roundtrip(a, sa, b, sb);
    } else {
      puts("FAIL: cannot read the real test files");
      failures++;
    }
    free(a); free(b);
  }

  if (failures) { printf("%d check(s) FAILED\n", failures); return 1; }
  puts("all checks passed");
  return 0;
}
