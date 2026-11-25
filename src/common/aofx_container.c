/*
 * aofx_container.c -- Build and parse the "raw container" of an SFX archive.
 *
 * Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
 * SPDX-License-Identifier: MIT
 *
 * This file is deliberately free of any Windows API so that it can be unit
 * tested on Linux (see test/run-tests.sh). It is shared by the creator
 * (auto-office.exe) and by the stub that is embedded in every generated SFX.
 */
#include "aofx_format.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* CRC-32                                                                    */
/* ------------------------------------------------------------------------ */

uint32_t aofx_crc32(const uint8_t *data, size_t length) {
  /* Bit-by-bit implementation: tiny and fast enough for a one-shot check.
   * 0xEDB88320 is the bit-reversed form of the polynomial 0x04C11DB7. */
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

/* ------------------------------------------------------------------------ */
/* Name validation                                                           */
/* ------------------------------------------------------------------------ */

int aofx_is_safe_name(const char *name) {
  size_t length = strlen(name);
  if (length == 0 || length > AOFX_MAX_NAME_LENGTH) return 0;
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)name[i];
    /* Control characters, path separators and ':' (drive letter or NTFS
     * alternate data stream) are all forbidden. Other Windows-reserved
     * characters make file creation fail, which is also safe. */
    if (c < 0x20 || c == '/' || c == '\\' || c == ':') return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------------ */
/* Little-endian read/write helpers                                          */
/* ------------------------------------------------------------------------ */

static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put_u64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint64_t get_u64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
  return v;
}

/* ------------------------------------------------------------------------ */
/* Container serialisation                                                   */
/* ------------------------------------------------------------------------ */

int aofx_container_build(const AofxEntry *entries, uint32_t count,
                         uint8_t **outBuffer, size_t *outSize) {
  if (count == 0 || count > AOFX_MAX_ENTRIES) return 1;

  /* First pass: validate and compute the total size. */
  uint64_t total = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (!aofx_is_safe_name(entries[i].name)) return 1;
    total += 2 + strlen(entries[i].name) + 8 + entries[i].size;
    if (total > AOFX_MAX_RAW_SIZE) return 1;
  }

  uint8_t *buffer = (uint8_t *)malloc((size_t)total);
  if (buffer == NULL) return 1;

  /* Second pass: write every entry one after the other. */
  uint8_t *p = buffer;
  for (uint32_t i = 0; i < count; i++) {
    size_t nameLength = strlen(entries[i].name);
    put_u16(p, (uint16_t)nameLength);
    p += 2;
    memcpy(p, entries[i].name, nameLength);
    p += nameLength;
    put_u64(p, entries[i].size);
    p += 8;
    if (entries[i].size > 0) memcpy(p, entries[i].data, (size_t)entries[i].size);
    p += entries[i].size;
  }

  *outBuffer = buffer;
  *outSize = (size_t)total;
  return 0;
}

/* ------------------------------------------------------------------------ */
/* Container parsing                                                         */
/* ------------------------------------------------------------------------ */

int aofx_container_parse(const uint8_t *buffer, size_t size, uint32_t count,
                         AofxEntry *entries, uint32_t maxEntries,
                         char *nameStorage) {
  if (count == 0 || count > maxEntries) return 1;

  size_t offset = 0;
  for (uint32_t i = 0; i < count; i++) {
    /* Every read below is preceded by a bounds check against `size`: the
     * container comes from a file we do not control, never trust it. */
    if (size - offset < 2) return 1;
    uint16_t nameLength = get_u16(buffer + offset);
    offset += 2;

    if (nameLength == 0 || nameLength > AOFX_MAX_NAME_LENGTH) return 1;
    if (size - offset < nameLength) return 1;
    char *name = nameStorage + (size_t)i * (AOFX_MAX_NAME_LENGTH + 1);
    memcpy(name, buffer + offset, nameLength);
    name[nameLength] = '\0';
    offset += nameLength;
    if (!aofx_is_safe_name(name)) return 1;

    if (size - offset < 8) return 1;
    uint64_t dataSize = get_u64(buffer + offset);
    offset += 8;
    if (dataSize > size - offset) return 1;

    entries[i].name = name;
    entries[i].data = buffer + offset;
    entries[i].size = dataSize;
    offset += (size_t)dataSize;
  }

  /* No trailing garbage allowed: the container must be consumed exactly. */
  return offset == size ? 0 : 1;
}
