#!/bin/bash
# build.sh -- Cross-compile auto-office.exe (Windows x86-64) on Linux.
#
# Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT
# SPDX-License-Identifier: MIT
#
# Requirements: the highcanfly/llvm4msvc toolchain (clang-cl, lld-link and the
# Windows SDK/CRT in /usr/share/msvc), plus od/sed/stat from coreutils. All of
# them are present in the hcf-coder development image. Result: build/auto-office.exe
#
# Two stages are needed because auto-office.exe embeds the extractor stub:
#   stage 1: compile the stub                 -> build/aofx_stub.exe
#   stage 2: turn it into a C array, compile the creator with it
#                                              -> build/auto-office.exe
set -euo pipefail
cd "$(dirname "$0")"

# $MSVC_CL / $MSVC_LINK are defined by /usr/local/bin/msvc.env (loaded by
# /etc/profile). Load it ourselves when running from a non-login shell.
if [ -z "${MSVC_CL:-}" ] || [ -z "${MSVC_LINK:-}" ]; then
  # shellcheck disable=SC1091
  . /usr/local/bin/msvc.env
fi

# MSVC_CL / MSVC_LINK contain a command plus arguments, so they must stay
# unquoted below (word splitting is intended).
# shellcheck disable=SC2086
CL_CMD="$MSVC_CL"
# shellcheck disable=SC2086
LINK_CMD="$MSVC_LINK"

OUT=build
OBJ=$OUT/obj
rm -rf "$OUT"
mkdir -p "$OBJ/lzma" "$OBJ/common" "$OBJ/stub" "$OBJ/creator"

# /O2 /MT : optimise, link the C runtime statically (no VC++ redistributable
#           needed on the target PC). Z7_ST : single-threaded LZMA SDK.
CFLAGS="/nologo /O2 /MT /W3 /DZ7_ST /DAOFX_NO_SIMD /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE \
-Wno-nonportable-include-path -Wno-ignored-pragma-intrinsic -Wno-ignored-attributes -Wno-pragma-pack -Wno-unknown-pragmas -Wno-microsoft-anon-tag \
/I third_party/lzma /I src/common"

compile() { # compile <source> <object>
  $CL_CMD $CFLAGS /c "$1" "/Fo$2"
}

# --- LZMA SDK pieces (public domain, vendored in third_party/lzma) --------
LZMA_COMMON="Alloc CpuArch"
for f in $LZMA_COMMON LzmaDec Lzma2Dec; do compile "third_party/lzma/$f.c" "$OBJ/lzma/dec_$f.obj"; done
for f in LzFind LzmaEnc Lzma2Enc;       do compile "third_party/lzma/$f.c" "$OBJ/lzma/enc_$f.obj"; done
compile src/common/aofx_container.c "$OBJ/common/aofx_container.obj"

# --- Stage 1: the extractor stub (GUI program, no console window) ---------
compile src/stub/stub.c          "$OBJ/stub/stub.obj"
compile src/stub/lzma2_decode.c  "$OBJ/stub/lzma2_decode.obj"
$LINK_CMD /nologo /subsystem:windows /opt:ref /opt:icf \
  /manifest:embed /manifestuac:no /manifestinput:src/stub/stub.manifest \
  "$OBJ/stub/stub.obj" "$OBJ/stub/lzma2_decode.obj" "$OBJ/common/aofx_container.obj" \
  "$OBJ"/lzma/dec_*.obj \
  kernel32.lib user32.lib shell32.lib \
  /out:"$OUT/aofx_stub.exe"

# --- Stage 2: embed the stub in the creator (console program) -------------
# The array is generated with od+sed (coreutils/sed: always available) instead
# of xxd, which is not installed everywhere. Symbol names are fixed:
# aofx_stub_exe (the bytes) and aofx_stub_exe_len (their count).
{
  echo "unsigned char aofx_stub_exe[] = {"
  od -An -v -tu1 "$OUT/aofx_stub.exe" | sed 's/^ *//; s/  */,/g; s/$/,/'
  echo "};"
  echo "unsigned int aofx_stub_exe_len = $(stat -c%s "$OUT/aofx_stub.exe");"
} > "$OUT/stub_data.c"
compile "$OUT/stub_data.c"          "$OBJ/creator/stub_data.obj"
compile src/creator/main.c          "$OBJ/creator/main.obj"
compile src/creator/lzma2_encode.c  "$OBJ/creator/lzma2_encode.obj"
$LINK_CMD /nologo /subsystem:console /opt:ref /opt:icf \
  "$OBJ/creator/main.obj" "$OBJ/creator/lzma2_encode.obj" "$OBJ/creator/stub_data.obj" \
  "$OBJ/common/aofx_container.obj" "$OBJ"/lzma/enc_*.obj "$OBJ/lzma/dec_Alloc.obj" "$OBJ/lzma/dec_CpuArch.obj" \
  kernel32.lib \
  /out:"$OUT/auto-office.exe"

ls -l "$OUT/auto-office.exe" "$OUT/aofx_stub.exe"
