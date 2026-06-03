//-------------------------------------------------------------------
// 7zPlugin.h -- SevenZipPlugin class declaration
//-------------------------------------------------------------------

#pragma once
#include <windows.h>
#include "AX_PlugIn.h"


class SevenZipPlugin
{
public:
    SevenZipPlugin();
    ~SevenZipPlugin() = default;

    // --- ACDSee AX plug-in entry points ------------------------------

    AMP_PlugInInfo* GetPlugInInfo()
    {
        return &m_PlugInInfo;
    }

    int OpenArchive(const AMP_OpenArchiveParams* params, void** phArchive);
    int GetArchiveInfo(void* hArchive, AMP_ArchiveInfo* pInfo);
    int FileDecode(void* hArchive, AMP_FileInfo* pFileInfo, void* pExtended);
    int CloseArchive(void* hArchive, void* pReserved);

    void ShowPlugInDialog(HWND hwndParent);

    // --- Module handle (set by DllMain) ------------------------------

    static HINSTANCE g_hModule;

private:
    static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message,
                                         WPARAM wParam, LPARAM lParam);

    // --- Format / plug-in info blocks --------------------------------

    ID_FormatInfo  m_FormatInfo;
    AMP_PlugInInfo m_PlugInInfo;
};
