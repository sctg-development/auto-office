/*
 * aofx_format.h -- On-disk format of an "auto-office" self-extracting archive.
 *
 * Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
 * SPDX-License-Identifier: MIT
 *
 * A finished self-extracting file (SFX) is laid out like this:
 *
 *   +-------------------------+
 *   | stub (a normal .exe)    |  <- Windows loads and runs this part only
 *   +-------------------------+
 *   | LZMA2 compressed block  |  <- "overlay": bytes after the last PE section
 *   +-------------------------+
 *   | AofxFooter (fixed size) |  <- always the LAST bytes of the file
 *   +-------------------------+
 *
 * Windows ignores everything that follows the last PE section, so appending
 * data to an .exe does not break it. At run time the stub opens its own file,
 * reads the footer from the end, finds the compressed block just before it,
 * decompresses it and extracts the files it contains.
 *
 * The compressed block, once decompressed ("raw container"), is a plain list
 * of entries, all integers little-endian:
 *
 *   repeat entryCount times:
 *     uint16  nameLength        number of bytes in the name (1..255)
 *     bytes   name              UTF-8, a bare file name (no directories)
 *     uint64  dataSize          number of bytes of file content
 *     bytes   data              the file content
 *
 * We only target little-endian CPUs (x86-64), so structures are written to
 * disk as-is and no byte swapping is performed.
 */
#ifndef AOFX_FORMAT_H
#define AOFX_FORMAT_H

#include <stddef.h>
#include <stdint.h>

/* Magic bytes that identify a valid footer (8 bytes, including two NULs). */
#define AOFX_MAGIC "AOFXv1\0"

/* Format version, bumped whenever the layout above changes. */
#define AOFX_VERSION 1u

/* Names of the two files the Office Deployment Tool flow needs. The stub
 * extracts them and then runs:  setup.exe /configure configuration.xml  */
#define AOFX_ENTRY_SETUP "setup.exe"
#define AOFX_ENTRY_XML "configuration.xml"

/* Sanity limits used to reject corrupt or hostile archives early. */
#define AOFX_MAX_ENTRIES 16u
#define AOFX_MAX_NAME_LENGTH 255u
#define AOFX_MAX_RAW_SIZE (1ull << 31) /* 2 GiB of decompressed data */

#pragma pack(push, 1)
typedef struct AofxFooter {
  char magic[8];        /* AOFX_MAGIC                                        */
  uint32_t version;     /* AOFX_VERSION                                      */
  uint32_t entryCount;  /* number of entries in the raw container            */
  uint64_t rawSize;     /* size of the container after decompression         */
  uint64_t packedSize;  /* size of the LZMA2 block stored before the footer  */
  uint32_t rawCrc32;    /* CRC-32 of the decompressed container              */
  uint8_t lzma2Prop;    /* LZMA2 "dictionary size" property byte             */
  uint8_t reserved[3];  /* always zero, kept for future use                  */
} AofxFooter;
#pragma pack(pop)

/* ------------------------------------------------------------------------ */
/* Helpers implemented in aofx_container.c (portable C, no Windows API).     */
/* ------------------------------------------------------------------------ */

/* One file inside the container. Pointers are NOT owned by the struct. */
typedef struct AofxEntry {
  const char *name;         /* NUL-terminated UTF-8 name                     */
  const uint8_t *data;      /* file content                                  */
  uint64_t size;            /* file size in bytes                            */
} AofxEntry;

/* Standard CRC-32 (IEEE 802.3, the one used by zip and PNG). */
uint32_t aofx_crc32(const uint8_t *data, size_t length);

/* Returns 1 if `name` is a safe bare file name, 0 otherwise. Rejects empty
 * names, path separators, drive letters ("C:"), "." and "..". This is what
 * protects the user from "zip slip" style path traversal in a tampered file. */
int aofx_is_safe_name(const char *name);

/* Serialises `count` entries into a freshly malloc'ed raw container.
 * Returns 0 on success (caller must free(*outBuffer)), non-zero on error. */
int aofx_container_build(const AofxEntry *entries, uint32_t count,
                         uint8_t **outBuffer, size_t *outSize);

/* Walks a raw container and fills `entries` (at most `maxEntries`). The
 * returned name pointers are copies owned by `nameStorage` (see below) and the
 * data pointers point INSIDE `buffer`, so `buffer` must outlive the entries.
 *
 * `nameStorage` must hold at least maxEntries * (AOFX_MAX_NAME_LENGTH + 1)
 * bytes. Every entry is validated (bounds, name safety). Returns 0 on success.
 */
int aofx_container_parse(const uint8_t *buffer, size_t size, uint32_t count,
                         AofxEntry *entries, uint32_t maxEntries,
                         char *nameStorage);

#endif /* AOFX_FORMAT_H */
