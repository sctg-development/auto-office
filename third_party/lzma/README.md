# LZMA SDK (vendored)

Only the files needed for **LZMA2 compression and decompression** of memory
buffers are included. They come from the C folder of 7-Zip / LZMA SDK 26.06
(<https://github.com/ip7z/7zip>, commit `d3296e39e1f1f92c7b41c0c3848e65640428d42d`).

The LZMA SDK is written by Igor Pavlov and is **public domain**: every source
file carries the line "Igor Pavlov : Public domain".

| Used by        | Files                                                   |
|----------------|---------------------------------------------------------|
| compressor     | `Lzma2Enc.c`, `LzmaEnc.c`, `LzFind.c`                   |
| decompressor   | `Lzma2Dec.c`, `LzmaDec.c`                               |
| both           | `Alloc.c`, `CpuArch.c` and the headers                  |

## Local modification

`LzFind.c` contains one small patch (search for `AOFX_NO_SIMD`): the optional
SSE4.1/AVX2 code paths do not compile with clang-cl 18, so they can be switched
off with `-DAOFX_NO_SIMD`. The portable C code is used instead; the compressed
output is identical, only a tiny part of the compression step is slower.
The multi-threaded code is not included: the SDK is built with `-DZ7_ST`.
