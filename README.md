# auto-office

Build a **self-extracting Office installer** with a single command.
`auto-office.exe` is a small Windows (x86-64) program that packs Microsoft's
*Office Deployment Tool* (`setup.exe`) and an Office configuration XML into one
`.exe` compressed with **LZMA2**.

```text
auto-office.exe --odt-setup .\setup.exe --xml office-2024.xml --out office-2024.exe
```

When a user double-clicks `office-2024.exe` they only see the Windows
elevation prompt and then the Office installation itself. Behind the scenes the
generated file:

1. extracts `setup.exe` and the XML into a new private folder in `%TEMP%`,
2. runs `setup.exe /configure configuration.xml` and waits for it to finish,
3. deletes the temporary folder and returns setup's exit code.

It is also a small, readable example of how a self-extracting archive works, so
the code is heavily commented for students.

## Command line

| Option                | Meaning                                                  |
|-----------------------|----------------------------------------------------------|
| `--odt-setup <file>`  | Office Deployment Tool `setup.exe` to embed              |
| `--xml <file>`        | Office configuration XML to embed                        |
| `--out <file>`        | self-extracting executable to create                     |
| `--level <0-9>`       | LZMA2 compression level, default `9` (smallest)         |
| `--help`              | show the help                                            |

The Office Deployment Tool can be downloaded from Microsoft
(search for "Office Deployment Tool"); the configuration XML can be written by
hand or generated with <https://config.office.com>.

## Building

Everything is cross-compiled **on Linux** with the
[`highcanfly/llvm4msvc`](https://github.com/highcanfly-club/llvm4msvc) toolchain
(clang-cl + lld-link + the real Windows SDK and MSVC runtime downloaded by
[xwin](https://github.com/Jake-Shadle/xwin)). No Windows machine is needed.

### Inside the hcf-coder development image

The toolchain is already installed, so just run:

```sh
./build.sh
```

### With the Docker image directly

From the root of this repository:

```sh
docker run --rm -v "$PWD":/src -w /src highcanfly/llvm4msvc:latest ./build.sh
```

(on Windows PowerShell replace `"$PWD"` with `${PWD}`).

The result is `build/auto-office.exe`, a single file with no dependency other
than Windows itself (the C runtime is linked statically).

### What `build.sh` does

`auto-office.exe` embeds the extractor program (the *stub*), so the build has
two stages:

1. compile the stub (`src/stub`) into `build/aofx_stub.exe`, a GUI program
   without console window and with a manifest that requests administrator
   rights (Office setup needs them; the elevation prompt appears once and
   `setup.exe` inherits it);
2. convert the stub into a C array (`build/stub_data.c`) and compile the creator
   (`src/creator`) which contains it.

`$MSVC_CL` and `$MSVC_LINK` (defined by `/usr/local/bin/msvc.env`) are the
`clang-cl` and `lld-link` command lines. The variables are deliberately **not**
named `CL` and `LINK`: those names are read by the tools themselves.

## How it works

Windows ignores any bytes placed after the last section of an `.exe`, so the
generated file is simply three parts glued together:

```text
+--------------------------+
| stub (a normal program)  |  <- the only part Windows runs
+--------------------------+
| LZMA2 compressed block   |  <- setup.exe + configuration.xml
+--------------------------+
| footer (fixed size)      |  <- magic, sizes, CRC-32, LZMA2 properties
+--------------------------+
```

At run time the stub opens its own file, reads the footer from the last bytes,
decompresses the block located just before it, verifies the CRC-32 and
extracts the files. The full format is documented in
[`src/common/aofx_format.h`](src/common/aofx_format.h).

Security notes (worth discussing with students):

* the archive comes from a file we do not control, so every size and offset is
  checked before use and file names are validated (no `..`, no `\`, no `:`) to
  prevent path traversal ("zip slip");
* the temporary folder is created with `CreateDirectory`, which fails if the
  folder already exists, so nobody can pre-create it to hijack our files;
* the generated file is **not signed**. Windows SmartScreen may warn about it;
  sign it with `signtool`/`osslsigncode` if you distribute it.

## Source layout

| Path                              | Content                                              |
|-----------------------------------|------------------------------------------------------|
| `src/creator/main.c`              | `auto-office.exe`: command line, writes the output   |
| `src/creator/lzma2_encode.c`      | LZMA2 compression wrapper                            |
| `src/stub/stub.c`                 | extractor: unpack, run setup, clean up               |
| `src/stub/lzma2_decode.c`         | LZMA2 decompression wrapper                          |
| `src/stub/stub.manifest`          | Windows manifest of the stub                         |
| `src/common/`                     | archive format, CRC-32, container code (portable C)  |
| `third_party/lzma/`               | LZMA SDK sources (public domain), see its README     |
| `test/`                           | native unit tests                                    |

## Tests

The portable part (container, CRC-32, LZMA2, rejection of corrupt or malicious
data) is tested on Linux with AddressSanitizer:

```sh
test/run-tests.sh                              # synthetic data
test/run-tests.sh setup.exe office-2024.xml    # plus your real files
```

## License

MIT, see [LICENSE](LICENSE).
Copyright (c) 2026 SCTG Development - Ronan LE MEILLAT.

The LZMA SDK in `third_party/lzma` is public domain (Igor Pavlov).
Microsoft Office and the Office Deployment Tool are Microsoft products and are
not included in this repository: you need your own licenses.
