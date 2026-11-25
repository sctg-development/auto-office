#!/bin/bash
# run-tests.sh -- Build and run the native unit tests with the host compiler.
# Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT (MIT)
#
# Usage: test/run-tests.sh [setup.exe config.xml]
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
L=third_party/lzma
# The test uses the same portable sources as the Windows build (no Windows API).
clang -O1 -g -fsanitize=address,undefined -fno-sanitize=alignment -DZ7_ST -DAOFX_NO_SIMD \
  -I $L -I src/common \
  test/roundtrip_test.c src/common/aofx_container.c \
  src/creator/lzma2_encode.c src/stub/lzma2_decode.c \
  $L/Alloc.c $L/CpuArch.c $L/LzFind.c $L/LzmaEnc.c $L/Lzma2Enc.c $L/LzmaDec.c $L/Lzma2Dec.c \
  -o build/roundtrip_test
./build/roundtrip_test "$@"
