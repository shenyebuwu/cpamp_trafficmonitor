#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "PluginInterface.h"
#include <iostream>
#include <string>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Usage: PluginProbe.exe plugin.dll [--options|--expect-live]");
        HMODULE module = LoadLibraryW(argv[1]);
        if (!module) throw std::runtime_error("LoadLibrary failed");
        auto entry = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
        if (!entry) throw std::runtime_error("Missing TMPluginGetInstance export");
        auto* plugin = entry();
        if (!plugin || entry() != plugin || plugin->GetItem(-1) || plugin->GetItem(2)) throw std::runtime_error("Invalid singleton or item bounds");
        std::wcout << L"Plugin: " << plugin->GetInfo(ITMPlugin::TMI_NAME) << L", API " << plugin->GetAPIVersion() << L"\n";
        if (argc > 2 && std::wstring(argv[2]) == L"--options") plugin->ShowOptionsDialog(nullptr);
        ULONGLONG start = GetTickCount64(); plugin->DataRequired();
        if (GetTickCount64() - start > 100) throw std::runtime_error("DataRequired blocked");
        bool lifecycle = argc > 2 && std::wstring(argv[2]) == L"--expect-lifecycle";
        bool expect = lifecycle || (argc > 2 && std::wstring(argv[2]) == L"--expect-live");
        for (int i = 0; i < (expect ? 200 : 1); ++i) {
            if (std::wstring(plugin->GetItem(0)->GetItemValueText()) != L"--%") break;
            Sleep(50); plugin->DataRequired();
        }
        HDC dc = CreateCompatibleDC(nullptr);
        HDC screen = GetDC(nullptr);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 500, 100);
        ReleaseDC(nullptr, screen);
        HGDIOBJ old = SelectObject(dc, bitmap);
        for (int i = 0; i < 2; ++i) {
            auto* item = plugin->GetItem(i);
            std::wcout << item->GetItemId() << L": " << item->GetItemLableText() << item->GetItemValueText() << L"\n";
            if (!item->GetItemName() || !item->GetItemValueSampleText() || item->GetItemWidthEx(dc) <= 0) throw std::runtime_error("Invalid item interface");
            item->DrawItem(dc, 0, i * 40, 250, 40, true);
        }
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
        std::wcout << plugin->GetTooltipInfo() << L"\n";
        if (expect && std::wstring(plugin->GetItem(0)->GetItemValueText()) != L"60%") throw std::runtime_error("Expected live 60% fixture data");
        if (expect && std::wstring(plugin->GetItem(1)->GetItemValueText()) == L"--:--") throw std::runtime_error("Expected reset time");
        if (lifecycle) {
            auto wait = [&](const wchar_t* value, DWORD timeout) {
                const auto deadline = GetTickCount64() + timeout;
                do {
                    plugin->DataRequired();
                    if (std::wstring(plugin->GetItem(0)->GetItemValueText()) == value) return;
                    Sleep(50);
                } while (GetTickCount64() < deadline);
                throw std::runtime_error("Lifecycle transition timed out");
            };
            wait(L"10%", 20000);
            plugin->OnPluginCommand(0, nullptr, nullptr);
            wait(L"--%", 5000);
            if (std::wstring(plugin->GetItem(1)->GetItemValueText()) != L"--:--" ||
                std::wstring(plugin->GetTooltipInfo()).find(L"HTTP 401") == std::wstring::npos)
                throw std::runtime_error("Failure must clear both values and report HTTP error");
            plugin->OnPluginCommand(0, nullptr, nullptr);
            wait(L"70%", 5000);
            std::cout << "PASS: timed refresh, manual refresh, failure clears values, recovery\n";
        }
        FreeLibrary(module);
        std::cout << "PASS: DLL ABI, item bounds, asynchronous update and GDI drawing\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << " (Win32 " << GetLastError() << ")\n"; return 1; }
}
