//-------------------------------------------------------------------
// 7zArchive.h -- LZMA SDK wrapper for .7z archive access
//
// Provides a C++ class (SevenZArchive) that wraps the 7z LZMA SDK
// for opening .7z files, enumerating entries, and extracting data.
//-------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <7z.h>
#include <7zAlloc.h>
#include <7zFile.h>


//===================================================================
// Debug tracing
//
// Logs to OutputDebugStringA when DEBUG_7Z_TRACE is defined.
// The format is "[7zPlugin][<threadId>] <message>".
// In Release builds all calls compile to nothing.
//===================================================================

#ifdef DEBUG_7Z_TRACE
#  define Debug7zTrace(fmt, ...) \
       do { \
           char _t[512]; \
           sprintf_s(_t, "[7zPlugin][%lu] " fmt "\n", \
                     GetCurrentThreadId(), ##__VA_ARGS__); \
           OutputDebugStringA(_t); \
       } while(0)
#else
#  define Debug7zTrace(fmt, ...) ((void)0)
#endif


//===================================================================
// ArchiveEntry -- metadata for a single entry in the archive
//===================================================================

struct ArchiveEntry {
    char      szName[MAX_PATH];  // entry path (ANSI, '/' separators)
    DWORDLONG nSize;             // uncompressed size in bytes
    DWORD     dwCRC;             // CRC32 checksum (0 if unavailable)
    BOOL      bIsFolder;         // TRUE for directory entries
    FILETIME  ftModified;        // last-modified time (UTC)
};


//===================================================================
// SevenZArchive -- LZMA SDK archive wrapper
//
// Manages the lifetime of a parsed .7z archive: open->parse->
// enumerate->extract->close.  Not copyable and not thread-safe
// (see CRITICAL_SECTION in ArchiveHandle for the threading model).
//===================================================================

class SevenZArchive {
public:
    SevenZArchive();
    ~SevenZArchive();

    // --- Archive lifecycle -------------------------------------------

    bool open(const char* pszFile);

    // --- Entry enumeration -------------------------------------------

    UInt32 getEntryCount() const { return isOpen_ ? db_.NumFiles : 0; }
    UInt32 getFileCount()  const { return fileCount_; }
    bool getEntryInfo(UInt32 index, ArchiveEntry* pEntry) const;

    // --- Extraction --------------------------------------------------

    bool extractToMemory(UInt32 index,
                         unsigned char* outputBuffer, size_t* pOutSize,
                         int (__stdcall *pfnProgress)(void*, int, int),
                         void* pParam);

private:
    void resetCache();

    static const ISzAlloc s_alloc;

    // LZMA SDK state
    bool          isOpen_      = false;
    UInt32        fileCount_   = 0;
    CSzArEx       db_          = {};  // parsed archive database
    CFileInStream fileStream_  = {};  // input file handle + vtable
    CLookToRead2  lookStream_  = {};  // look-ahead buffered reader

    // Solid-block extraction cache (managed by SzArEx_Extract)
    UInt32 blockIndex_        = (UInt32)-1;
    Byte*  outBuffer_         = nullptr;
    size_t outBufferSize_     = 0;
};
