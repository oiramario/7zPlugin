# AX_7Z -- 7z Archive Plugin for ACDSee (3.1 / Pro 5)

A Win32 plug-in (`.apl` DLL) that adds native **7z archive** support to **ACDSee**. Implements the ACDSee AX (Archive eXtension) plugin interface using the [LZMA SDK 26.01](https://www.7-zip.org/sdk.html) for decompression.

> ✅ **Tested and confirmed working on:**
> - **ACDSee 3.1** (32-bit)
> - **ACDSee Pro 5** (32-bit)

---

## Features

- **Full archive browsing** -- Open and list `.7z` files directly in ACDSee's Browser/Manage mode, with file names, sizes, CRC32 checksums, and modification timestamps.
- **On-demand extraction** -- Decode individual files for preview, viewing, or extraction, supporting both **memory** (ACDSee-owned buffer) and **file** (temp file on disk) output modes.
- **LRU decode cache** -- Repeated previews of the same file (common during scrolling) hit an in-memory 8-entry cache rather than re-decompressing from the solid archive block.
- **Solid archive compatible** -- Uses the LZMA SDK `SzArEx_Extract` API which handles solid-block extraction transparently.
- **Thread-safe** -- Each `OpenArchive` call creates its own `ArchiveHandle` with an independent `SevenZArchive` and a dedicated critical section; no shared mutable state between handles.
- **Lightweight** -- Single DLL (~120 KB Release build), no external dependencies beyond the LZMA SDK (compiled in as a static library).

## Architecture

```
archive/
  7zArchive.h/.cpp    C++ SevenZArchive -- open, parse, enumerate, extract via LZMA SDK
  lzma/C/             LZMA SDK 26.01 (C sources, compiled as static lib "lzma")
  lzma/DOC/           LZMA SDK documentation (format spec, method descriptions)
plugin/
  AX_7z.cpp           ACDSee AX exports -- thin delegation to g_p7zPlugin
  7zPlugin.h/.cpp     Plugin logic: SevenZipPlugin, ArchiveHandle (LRU cache), decode pipeline
  AX_PlugIn.h         ACDSee AX plugin interface (reverse-engineered structs & function signatures)
  dllmain.cpp         DLL entry point -- CRC table init, module handle, cleanup on detach
  AX_7z.def           DLL export definitions
  res/                Icon and version resources
```

### Data flow

```
ACDSee Pro
    v AMP_* exports
AX_7z.cpp  (thin delegation)
    v
SevenZipPlugin  (handle management, LRU cache, decode routing)
    v
SevenZArchive   (LZMA SDK wrapper -- open, enumerate, extract)
    v
LZMA SDK (SzArEx_*)  ->  decompressed data
```

### Exports

| Export                | Status | Notes                                      |
|-----------------------|--------|--------------------------------------------|
| `AMP_Init`            | ✓      | Creates global `SevenZipPlugin` instance   |
| `AMP_GetPlugInInfo`   | ✓      | Returns format info for `.7z`              |
| `AMP_OpenArchive`     | ✓      | Opens `.7z`, returns opaque handle         |
| `AMP_GetArchiveInfo`  | ✓      | Returns file listing (filters directories) |
| `AMP_FileDecode`      | ✓      | Extracts one file (MEM or FILE mode)       |
| `AMP_CloseArchive`    | ✓      | Frees handle and its LRU cache             |
| `AMP_ShowPlugInDialog`| ✓      | About dialog with version info             |

Streaming exports (`FileDecodeStart`/`Step`/`Stop`) are **not** implemented -- the plugin uses the single-call `FileDecode` path, matching the approach used by ACDSee's own RAR plugin.

### Key design decisions

- **Per-handle isolation**: Each `OpenArchive` call creates an independent `ArchiveHandle` containing its own `SevenZArchive`. `FileDecode` operates on the same handle and serialises access via a per-handle critical section.
- **LRU decode cache**: Archives frequently have the same file decoded 5-15 times during a preview session. A fixed-size (8-entry) LRU cache on each `ArchiveHandle` makes repeated accesses instantaneous.
- **Folder exclusion**: Directory entries are filtered out of the file listing in `GetArchiveInfo`; exposing them causes ACDSee to prompt for a password when it tries to decode them.
- **Path normalization**: 7z stores paths with `/`; ACDSee expects `\`. Conversion happens in `GetArchiveInfo`.
- **Packed size fallback**: 7z solid archives don't track per-file compressed size. The plugin reports `nPackedSize = nSize` (uncompressed size) since the SDK's folder-based "packed size" is identical for all files in a solid block.
- **Per-decode private extraction**: `FileDecode` uses the shared `SevenZArchive` under a critical section. Each call allocates a private output buffer (MEM mode writes directly to ACDSee's buffer; FILE mode writes to a temp file after extracting to an internal buffer).

## Requirements

- **OS**: Windows (tested on Windows 10/11)
- **Host**: ACDSee 3.1 / Pro 5 (32-bit) -- tested and confirmed working on both
- **Build**: Visual Studio 2022 with C++ workload, CMake >= 3.15
- **Target**: **x86 (32-bit)** Win32 DLL -- this is a hard constraint from ACDSee Pro's 32-bit plugin host.

## Build

```bat
# Quick build + deploy (run as Admin for deploy step)
build_cmake.bat

# Manual steps
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release

# Build specific target
cmake --build build --config Release --target AX_7Z

# Enable debug tracing (OutputDebugString)
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DDEBUG_7Z_TRACE=ON
```

**Output**: `build\bin\Release\AX_7Z.apl`
**Deploy**: `%ProgramFiles(x86)%\ACDSee Pro\PlugIns\AX_7Z.apl`

## Debugging

The plugin writes diagnostic output via `OutputDebugStringA`, visible in **DebugView** or **WinDbg**. All lines are prefixed with `[7zPlugin][<threadId>]`.

The `Debug7zTrace(fmt, ...)` macro (defined in `archive/7zArchive.h`) compiles to a no-op in Release builds. Enable it by configuring with `-DDEBUG_7Z_TRACE=ON`.

## Technical details

- **`AMP_OpenArchiveParams` / `AMP_FileInfo`** layout reverse-engineered from ACDSee's ZIP and RAR plugins via binary analysis.
- **`AMP_Extended`** decode parameter block -- struct with two output modes: `pszOutputFile` (FILE mode, write to temp file) or `pBuffer` (MEM mode, copy to ACDSee-owned buffer). Exactly one must be non-null.
- **`fi.index`** stores the **original 7z archive index**, not the filtered array position -- critical for correct extraction from archives with mixed file/folder entries.
- **Per-archive CRITICAL_SECTION**: Each `ArchiveHandle` has its own critical section guarding both the `SevenZArchive` (SzArEx_Extract has mutable internal cache state) and the LRU cache. No global lock is needed.

## License

The LZMA SDK is in the public domain (see `archive/lzma/C/7zCrc.h` and related files). The plugin wrapper code is available under the MIT license.
