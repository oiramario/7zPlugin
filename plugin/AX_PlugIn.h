//-------------------------------------------------------------------
// AX_PlugIn.h
//-------------------------------------------------------------------
// ACD SYSTEMS INTERNATIONAL INC.
// Copyright (c) 1994 - 2020 ACD Systems International Inc.
// All Rights Reserved
//
// Header containing datatypes and functions for ACDSee Archive
// Handling (AX) plug-ins.
//
// The plug-in must export the following functions:
//
//   AMP_Init              - Initialize the plug-in
//   AMP_GetPlugInInfo     - Get plug-in / format information
//   AMP_OpenArchive       - Open an archive for browsing/extraction
//   AMP_GetArchiveInfo    - Get archive content listing
//   AMP_FileDecode        - Extract a file from the archive
//   AMP_CloseArchive      - Close the archive
//
// The following functions are optional:
//
//   AMP_FileDecodeStart   - Begin incremental file extraction
//   AMP_FileDecodeStep    - Continue incremental extraction
//   AMP_FileDecodeStop    - Finish/cancel incremental extraction
//   AMP_ShowPlugInDialog  - Show plug-in about/configuration dialog
//-------------------------------------------------------------------

#include <windows.h>

#ifndef _AX_PLUGIN_H_INCLUDED
#define _AX_PLUGIN_H_INCLUDED

/////////////////
// MAKE_FORMATID
/////////////////
#ifndef MAKE_FORMATID
#define MAKE_FORMATID(a,b,c,d) (((DWORD)(d)<<24) | ((DWORD)(c)<<16) | ((DWORD)(b)<<8) | ((DWORD)(a)))
#endif

/////////////////////
// Plug-in version //
/////////////////////
#define AMP_VERSION 100

//////////////////
// Status codes //
//////////////////
enum
{
   AMPE_OK                 =  0,  // Success
   AMPE_Error              = -1,  // Unspecified error
   AMPE_InvalidOp          = -2,  // Operation cannot be completed at this time
   AMPE_InvalidParam       = -3,  // Invalid parameters
   AMPE_NotImplemented     = -4,  // Missing implementation
   AMPE_Abort              = -5,  // Operation cancelled by user
   AMPE_InvalidFormat      = -6,  // Invalid or corrupt archive
   AMPE_FileNotFound       = -7,  // File not found in archive
   AMPE_Malloc             = -8,  // Memory allocation error
   AMPE_Unsupported        = -9,  // Unsupported compression method
   AMPE_CRCError           = -10, // CRC checksum error
   AMPE_ReadError          = -11, // Read error
   AMPE_WriteError         = -12, // Write error
   AMPE_FileExists         = -13, // Destination file already exists

   // Plug-in defined errors start at -1000
   AMPE_FirstPlugInError   = -1000,
};

/////////////////
// APF_* flags //
/////////////////
// AMP_PlugInInfo flags
#define APF_CANENCODE      (1<<0)  // Plug-in can create archives
#define APF_CANADD         (1<<1)  // Plug-in can add files to archives
#define APF_CANDELETE      (1<<2)  // Plug-in can delete files from archives
#define APF_CANRENAME      (1<<3)  // Plug-in can rename files in archives
#define APF_CANEXTRACT     (1<<4)  // Plug-in can extract files
#define APF_STREAMDECODE   (1<<5)  // Plug-in supports Start/Step/Stop streaming
#define APF_FOLDERS        (1<<6)  // Plug-in supports folders in archives
#define APF_PASSWORD       (1<<7)  // Plug-in supports password-protected archives

/////////////////
// AAF_* flags //
/////////////////
// AMP_ArchiveInfo flags
#define AAF_HASCOMMENTS    (1<<0)  // Archive has embedded comments
#define AAF_ENCRYPTED      (1<<1)  // Archive is encrypted
#define AAF_MULTIVOLUME    (1<<2)  // Archive is multi-volume
#define AAF_SOLID          (1<<3)  // Archive uses solid compression

/////////////////
// AFF_* flags //
/////////////////
// AMP_FileInfo flags
#define AFF_ISFOLDER       (1<<0)  // Entry is a directory
#define AFF_ENCRYPTED      (1<<1)  // Entry is encrypted
#define AFF_CRC32          (1<<2)  // dwCRC field is valid

/////////////////////
// AMP_FileInfo //
/////////////////////
// Information about a single file in the archive
// NOTE: Layout determined via RAR plugin binary analysis; each entry is 0x170 bytes.
struct AMP_FileInfo
{
   AMP_FileInfo() { ZeroMemory(this, sizeof(*this)); };

   DWORD    dwFlags;            // [0x00] AFF_* flags
   BYTE     __pad0[0x28];       // [0x04-0x2B] reserved / unknown
   DWORD     index;             // [0x2C] internal file index
   DWORD    nSize;              // [0x30] Uncompressed size (32-bit - RAR 0x1f81, ZIP 0x448d)
   DWORD    nPackedSize;        // [0x34] Packed size (32-bit - RAR 0x1fcb, ZIP 0x44ad)
   FILETIME ftModified;         // [0x38] Last modified time (UTC)
   char     szName[MAX_PATH];   // [0x40] Full path of entry within archive
   DWORD    dwCRC;              // [0x144] CRC32 checksum
   char     szTemplateDir[0x28]; // [0x148] template mode: ACDSee writes output dir here
};

////////////////////////
// AMP_ArchiveInfo //
////////////////////////
// Information about an open archive
// NOTE: Layout confirmed via ZIP/RAR binary analysis.
//       Both write exactly 12 bytes: dwFlags@+0x00, nFiles@+0x04, pFiles@+0x08.
//       The allocated sub-struct is only 0xC bytes.
struct AMP_ArchiveInfo
{
   DWORD    dwFlags;       // [0x00] AAF_* flags
   int      nFiles;        // [0x04] Number of files in archive
   AMP_FileInfo* pFiles;   // [0x08] File listing array
};

///////////////////
// ID_FormatInfo //
///////////////////
// Specifies information about an image format supported bythe plug-in.
// The plug-in owns the memory pointed to by pszExtList and must not free it
// until the plug-in library is unloaded.
struct ID_FormatInfo
{
    DWORD       dwFlags;        // FIF_* flags
    DWORD       dwID;           // unique identifier for this format
    char        szName[40];     // name of this format (e.g., "Windows BMP")
    char        szNameShort[8]; // short name of this format (e.g., "BMP")
    char*       pszExtList;     // list of filename extensions for this format (e.g., "BMP\0DIB\0RLE\0")
    char        szDefExt[8];    // default extension for this format (e.g., "BMP")
    COLORREF    color;          // background colour to use when highlighting
    UINT        iIcon;          // index of icon to display in Explorer
    char*       pszMimeType;    // MIME type name
};

///////////////////////
// AMP_PlugInInfo //
///////////////////////
// Format information returned by AMP_GetPlugInInfo
struct AMP_PlugInInfo
{
    DWORD            dwFlags;     // Flags -- set to 0
    int              nVersion;    // Plug-in specification version (ID_VERSION)
    char             szTitle[40]; // Plug-in title
    UINT             iIcon;       // Plug-in icon resource id
    int              nFormats;    // Number of formats supported by plug-in
    ID_FormatInfo*   pFormatInfo; // Information about each format supported
};

///////////////////
// AMP_ClientInfo //
///////////////////
struct AMP_ClientInfo
{
   DWORD    dwFlags;
   char     szCompany[40];
   char     szAppName[40];
   int      nVersion;
};

struct AMP_OpenArchiveParams {
    DWORD       dwFlags;        // [0x00] 1=probe, 0=real open
    const char* pszFile;        // [0x04] file path
    void*       pfnCallback;    // [0x08] callback fn  (valid when dwFlags & 1)
    void*       pvCallbackCtx;  // [0x0C] callback ctx (valid when dwFlags & 1)
};

#endif // _AX_PLUGIN_H_INCLUDED
