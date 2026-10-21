#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "PluginInterface.h"
#include "platform.h"
#include "resource.h"
#include <array>
#include <mutex>
#include <memory>
#include <sstream>
#include <ctime>

namespace {
int module_anchor;
HMODULE Module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_anchor), &module);
    return module;
}
class Plugin;
class Item final : public IPluginItem {
public:
    Item(Plugin& plugin, int index) : plugin_(plugin), index_(index) {}
    const wchar_t* GetItemName() const override { return index_ ? L"CPAMP Reset Time" : L"CPAMP 5H Remaining"; }
    const wchar_t* GetItemId() const override { return index_ ? L"CPAMPResetTime" : L"CPAMP5HRemaining"; }
    const wchar_t* GetItemLableText() const override { return index_ ? L"Reset: " : L"5H: "; }
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override { return index_ ? L"00:00" : L"100%"; }
    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { return index_ ? 85 : 72; }
    int GetItemWidthEx(void* dc) const override {
        std::wstring sample = std::wstring(GetItemLableText()) + GetItemValueSampleText();
        SIZE size{};
        if (GetTextExtentPoint32W(static_cast<HDC>(dc), sample.c_str(), static_cast<int>(sample.size()), &size)) return size.cx + 6;
        return 0;
    }
    void DrawItem(void* dc, int x, int y, int w, int h, bool dark) override;
    int OnMouseEvent(MouseEventType type, int, int, void* window, int) override;
private:
    Plugin& plugin_;
    int index_;
};

class Plugin final : public ITMPlugin {
public:
    Plugin() : items_{Item(*this, 0), Item(*this, 1)} {
        try {
            settings_ = cpamp::LoadSettings(cpamp::ConfigPath());
            tooltip_ = settings_.connections.empty() ? L"CPAMP: configure Server URL and Admin Key in plugin options." : L"CPAMP: waiting for first refresh";
        } catch (const std::exception& e) { tooltip_ = L"CPAMP configuration error: " + cpamp::Wide(e.what()); }
    }
    IPluginItem* GetItem(int index) override { return index >= 0 && index < 2 ? &items_[index] : nullptr; }
    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME: return L"CPAMP Quota Monitor";
        case TMI_DESCRIPTION: return L"Codex 5-hour remaining quota and local reset time from CPAMP Manager Server.";
        case TMI_AUTHOR: return L"CPAMP TrafficMonitor contributors";
        case TMI_COPYRIGHT: return L"Based on shenyebuwu/cpamp_monitor";
        case TMI_VERSION: return L"1.0.0";
        case TMI_URL: return L"https://github.com/shenyebuwu/cpamp_monitor";
        default: return L"";
        }
    }
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* parent) override;
    const wchar_t* GetTooltipInfo() override {
        thread_local std::wstring text;
        std::lock_guard<std::mutex> lock(mutex_);
        text = tooltip_;
        return text.c_str();
    }
    int GetCommandCount() override { return 2; }
    const wchar_t* GetCommandName(int index) override { return index == 0 ? L"Refresh CPAMP now" : index == 1 ? L"CPAMP settings..." : L""; }
    void OnPluginCommand(int index, void* window, void*) override {
        if (index == 0) Refresh();
        if (index == 1) ShowOptionsDialog(window);
    }
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* text) override {
        if (index == EI_VALUE_TEXT_COLOR && text) {
            std::lock_guard<std::mutex> lock(mutex_);
            color_ = static_cast<COLORREF>(wcstoul(text, nullptr, 10));
            has_color_ = true;
        }
    }
    void Refresh() { { std::lock_guard<std::mutex> lock(mutex_); refresh_ = true; } DataRequired(); }
    std::wstring Text(int index) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!quota_) return index ? L"--:--" : L"--%";
        return index ? cpamp::ResetText(quota_->reset) : std::to_wstring(static_cast<int>(std::round(quota_->remaining))) + L"%";
    }
    COLORREF Color(bool dark) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!quota_ || quota_->remaining < 20) return dark ? RGB(255, 151, 102) : RGB(190, 62, 15);
        return has_color_ ? color_ : dark ? RGB(240, 240, 240) : RGB(25, 25, 25);
    }
private:
    struct Work {
        Plugin* plugin;
        cpamp::Settings settings;
        unsigned generation;
        HMODULE module;
    };
    static DWORD WINAPI Worker(void* raw) {
        std::unique_ptr<Work> work(static_cast<Work*>(raw));
        HMODULE module = work->module;
        try {
            std::vector<cpamp::Status> statuses;
            for (const auto& connection : work->settings.connections) statuses.push_back(cpamp::Fetch(connection));
            work->plugin->Publish(statuses, work->generation);
        } catch (...) {
            std::lock_guard<std::mutex> lock(work->plugin->mutex_);
            work->plugin->busy_ = false;
            work->plugin->quota_.reset();
            work->plugin->tooltip_ = L"CPAMP: refresh failed unexpectedly";
        }
        work.reset();
        // A worker holds its own DLL reference; host unload cannot race its code/data.
        FreeLibraryAndExitThread(module, 0);
    }
    void Publish(const std::vector<cpamp::Status>& statuses, unsigned generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = false;
        if (generation != generation_) return;
        quota_ = cpamp::Average(statuses);
        std::wostringstream text;
        text << L"CPAMP - 5-hour remaining quota";
        if (quota_) text << L"\nTotal: " << static_cast<int>(std::round(quota_->remaining)) << L"%, " << quota_->accounts << L" account(s), reset " << cpamp::ResetText(quota_->reset);
        for (const auto& status : statuses) {
            text << L"\n" << cpamp::Wide(status.name) << L": ";
            if (status.quota) text << static_cast<int>(std::round(status.quota->remaining)) << L"%, reset " << cpamp::ResetText(status.quota->reset) << L" (" << status.quota->accounts << L" accounts)";
            else text << L"Offline - " << cpamp::Wide(status.error);
        }
        text << L"\nUpdated: " << cpamp::ResetText(static_cast<int64_t>(time(nullptr))) << L" (local time)";
        tooltip_ = text.str();
    }
    std::mutex mutex_;
    cpamp::Settings settings_;
    std::optional<cpamp::Quota> quota_;
    std::wstring tooltip_;
    std::array<Item, 2> items_;
    bool busy_ = false, refresh_ = true, has_color_ = false;
    bool options_open_ = false;
    COLORREF color_ = 0;
    ULONGLONG last_ = 0;
    unsigned generation_ = 0;
};

void Plugin::DataRequired() {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = GetTickCount64();
        if (busy_ || settings_.connections.empty() || (!refresh_ && now - last_ < static_cast<ULONGLONG>(settings_.refresh) * 1000)) return;
        auto work = std::make_unique<Work>();
        work->plugin = this; work->settings = settings_; work->generation = generation_;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&module_anchor), &work->module)) return;
        HANDLE thread = CreateThread(nullptr, 0, Worker, work.get(), 0, nullptr);
        if (!thread) { FreeLibrary(work->module); return; }
        work.release();
        CloseHandle(thread);
        busy_ = true; refresh_ = false; last_ = now;
    } catch (...) {}
}

const wchar_t* Item::GetItemValueText() const {
    thread_local std::array<std::wstring, 2> values;
    values[index_] = plugin_.Text(index_);
    return values[index_].c_str();
}
void Item::DrawItem(void* context, int x, int y, int w, int h, bool dark) {
    auto dc = static_cast<HDC>(context);
    int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, plugin_.Color(dark));
    RECT rect{x, y, x + w, y + h};
    auto text = std::wstring(GetItemLableText()) + plugin_.Text(index_);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (saved) RestoreDC(dc, saved);
}
int Item::OnMouseEvent(MouseEventType type, int, int, void* window, int) {
    if (type == MT_DBCLICKED) { plugin_.ShowOptionsDialog(window); return 1; }
    return 0;
}

struct DialogState {
    cpamp::Settings settings;
    int selected = -1;
    bool changed = false, dirty = false, loading = false;
};
std::wstring Field(HWND window, int id) {
    HWND edit = GetDlgItem(window, id);
    std::wstring text(GetWindowTextLengthW(edit) + 1, 0);
    text.resize(GetWindowTextW(edit, text.data(), static_cast<int>(text.size())));
    return text;
}
void Clear(HWND window, DialogState& state) {
    state.loading = true;
    state.selected = -1;
    SendDlgItemMessageW(window, IDC_CONNECTIONS, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    for (int id : {IDC_NAME, IDC_URL, IDC_KEY}) SetDlgItemTextW(window, id, L"");
    state.loading = state.dirty = false;
}
void Populate(HWND window, DialogState& state) {
    SendDlgItemMessageW(window, IDC_CONNECTIONS, LB_RESETCONTENT, 0, 0);
    for (const auto& c : state.settings.connections) SendDlgItemMessageW(window, IDC_CONNECTIONS, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(cpamp::Wide(c.name).c_str()));
}
INT_PTR CALLBACK DialogProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<DialogState*>(lp);
        SetWindowLongPtrW(window, DWLP_USER, lp);
        Populate(window, *state);
        SetDlgItemInt(window, IDC_REFRESH, state->settings.refresh, FALSE);
        for (int id : {IDC_NAME, IDC_URL, IDC_KEY}) SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, id == IDC_NAME ? 128 : 4096, 0);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_CLOSE) { EndDialog(window, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND) return FALSE;
    const int id = LOWORD(wp), event = HIWORD(wp);
    try {
        if ((id == IDC_NAME || id == IDC_URL || id == IDC_KEY) && event == EN_CHANGE && !state->loading) state->dirty = true;
        if (id == IDC_CONNECTIONS && event == LBN_SELCHANGE) {
            if (state->dirty) {
                SendDlgItemMessageW(window, IDC_CONNECTIONS, LB_SETCURSEL, state->selected, 0);
                throw std::runtime_error("Apply your connection edits or click New to discard them first.");
            }
            int selected = static_cast<int>(SendDlgItemMessageW(window, IDC_CONNECTIONS, LB_GETCURSEL, 0, 0));
            if (selected >= 0 && selected < static_cast<int>(state->settings.connections.size())) {
                state->selected = selected;
                state->loading = true;
                const auto& c = state->settings.connections[selected];
                SetDlgItemTextW(window, IDC_NAME, cpamp::Wide(c.name).c_str());
                SetDlgItemTextW(window, IDC_URL, cpamp::Wide(c.url).c_str());
                SetDlgItemTextW(window, IDC_KEY, cpamp::Wide(c.key).c_str());
                state->loading = state->dirty = false;
            }
        }
        if (id == IDC_NEW) Clear(window, *state);
        if (id == IDC_UPDATE) {
            cpamp::Connection c{cpamp::Utf8(Field(window, IDC_NAME)), cpamp::NormalizeUrl(cpamp::Utf8(Field(window, IDC_URL))), cpamp::Utf8(Field(window, IDC_KEY))};
            if (c.name.empty() || c.url.empty() || c.key.empty()) throw std::runtime_error("Name, Server URL and Admin Key are required.");
            if (c.key.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("Invalid Admin Key.");
            cpamp::ValidateUrl(c.url);
            if (state->selected < 0) { state->selected = static_cast<int>(state->settings.connections.size()); state->settings.connections.push_back(c); }
            else state->settings.connections[state->selected] = c;
            Populate(window, *state);
            SendDlgItemMessageW(window, IDC_CONNECTIONS, LB_SETCURSEL, state->selected, 0);
            state->dirty = false;
            SetDlgItemTextW(window, IDC_STATUS, L"Connection applied. Click Save to persist and refresh.");
        }
        if (id == IDC_DELETE && state->selected >= 0) {
            state->settings.connections.erase(state->settings.connections.begin() + state->selected);
            Populate(window, *state); Clear(window, *state);
        }
        if (id == IDOK) {
            if (state->dirty) throw std::runtime_error("Click Apply connection before saving your connection edits.");
            BOOL valid = FALSE;
            UINT interval = GetDlgItemInt(window, IDC_REFRESH, &valid, FALSE);
            if (!valid || interval < 15 || interval > 3600) throw std::runtime_error("Refresh interval must be 15 to 3600 seconds.");
            state->settings.refresh = static_cast<int>(interval);
            cpamp::SaveSettings(state->settings, cpamp::ConfigPath());
            state->changed = true; EndDialog(window, IDOK);
        }
        if (id == IDCANCEL) EndDialog(window, IDCANCEL);
    } catch (const std::exception& e) { MessageBoxW(window, cpamp::Wide(e.what()).c_str(), L"CPAMP settings", MB_OK | MB_ICONWARNING); }
    return TRUE;
}
ITMPlugin::OptionReturn Plugin::ShowOptionsDialog(void* parent) {
    DialogState state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (options_open_) return OR_OPTION_UNCHANGED;
        options_open_ = true;
        state.settings = settings_;
    }
    INT_PTR result = DialogBoxParamW(Module(), MAKEINTRESOURCEW(IDD_SETTINGS), static_cast<HWND>(parent), DialogProc, reinterpret_cast<LPARAM>(&state));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        options_open_ = false;
        if (state.changed) {
            settings_ = std::move(state.settings); ++generation_; refresh_ = true; quota_.reset();
            tooltip_ = settings_.connections.empty() ? L"CPAMP: no connections configured" : L"CPAMP: refreshing...";
        }
    }
    if (result == -1) MessageBoxW(static_cast<HWND>(parent), L"Cannot open CPAMP settings dialog.", L"CPAMP", MB_OK | MB_ICONERROR);
    if (state.changed) { DataRequired(); return OR_OPTION_CHANGED; }
    return OR_OPTION_UNCHANGED;
}
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() {
    static Plugin instance;
    return &instance;
}
