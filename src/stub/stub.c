/*
 * stub.c -- The program that runs when the user opens the generated .exe.
 *
 * Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
 * SPDX-License-Identifier: MIT
 *
 * It is a GUI-subsystem program (no console window). What it does:
 *   1. open its own .exe file and read the AofxFooter from the last bytes;
 *   2. read the LZMA2 block located just before the footer;
 *   3. decompress it and check the CRC-32;
 *   4. extract setup.exe and configuration.xml into a new private folder
 *      under %TEMP%;
 *   5. run "setup.exe /configure configuration.xml" and wait for it;
 *   6. delete the temporary folder.
 * The only things the user sees are the Windows elevation prompt (the
 * manifest asks for administrator rights, which Office setup needs anyway)
 * and the Office installer itself.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../common/aofx_format.h"
#include "../common/aofx_lzma2.h"

/* ------------------------------------------------------------------------ */
/* Error reporting                                                           */
/* ------------------------------------------------------------------------ */

/* A GUI program has no console, so errors are shown in a message box. */
static void fail(const wchar_t *message) {
  MessageBoxW(NULL, message, L"Office installer", MB_OK | MB_ICONERROR);
}

/* ------------------------------------------------------------------------ */
/* Reading our own file                                                      */
/* ------------------------------------------------------------------------ */

/* Reads `size` bytes at absolute position `offset`. Returns 1 on success. */
static int read_at(HANDLE file, uint64_t offset, void *buffer, size_t size) {
  LARGE_INTEGER position;
  position.QuadPart = (LONGLONG)offset;
  if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN)) return 0;

  uint8_t *dest = (uint8_t *)buffer;
  while (size > 0) {
    /* ReadFile takes a DWORD, so very large reads are split in chunks. */
    DWORD chunk = size > 0x10000000u ? 0x10000000u : (DWORD)size;
    DWORD got = 0;
    if (!ReadFile(file, dest, chunk, &got, NULL) || got == 0) return 0;
    dest += got;
    size -= got;
  }
  return 1;
}

/* Writes a whole buffer to a new file. Returns 1 on success. */
static int write_file(const wchar_t *path, const uint8_t *data, uint64_t size) {
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) return 0;
  int ok = 1;
  while (size > 0 && ok) {
    DWORD chunk = size > 0x10000000u ? 0x10000000u : (DWORD)size;
    DWORD written = 0;
    ok = WriteFile(file, data, chunk, &written, NULL) && written == chunk;
    data += written;
    size -= written;
  }
  return CloseHandle(file) && ok;
}

/* ------------------------------------------------------------------------ */
/* Temporary folder                                                          */
/* ------------------------------------------------------------------------ */

/* Creates a brand-new folder "%TEMP%\auto-office-XXXXXXXX\" and stores its
 * path (with a trailing backslash) in `dir`. CreateDirectoryW fails if the
 * folder already exists, so nobody can pre-create it to hijack our files. */
static int make_temp_dir(wchar_t *dir, size_t capacity) {
  wchar_t base[MAX_PATH];
  DWORD length = GetTempPathW(MAX_PATH, base);
  if (length == 0 || length >= MAX_PATH) return 0;

  for (int attempt = 0; attempt < 20; attempt++) {
    /* The tick count and process id make the name unique in practice; the
     * attempt counter handles the (unlikely) collisions. */
    unsigned long id = (unsigned long)(GetTickCount64() ^ (GetCurrentProcessId() << 8)) + (unsigned long)attempt;
    if (swprintf(dir, capacity, L"%lsauto-office-%08lx\\", base, id) < 0) return 0;
    if (CreateDirectoryW(dir, NULL)) return 1;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return 0;
  }
  return 0;
}

/* Joins a folder path (ending with '\') and an UTF-8 file name. */
static int join_path(wchar_t *out, size_t capacity, const wchar_t *dir,
                     const char *utf8Name) {
  size_t dirLength = wcslen(dir);
  if (dirLength >= capacity) return 0;
  wcscpy(out, dir);
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Name, -1,
                              out + dirLength, (int)(capacity - dirLength));
  return n > 0;
}

/* Deletes the extracted files and then the folder. Failures are ignored: the
 * installer may still hold a file for a moment, so we retry a few times. */
static void cleanup_dir(const wchar_t *dir, const AofxEntry *entries,
                        uint32_t count) {
  for (int attempt = 0; attempt < 10; attempt++) {
    int allGone = 1;
    for (uint32_t i = 0; i < count; i++) {
      wchar_t path[MAX_PATH * 2];
      if (join_path(path, MAX_PATH * 2, dir, entries[i].name)) {
        if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND) allGone = 0;
      }
    }
    if (allGone && RemoveDirectoryW(dir)) return;
    Sleep(500);
  }
}

/* ------------------------------------------------------------------------ */
/* Entry point                                                               */
/* ------------------------------------------------------------------------ */

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine,
                    int showCommand) {
  (void)instance; (void)previous; (void)commandLine; (void)showCommand;

  int exitCode = 1;
  HANDLE self = INVALID_HANDLE_VALUE;
  uint8_t *packed = NULL, *raw = NULL;
  AofxEntry entries[AOFX_MAX_ENTRIES];
  char *names = NULL;
  uint32_t entryCount = 0;
  wchar_t dir[MAX_PATH];
  int dirCreated = 0;

  /* --- 1. Open our own executable ------------------------------------- */
  wchar_t selfPath[32768];
  DWORD pathLength = GetModuleFileNameW(NULL, selfPath, 32768);
  if (pathLength == 0 || pathLength >= 32768) { fail(L"Cannot locate the installer file."); goto done; }
  /* FILE_SHARE_READ: other programs (antivirus...) may read us at the same time. */
  self = CreateFileW(selfPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (self == INVALID_HANDLE_VALUE) { fail(L"Cannot open the installer file."); goto done; }

  LARGE_INTEGER fileSize;
  if (!GetFileSizeEx(self, &fileSize) || (uint64_t)fileSize.QuadPart < sizeof(AofxFooter)) {
    fail(L"The installer file is damaged."); goto done;
  }

  /* --- 2. Read and validate the footer -------------------------------- */
  AofxFooter footer;
  if (!read_at(self, (uint64_t)fileSize.QuadPart - sizeof footer, &footer, sizeof footer) ||
      memcmp(footer.magic, AOFX_MAGIC, sizeof footer.magic) != 0 ||
      footer.version != AOFX_VERSION ||
      footer.entryCount == 0 || footer.entryCount > AOFX_MAX_ENTRIES ||
      footer.rawSize == 0 || footer.rawSize > AOFX_MAX_RAW_SIZE ||
      footer.packedSize == 0 ||
      footer.packedSize > (uint64_t)fileSize.QuadPart - sizeof footer) {
    fail(L"The installer file is damaged (no valid data found).");
    goto done;
  }
  entryCount = footer.entryCount;

  /* --- 3. Read the compressed block and decompress it ----------------- */
  uint64_t packedOffset = (uint64_t)fileSize.QuadPart - sizeof footer - footer.packedSize;
  packed = (uint8_t *)malloc((size_t)footer.packedSize);
  raw = (uint8_t *)malloc((size_t)footer.rawSize);
  names = (char *)malloc((size_t)AOFX_MAX_ENTRIES * (AOFX_MAX_NAME_LENGTH + 1));
  if (packed == NULL || raw == NULL || names == NULL) { fail(L"Not enough memory."); goto done; }
  if (!read_at(self, packedOffset, packed, (size_t)footer.packedSize)) {
    fail(L"Cannot read the installer data."); goto done;
  }
  if (aofx_lzma2_decompress(packed, (size_t)footer.packedSize, footer.lzma2Prop,
                            raw, (size_t)footer.rawSize) != 0 ||
      aofx_crc32(raw, (size_t)footer.rawSize) != footer.rawCrc32) {
    fail(L"The installer data is corrupt. Please download it again.");
    goto done;
  }
  if (aofx_container_parse(raw, (size_t)footer.rawSize, entryCount, entries,
                           AOFX_MAX_ENTRIES, names) != 0) {
    fail(L"The installer data is corrupt. Please download it again.");
    goto done;
  }

  /* --- 4. Extract into a private temporary folder --------------------- */
  if (!make_temp_dir(dir, MAX_PATH)) { fail(L"Cannot create a temporary folder."); goto done; }
  dirCreated = 1;
  for (uint32_t i = 0; i < entryCount; i++) {
    wchar_t path[MAX_PATH * 2];
    if (!join_path(path, MAX_PATH * 2, dir, entries[i].name) ||
        !write_file(path, entries[i].data, entries[i].size)) {
      fail(L"Cannot extract the installer files (disk full?).");
      goto done;
    }
  }

  /* --- 5. Run: setup.exe /configure configuration.xml ----------------- */
  wchar_t setupPath[MAX_PATH * 2], xmlPath[MAX_PATH * 2], parameters[MAX_PATH * 2 + 32];
  if (!join_path(setupPath, MAX_PATH * 2, dir, AOFX_ENTRY_SETUP) ||
      !join_path(xmlPath, MAX_PATH * 2, dir, AOFX_ENTRY_XML) ||
      swprintf(parameters, MAX_PATH * 2 + 32, L"/configure \"%ls\"", xmlPath) < 0) {
    fail(L"Internal error while preparing the command line."); goto done;
  }

  /* ShellExecuteEx (instead of CreateProcess) lets Windows show the elevation
   * prompt if setup.exe needs it. SEE_MASK_NOCLOSEPROCESS gives us a process
   * handle so that we can wait until the installation is over. */
  SHELLEXECUTEINFOW info;
  memset(&info, 0, sizeof info);
  info.cbSize = sizeof info;
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  info.lpFile = setupPath;
  info.lpParameters = parameters;
  info.lpDirectory = dir;
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info) || info.hProcess == NULL) {
    fail(L"Cannot start the Office setup program.");
    goto done;
  }
  WaitForSingleObject(info.hProcess, INFINITE);
  DWORD setupExit = 0;
  GetExitCodeProcess(info.hProcess, &setupExit);
  CloseHandle(info.hProcess);
  exitCode = (int)setupExit; /* propagate setup.exe's result to our caller */

done:
  /* --- 6. Always remove what we extracted ----------------------------- */
  if (dirCreated) cleanup_dir(dir, entries, entryCount);
  if (self != INVALID_HANDLE_VALUE) CloseHandle(self);
  free(packed);
  free(raw);
  free(names);
  return exitCode;
}
