//-------------------------------------------------------------------
// AX_7z.cpp -- ACDSee AX plug-in exports
//
// Thin delegation layer: every exported AMP_* function forwards to
// the global SevenZipPlugin instance.  All real logic lives in
// 7zPlugin.cpp / 7zArchive.cpp.
//-------------------------------------------------------------------

#include "AX_PlugIn.h"
#include "7zPlugin.h"

SevenZipPlugin* g_p7zPlugin = nullptr;


extern "C" int __stdcall AMP_Init(AMP_ClientInfo* /*pci*/)
{
    if (g_p7zPlugin == nullptr)
        g_p7zPlugin = new SevenZipPlugin();
    return AMPE_OK;
}

extern "C" int __stdcall AMP_GetPlugInInfo(AMP_PlugInInfo** ppii)
{
    if (!g_p7zPlugin) return AMPE_Error;
    *ppii = g_p7zPlugin->GetPlugInInfo();
    return AMPE_OK;
}

extern "C" int __stdcall AMP_OpenArchive(const AMP_OpenArchiveParams* params,
                                         void** phArchive)
{
    return g_p7zPlugin
               ? g_p7zPlugin->OpenArchive(params, phArchive)
               : AMPE_Error;
}

extern "C" int __stdcall AMP_GetArchiveInfo(void* hArchive,
                                            AMP_ArchiveInfo* pInfo)
{
    return g_p7zPlugin
               ? g_p7zPlugin->GetArchiveInfo(hArchive, pInfo)
               : AMPE_Error;
}

extern "C" int __stdcall AMP_FileDecode(void* hArchive,
                                        AMP_FileInfo* pFileInfo,
                                        void* pExtended)
{
    return g_p7zPlugin
               ? g_p7zPlugin->FileDecode(hArchive, pFileInfo, pExtended)
               : AMPE_Error;
}

extern "C" int __stdcall AMP_CloseArchive(void* hArchive, void* pReserved)
{
    return g_p7zPlugin
               ? g_p7zPlugin->CloseArchive(hArchive, pReserved)
               : AMPE_Error;
}

extern "C" int __stdcall AMP_ShowPlugInDialog(HWND hwndParent)
{
    if (!g_p7zPlugin) return AMPE_Error;
    g_p7zPlugin->ShowPlugInDialog(hwndParent);
    return AMPE_OK;
}
