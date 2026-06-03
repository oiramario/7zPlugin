//-------------------------------------------------------------------
// 7zArchive.cpp -- LZMA SDK wrapper for .7z file operations
//
// Thin C++ wrapper around the 7z LZMA SDK (SzArEx_* API).
// Handles opening, file enumeration, and extraction to memory.
//-------------------------------------------------------------------

#include "7zArchive.h"
#include <stdio.h>   // sprintf_s (via Debug7zTrace)
#include <stdlib.h>  // malloc, free
#include <string.h>  // memcpy


//-------------------------------------------------------------------
// Custom allocators -- forward to the CRT heap
//-------------------------------------------------------------------

static void* _SzAlloc(ISzAllocPtr p, size_t size)
{
    (void)p;
    return malloc(size);
}

static void _SzFree(ISzAllocPtr p, void* addr)
{
    (void)p;
    free(addr);
}

const ISzAlloc SevenZArchive::s_alloc = { _SzAlloc, _SzFree };


//-------------------------------------------------------------------
// Constructor / Destructor
//-------------------------------------------------------------------

SevenZArchive::SevenZArchive()
{
    SzArEx_Init(&db_);
    FileInStream_CreateVTable(&fileStream_);
    LookToRead2_CreateVTable(&lookStream_, 0);
}

SevenZArchive::~SevenZArchive()
{
    if (!isOpen_)
        return;

    SzArEx_Free(&db_, &s_alloc);
    File_Close(&fileStream_.file);

    if (lookStream_.buf)
        ISzAlloc_Free(&s_alloc, lookStream_.buf);
    ISzAlloc_Free(&s_alloc, outBuffer_);
}


//-------------------------------------------------------------------
// resetCache -- clear the SzArEx_Extract solid-block cache state
//
// Called (after freeing outBuffer_) on extraction failure so that
// subsequent calls start with a clean cache slot.
//-------------------------------------------------------------------

void SevenZArchive::resetCache()
{
    blockIndex_    = (UInt32)-1;
    outBuffer_     = nullptr;
    outBufferSize_ = 0;
}


//-------------------------------------------------------------------
// open -- open a .7z file and parse its headers
//
// Allocates a 1 MB look-ahead buffer for the SDK, opens the file,
// and parses the archive headers into db_.  On success isOpen_ is
// set; the destructor uses this flag to decide whether cleanup is
// needed.
//-------------------------------------------------------------------

bool SevenZArchive::open(const char* pszFile)
{
    Debug7zTrace("SevenZArchive::open: %s", pszFile);

    if (InFile_Open(&fileStream_.file, pszFile) != 0) {
        Debug7zTrace("SevenZArchive::open: InFile_Open FAILED");
        return false;
    }

    const size_t kBufSize = (size_t)1 << 20; // 1 MB look-ahead buffer
    lookStream_.buf = (Byte*)ISzAlloc_Alloc(&s_alloc, kBufSize);
    if (!lookStream_.buf) {
        Debug7zTrace("SevenZArchive::open: lookup buffer alloc FAILED");
        File_Close(&fileStream_.file);
        return false;
    }
    lookStream_.bufSize = kBufSize;

    LookToRead2_INIT(&lookStream_);
    lookStream_.realStream = &fileStream_.vt;
    fileStream_.wres = 0;

    SRes res = SzArEx_Open(&db_, &lookStream_.vt, &s_alloc, &s_alloc);
    if (res != SZ_OK) {
        Debug7zTrace("SevenZArchive::open: SzArEx_Open FAILED res=%d", res);
        SzArEx_Free(&db_, &s_alloc);
        ISzAlloc_Free(&s_alloc, lookStream_.buf);
        File_Close(&fileStream_.file);
        return false;
    }

    // Count non-directory entries (fileCount is used by GetArchiveInfo
    // to pre-allocate the file-listing vector).
    fileCount_ = 0;
    for (UInt32 i = 0; i < db_.NumFiles; i++)
        if (!SzArEx_IsDir(&db_, i))
            fileCount_++;

    isOpen_ = true;
    Debug7zTrace("SevenZArchive::open: OK, %u entries (%u files)",
                 db_.NumFiles, fileCount_);
    return true;
}


//-------------------------------------------------------------------
// getEntryInfo -- read entry metadata by archive index
//
// Fills ArchiveEntry with name (converted from UTF-16 to ANSI via
// CP_ACP), uncompressed size, CRC32, modification time, and
// folder/ file flag.
//-------------------------------------------------------------------

bool SevenZArchive::getEntryInfo(UInt32 index, ArchiveEntry* pEntry) const
{
    if (!isOpen_ || !pEntry || index >= db_.NumFiles) {
        Debug7zTrace("SevenZArchive::getEntryInfo: invalid args"
                     " (isOpen=%d index=%u)", isOpen_, index);
        return false;
    }

    ZeroMemory(pEntry, sizeof(*pEntry));

    // Convert filename: UTF-16 -> ANSI (CP_ACP)
    {
        size_t len = SzArEx_GetFileNameUtf16(&db_, index, NULL);
        if (len == 0 || len > MAX_PATH) {
            Debug7zTrace("SevenZArchive::getEntryInfo:"
                         " filename too long (len=%zu index=%u)", len, index);
            return false;
        }

        wchar_t nameBuf[MAX_PATH];
        SzArEx_GetFileNameUtf16(&db_, index, (UInt16*)nameBuf);
        WideCharToMultiByte(CP_ACP, 0, nameBuf, -1,
                            pEntry->szName, MAX_PATH, NULL, NULL);
        pEntry->szName[MAX_PATH - 1] = '\0';
    }

    pEntry->nSize     = SzArEx_GetFileSize(&db_, index);
    pEntry->bIsFolder = SzArEx_IsDir(&db_, index) ? TRUE : FALSE;

    if (SzBitWithVals_Check(&db_.CRCs, index))
        pEntry->dwCRC = db_.CRCs.Vals[index];

    if (SzBitWithVals_Check(&db_.MTime, index)) {
        pEntry->ftModified.dwLowDateTime  = db_.MTime.Vals[index].Low;
        pEntry->ftModified.dwHighDateTime = db_.MTime.Vals[index].High;
    }

    return true;
}


//-------------------------------------------------------------------
// extractToMemory -- decompress an entry into the caller's buffer
//
// SzArEx_Extract manages its own solid-block cache (outBuffer_ /
// blockIndex_) internally.  On success the result is memcpy'd from
// that cache into outputBuffer.  On failure the internal buffer is
// freed and the cache state is reset so future calls start fresh.
//-------------------------------------------------------------------

bool SevenZArchive::extractToMemory(UInt32 index,
                                    unsigned char* outputBuffer,
                                    size_t* pOutSize,
                                    int (__stdcall* pfnProgress)(void*, int, int),
                                    void* pParam)
{
    Debug7zTrace("SevenZArchive::extractToMemory: index=%u", index);

    if (!isOpen_ || !outputBuffer || !pOutSize || index >= db_.NumFiles) {
        Debug7zTrace("SevenZArchive::extractToMemory: invalid args");
        return false;
    }

    size_t offset           = 0;
    size_t outSizeProcessed = 0;

    SRes res = SzArEx_Extract(&db_, &lookStream_.vt,
                              index,
                              &blockIndex_, &outBuffer_, &outBufferSize_,
                              &offset, &outSizeProcessed,
                              &s_alloc, &s_alloc);

    if (res != SZ_OK) {
        Debug7zTrace("SevenZArchive::extractToMemory:"
                     " SzArEx_Extract FAILED res=%d", res);
        ISzAlloc_Free(&s_alloc, outBuffer_);
        resetCache();
        return false;
    }

    if (outSizeProcessed > 0)
        memcpy(outputBuffer, outBuffer_ + offset, outSizeProcessed);

    *pOutSize = outSizeProcessed;

    if (pfnProgress)
        pfnProgress(pParam, 1, 1);

    Debug7zTrace("SevenZArchive::extractToMemory: OK size=%zu",
                 outSizeProcessed);
    return true;
}
