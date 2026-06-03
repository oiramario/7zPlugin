//-------------------------------------------------------------------
// 7zPlugin.cpp - ACDSee AX plugin for 7z archives
//
// Implements the ACDSee AMP plugin interface for .7z file browsing
// and extraction via the LZMA SDK (SzArEx_* API).
//-------------------------------------------------------------------

#include "7zPlugin.h"
#include "7zArchive.h"
#include "res/resource.h"

#include <new>       // std::nothrow
#include <stdio.h>   // sprintf_s (via Debug7zTrace)
#include <stdlib.h>  // malloc, free
#include <string.h>  // strrchr, memcpy
#include <strsafe.h> // StringCchCopyA
#include <vector>


//===================================================================
// ArchiveHandle -- per-open-archive context
//
// Holds the LZMA archive handle (SevenZArchive), the file listing
// vector, a critical section for thread safety, and an LRU decode
// cache that avoids redundant decompression of the same file.
//
// Each ArchiveHandle is returned as an opaque `void*` to ACDSee.
//===================================================================

struct ArchiveHandle {
    static constexpr size_t kDecodeCacheSize = 8;

    SevenZArchive             arch;      // LZMA SDK archive (open, seek, extract)
    std::vector<AMP_FileInfo> files;     // file listing built by GetArchiveInfo
    CRITICAL_SECTION          cs;        // guards arch + cache
    uint32_t                  lruSeqGen = 0;  // monotonic LRU timestamp

    // A cache entry holds the decompressed data of one file.
    // `index` is the original 7z archive index; `data`/`size` is the result.
    // -1 in `index` marks an empty slot.
    struct CacheEntry {
        UInt32        index = (UInt32)-1;
        unsigned char* data = nullptr;
        size_t        size  = 0;
        uint32_t      lruSeq = 0;
    } cache[kDecodeCacheSize];

    ArchiveHandle() { InitializeCriticalSection(&cs); }

    ~ArchiveHandle() {
        for (auto& c : cache)
            free(c.data);
        DeleteCriticalSection(&cs);
    }

    // --- LRU helpers (must be called while holding cs) ---

    // Find a cache entry by archive index, or return nullptr.
    // On hit, updates the entry's LRU timestamp.
    CacheEntry* cacheFind(UInt32 idx) {
        for (auto& c : cache)
            if (c.index == idx) {
                c.lruSeq = lruSeqGen++;
                return &c;
            }
        return nullptr;
    }

    // Insert (index, data, size) into the cache, evicting the
    // least-recently-used entry if the cache is full.
    // Takes ownership of `data` -- caller must not free it afterwards.
    CacheEntry* cacheInsert(UInt32 idx, unsigned char* data, size_t size) {
        // Use an empty slot if one exists
        for (auto& c : cache)
            if (c.index == (UInt32)-1) {
                c.index  = idx;
                c.data   = data;
                c.size   = size;
                c.lruSeq = lruSeqGen++;
                return &c;
            }

        // Evict the entry with the smallest lruSeq
        CacheEntry* victim = &cache[0];
        for (auto& c : cache)
            if (c.lruSeq < victim->lruSeq)
                victim = &c;

        free(victim->data);
        Debug7zTrace("ArchiveHandle: EVICT idx=%u (lru=%u) for idx=%u",
                     victim->index, victim->lruSeq, idx);

        victim->index  = idx;
        victim->data   = data;
        victim->size   = size;
        victim->lruSeq = lruSeqGen++;
        return victim;
    }
};


//===================================================================
// AMP_Extended -- decode-mode parameter block
//
// Reverse-engineered from the ACDSee RAR plugin binary.
// ACDSee encodes the output target in a DWORD[4] array:
//   [0] reserved (unused)
//   [1] pszOutputFile  -- FILE mode: write to this path
//   [2] pBuffer        -- MEM  mode: write to this buffer
//   [3..4] progress callback (always null in practice)
//
// Exactly one of [1] or [2] must be non-null.
//===================================================================

struct AMP_Extended {
    DWORD          reserved;       // [0]  unused
    const char*    pszOutputFile;  // [1]  FILE mode output path
    unsigned char* pBuffer;        // [2]  MEM mode output buffer
    void*          pfnProgress;    // [3]  always null (progress callback)
    void*          pvContext;      // [4]  always null
};


//-------------------------------------------------------------------
// Static members
//-------------------------------------------------------------------

HINSTANCE SevenZipPlugin::g_hModule = NULL;


//-------------------------------------------------------------------
// Constructor -- initialises format and plug-in info blocks
//-------------------------------------------------------------------

SevenZipPlugin::SevenZipPlugin()
    : m_FormatInfo{
        .dwFlags     = APF_CANEXTRACT,
        .dwID        = MAKE_FORMATID('7', 'z', 0, 0),
        .szName      = "7z Archive",
        .szNameShort = "7z",
        .pszExtList  = (char*)"7z\0\0",
        .szDefExt    = "7z",
        .color       = 0,
        .iIcon       = 0,
        .pszMimeType = nullptr
    },
    m_PlugInInfo{
        .dwFlags     = 0,
        .nVersion    = AMP_VERSION,
        .szTitle     = "7-Zip Archive",
        .iIcon       = 0,
        .nFormats    = 1,
        .pFormatInfo = &m_FormatInfo
    }
{
}


//-------------------------------------------------------------------
// OpenArchive -- open .7z file and return an opaque archive handle
//
// dwFlags == 1  -> probe call (format detection only)
// dwFlags == 0  -> real open
//-------------------------------------------------------------------

int SevenZipPlugin::OpenArchive(const AMP_OpenArchiveParams* params,
                                void** phArchive)
{
    Debug7zTrace("OpenArchive: dwFlags=%lu pszFile='%s' pfnCallback=%p "
                 "pvCallbackCtx=%p",
                 params ? params->dwFlags : 0,
                 params ? (params->pszFile ? params->pszFile : "(null)")
                        : "(null)",
                 params ? params->pfnCallback : nullptr,
                 params ? params->pvCallbackCtx : nullptr);

    if (!params || !phArchive) {
        Debug7zTrace("OpenArchive: invalid params -> AMPE_InvalidParam");
        return AMPE_InvalidParam;
    }

    ArchiveHandle* ar = new (std::nothrow) ArchiveHandle();
    if (!ar) {
        Debug7zTrace("OpenArchive: new failed -> AMPE_Malloc");
        return AMPE_Malloc;
    }

    if (!ar->arch.open(params->pszFile)) {
        Debug7zTrace("OpenArchive: open FAILED -> AMPE_InvalidFormat");
        delete ar;
        return AMPE_InvalidFormat;
    }

    *phArchive = ar;
    return AMPE_OK;
}


//-------------------------------------------------------------------
// GetArchiveInfo -- enumerate files in the archive
//
// Reads all entries from the parsed LZMA database, filters out
// directories, and builds the AMP_FileInfo array that ACDSee uses
// to display the archive contents.
//
// fi.index stores the original 7z archive index (not the filtered
// position) so that FileDecode can extract by the correct index.
//-------------------------------------------------------------------

int SevenZipPlugin::GetArchiveInfo(void* hArchive, AMP_ArchiveInfo* pInfo)
{
    if (!hArchive || !pInfo) {
        Debug7zTrace("GetArchiveInfo: invalid params -> AMPE_InvalidParam");
        return AMPE_InvalidParam;
    }

    ArchiveHandle* ar = (ArchiveHandle*)hArchive;

    ar->files.clear();

    UInt32 totalCount = ar->arch.getEntryCount();
    UInt32 fileCount  = ar->arch.getFileCount();
    Debug7zTrace("GetArchiveInfo: total=%u files=%u", totalCount, fileCount);

    if (fileCount == 0) {
        pInfo->dwFlags = 0;
        pInfo->nFiles  = 0;
        pInfo->pFiles  = NULL;
        return AMPE_OK;
    }

    try {
        ar->files.reserve(fileCount);
    } catch (const std::bad_alloc&) {
        Debug7zTrace("GetArchiveInfo: bad_alloc -> AMPE_Malloc");
        return AMPE_Malloc;
    }

    ArchiveEntry entry;
    for (UInt32 srcIdx = 0; srcIdx < totalCount; srcIdx++) {
        if (!ar->arch.getEntryInfo(srcIdx, &entry) || entry.bIsFolder)
            continue;

        AMP_FileInfo fi = {};
        StringCchCopyA(fi.szName, MAX_PATH, entry.szName);

        // Normalise path separators: 7z uses '/', ACDSee expects '\'
        for (char* p = fi.szName; *p; p++)
            if (*p == '/') *p = '\\';

        fi.nSize       = (DWORD)entry.nSize;
        fi.nPackedSize = (DWORD)entry.nSize;
        // 7z solid archives don't track per-file compressed size.
        // The SDK's folder-based "packed size" is the whole solid
        // block -- identical for all files in it, thus useless to
        // ACDSee.  Using nSize (= uncompressed size) is the safe
        // choice for buffer allocation and decode-loop termination.

        fi.dwCRC      = entry.dwCRC;
        fi.ftModified = entry.ftModified;
        fi.index      = srcIdx;

        if (entry.dwCRC != 0)
            fi.dwFlags |= AFF_CRC32;

        ar->files.push_back(fi);
    }

    pInfo->dwFlags = 0;
    pInfo->nFiles  = (int)ar->files.size();
    pInfo->pFiles  = ar->files.data();

    Debug7zTrace("GetArchiveInfo: OK -> %d files returned", pInfo->nFiles);
    return AMPE_OK;
}


//-------------------------------------------------------------------
// FileDecode -- extract a single file from the archive
//
// ACDSee calls this for every file it displays.  The same file is
// typically decoded 5-15 times during a preview session; we cache
// the decompressed result to make subsequent calls cheap.
//
// Two output modes, distinguished by the pExtended block:
//   MEM  mode (pBuffer != NULL)  -- copy decoded data to ACDSee's buffer
//   FILE mode (pszOutputFile)    -- write decoded data to a temp file
//
// Thread safety: a CRITICAL_SECTION on ArchiveHandle serialises
// access to the shared SevenZArchive (its SzArEx_Extract path has
// mutable internal cache state) and the LRU cache.
//-------------------------------------------------------------------

int SevenZipPlugin::FileDecode(void* hArchive, AMP_FileInfo* pFileInfo,
                               void* pExtended)
{
    if (!hArchive || !pFileInfo || !pExtended) {
        Debug7zTrace("FileDecode: invalid params -> AMPE_InvalidParam");
        return AMPE_InvalidParam;
    }

    ArchiveHandle* ar = (ArchiveHandle*)hArchive;
    AMP_Extended*  ext = (AMP_Extended*)pExtended;

    Debug7zTrace("FileDecode: name='%s' size=%lu idx=%lu mode=%s",
                 pFileInfo->szName, pFileInfo->nSize, pFileInfo->index,
                 ext->pBuffer ? "MEM" : "FILE");

    // Validate: exactly one of FILE or MEM mode must be active
    bool fileMode  = (ext->pszOutputFile != nullptr);
    bool memMode   = (ext->pBuffer != nullptr);
    if (fileMode == memMode) {  // both or neither -> invalid
        Debug7zTrace("FileDecode: invalid pExtended path=%p buf=%p"
                     " -> AMPE_Abort",
                     ext->pszOutputFile, ext->pBuffer);
        return AMPE_Abort;
    }

    // RAII lock -- guards ar->arch (SzArEx_Extract mutates internal
    // cache state) and ar->cache (LRU metadata + data pointers).
    struct CsLock {
        CRITICAL_SECTION* cs;
        CsLock(CRITICAL_SECTION* c) : cs(c) { EnterCriticalSection(cs); }
        ~CsLock() { LeaveCriticalSection(cs); }
    } lk(&ar->cs);

    const UInt32      archiveIdx = pFileInfo->index;
    size_t            resultSize = 0;
    unsigned char*    memOut     = ext->pBuffer;

    //-----------------------------------------------------------------
    // MEM mode -- ACDSee owns the output buffer
    //-----------------------------------------------------------------
    if (memOut) {
        if (auto* ce = ar->cacheFind(archiveIdx)) {
            // Cache hit: copy directly to ACDSee's buffer
            memcpy(memOut, ce->data, ce->size);
            resultSize = ce->size;
            Debug7zTrace("FileDecode: CACHE HIT idx=%u", archiveIdx);
        } else {
            // Cache miss: extract into ACDSee's buffer, then cache a copy
            Debug7zTrace("FileDecode: CACHE MISS idx=%u, extracting...",
                         archiveIdx);

            if (!ar->arch.extractToMemory(archiveIdx, memOut, &resultSize,
                                          nullptr, nullptr)) {
                Debug7zTrace("FileDecode: extractToMemory FAILED");
                return AMPE_Error;
            }

            unsigned char* copy = (unsigned char*)malloc(resultSize);
            if (copy) {
                memcpy(copy, memOut, resultSize);
                ar->cacheInsert(archiveIdx, copy, resultSize);
                Debug7zTrace("FileDecode: CACHED idx=%u size=%zu",
                             archiveIdx, resultSize);
            }
        }
    }
    //-----------------------------------------------------------------
    // FILE mode -- write decoded data to a temp file
    //-----------------------------------------------------------------
    else {
        char outputPath[MAX_PATH] = {};
        lstrcpynA(outputPath, ext->pszOutputFile, MAX_PATH);

        // Create the directory tree if necessary
        {
            char dir[MAX_PATH];
            lstrcpynA(dir, outputPath, MAX_PATH);
            if (char* pSlash = strrchr(dir, '\\')) {
                *pSlash = '\0';
                // Skip drive-letter prefix (e.g. "C:\") for iteration
                char* pStart = (dir[0] && dir[1] == ':') ? dir + 3 : dir;
                for (char* p = pStart; *p; p++) {
                    if (*p == '\\') {
                        *p = '\0';
                        CreateDirectoryA(dir, NULL);
                        *p = '\\';
                    }
                }
                CreateDirectoryA(dir, NULL);
            }
        }

        // Resolve data: cache hit, or extract + cache
        ArchiveHandle::CacheEntry* cacheEntry = ar->cacheFind(archiveIdx);
        if (!cacheEntry) {
            Debug7zTrace("FileDecode: CACHE MISS idx=%u, extracting...",
                         archiveIdx);

            size_t sz = pFileInfo->nSize ? pFileInfo->nSize : 4096;
            unsigned char* buf = (unsigned char*)malloc(sz);
            if (!buf || !ar->arch.extractToMemory(archiveIdx, buf, &sz,
                                                  nullptr, nullptr)) {
                Debug7zTrace("FileDecode: extract FAILED");
                free(buf);
                return AMPE_Error;
            }
            cacheEntry = ar->cacheInsert(archiveIdx, buf, sz);
            Debug7zTrace("FileDecode: CACHED idx=%u size=%zu",
                         archiveIdx, sz);
        } else {
            Debug7zTrace("FileDecode: CACHE HIT idx=%u", archiveIdx);
        }

        // Write cached data to the output file
        HANDLE hFile = CreateFileA(outputPath,
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ, NULL,
                                   CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            Debug7zTrace("FileDecode: CreateFile('%s') FAILED err=%lu",
                         outputPath, GetLastError());
            return AMPE_WriteError;
        }

        // Retry loop -- WriteFile may complete partially
        const unsigned char* src = cacheEntry->data;
        DWORD remaining = (DWORD)cacheEntry->size;
        while (remaining > 0) {
            DWORD written = 0;
            if (!WriteFile(hFile, src, remaining, &written, NULL)) {
                Debug7zTrace("FileDecode: WriteFile FAILED err=%lu",
                             GetLastError());
                CloseHandle(hFile);
                return AMPE_WriteError;
            }
            src       += written;
            remaining -= written;
        }
        CloseHandle(hFile);
        resultSize = cacheEntry->size;

        Debug7zTrace("FileDecode: wrote %zu bytes to '%s'",
                     cacheEntry->size, outputPath);
    }

    Debug7zTrace("FileDecode: EXIT OK size=%zu", resultSize);
    return AMPE_OK;
}


//-------------------------------------------------------------------
// CloseArchive -- release an archive handle
//-------------------------------------------------------------------

int SevenZipPlugin::CloseArchive(void* hArchive, void* pReserved)
{
    Debug7zTrace("CloseArchive: hArchive=%p", hArchive);
    if (!hArchive)
        return AMPE_InvalidParam;

    (void)pReserved;
    delete (ArchiveHandle*)hArchive;
    return AMPE_OK;
}


//-------------------------------------------------------------------
// ShowPlugInDialog -- display the About dialog
//-------------------------------------------------------------------

void SevenZipPlugin::ShowPlugInDialog(HWND hwndParent)
{
    DialogBoxA(g_hModule,
               MAKEINTRESOURCE(IDD_ABOUT),
               hwndParent,
               AboutDlgProc);
}


INT_PTR CALLBACK SevenZipPlugin::AboutDlgProc(HWND hDlg, UINT message,
                                              WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (message) {
    case WM_INITDIALOG: {
        char version[64] = {};
        LoadStringA(g_hModule, IDS_VERSION, version, sizeof(version));
        SetDlgItemTextA(hDlg, IDC_VERSION, version);
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}
