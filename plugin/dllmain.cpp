//-------------------------------------------------------------------
// dllmain.cpp -- DLL entry point for AX_7Z.apl
//
// On attach: saves the module handle (needed by ShowPlugInDialog for
// resource loading) and initialises the LZMA CRC table.
// On detach: cleans up the global plugin instance.
//-------------------------------------------------------------------

#include <windows.h>
#include "7zPlugin.h"
#include "../archive/7zArchive.h"
#include <7zCrc.h>

extern SevenZipPlugin* g_p7zPlugin;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        SevenZipPlugin::g_hModule = hinstDLL;
        CrcGenerateTable();
        break;

    case DLL_PROCESS_DETACH:
        delete g_p7zPlugin;
        g_p7zPlugin = nullptr;
        break;
    }

    return TRUE;
}
