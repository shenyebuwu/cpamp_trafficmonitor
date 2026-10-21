#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <shlobj.h>
#include "platform.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <ctime>

namespace cpamp {
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n);
    return result;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n, nullptr, nullptr);
    return result;
}
namespace {
std::string Protect(const std::string& text, bool decrypt) {
    if (text.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(text.size()), reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))}, output{};
    BOOL ok = decrypt ? CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) :
        CryptProtectData(&input, L"CPAMP TrafficMonitor", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) throw std::runtime_error("Windows DPAPI failed; use the Windows account that saved this configuration");
    std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}
std::string Hex(const std::string& text) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : text) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
std::string Unhex(const std::string& text) {
    if (text.size() % 2) throw std::runtime_error("Invalid encrypted key");
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error("Invalid encrypted key");
    };
    std::string result;
    for (size_t i = 0; i < text.size(); i += 2) result += static_cast<char>((digit(text[i]) << 4) | digit(text[i + 1]));
    return result;
}
struct HttpHandle {
    HINTERNET value = nullptr;
    explicit HttpHandle(HINTERNET h) : value(h) { if (!h) throw std::runtime_error("Cannot initialize WinHTTP"); }
    ~HttpHandle() { WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
};
struct Url {
    std::wstring host, path;
    INTERNET_PORT port;
    bool secure;
};
Url ParseUrl(const std::string& input) {
    auto text = Wide(input);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(text.c_str(), 0, 0, &parts) || !parts.dwHostNameLength ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        parts.dwUserNameLength || parts.dwPasswordLength) throw std::runtime_error("Use an http:// or https:// server URL without embedded credentials");
    return {std::wstring(parts.lpszHostName, parts.dwHostNameLength), std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength),
        parts.nPort, parts.nScheme == INTERNET_SCHEME_HTTPS};
}
Json Request(const Connection& connection, const char* endpoint, const Json* body = nullptr) {
    const auto url = ParseUrl(NormalizeUrl(connection.url) + "/" + endpoint);
    HttpHandle session(WinHttpOpen(L"CPAMP-TrafficMonitor/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session.value, 12000, 12000, 12000, 12000);
    HttpHandle host(WinHttpConnect(session.value, url.host.c_str(), url.port, 0));
    HttpHandle request(WinHttpOpenRequest(host.value, body ? L"POST" : L"GET", url.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0));
    // Never forward administrator credentials through HTTP redirects.
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (connection.key.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("Invalid admin key");
    auto headers = Wide("Authorization: Bearer " + connection.key + "\r\nX-API-Key: " + connection.key +
        "\r\nAccept: application/json\r\nContent-Type: application/json\r\n");
    std::string payload = body ? body->dump() : "";
    if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()),
        payload.empty() ? WINHTTP_NO_REQUEST_DATA : payload.data(), static_cast<DWORD>(payload.size()),
        static_cast<DWORD>(payload.size()), 0) || !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("Network/TLS request failed (WinHTTP " + std::to_string(GetLastError()) + ")");
    DWORD status = 0, length = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) throw std::runtime_error("HTTP " + std::to_string(status));
    std::string response;
    const auto deadline = GetTickCount64() + 20000;
    for (;;) {
        char buffer[16384]; DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) throw std::runtime_error("Failed to read response");
        if (!read) break;
        response.append(buffer, read);
        if (response.size() > 8 * 1024 * 1024 || GetTickCount64() > deadline) throw std::runtime_error("Response exceeds size/time limit");
    }
    auto json = Json::parse(response, nullptr, false);
    if (json.is_discarded()) throw std::runtime_error("Server returned invalid JSON");
    return json;
}
}

std::filesystem::path ConfigPath() {
    PWSTR dir = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &dir))) throw std::runtime_error("Cannot locate AppData");
    std::filesystem::path path(dir);
    CoTaskMemFree(dir);
    return path / L"CPAMPTrafficMonitor" / L"settings.json";
}
void ValidateUrl(const std::string& url) { (void)ParseUrl(NormalizeUrl(url)); }
Settings LoadSettings(const std::filesystem::path& path) {
    Settings result;
    if (!std::filesystem::exists(path)) return result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read configuration");
    Json json;
    try { stream >> json; } catch (...) { throw std::runtime_error("Invalid configuration JSON"); }
    result.refresh = std::clamp(json.value("refresh_seconds", 60), 15, 3600);
    for (const auto& item : json.at("connections")) {
        Connection c{item.at("name").get<std::string>(), NormalizeUrl(item.at("server_url").get<std::string>()),
            Protect(Unhex(item.at("key_dpapi").get<std::string>()), true)};
        ValidateUrl(c.url);
        if (c.key.empty()) throw std::runtime_error("Empty admin key in configuration");
        result.connections.push_back(std::move(c));
    }
    return result;
}
void SaveSettings(const Settings& settings, const std::filesystem::path& path) {
    Json json{{"version", 1}, {"refresh_seconds", std::clamp(settings.refresh, 15, 3600)}, {"connections", Json::array()}};
    for (const auto& c : settings.connections) {
        ValidateUrl(c.url);
        json["connections"].push_back({{"name", c.name}, {"server_url", NormalizeUrl(c.url)}, {"key_dpapi", Hex(Protect(c.key, false))}});
    }
    std::filesystem::create_directories(path.parent_path());
    auto temp = path; temp += L".tmp";
    { std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        stream << json.dump(2); stream.flush();
        if (!stream) throw std::runtime_error("Cannot write configuration");
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace configuration file");
}

Status Fetch(const Connection& connection) {
    Status status{connection.name, {}, ""};
    if (connection.url.empty() || connection.key.empty()) { status.error = "Missing server URL or admin key"; return status; }
    std::string error;
    try {
        auto auth = Request(connection, "v0/management/auth-files");
        status.quota = HealthQuota(auth);
        if (status.quota) return status;
        auto accounts = SnapshotAccounts(auth);
        if (!accounts.empty()) {
            Json body{{"accounts", accounts}};
            status.quota = SnapshotQuota(Request(connection, "v0/management/quota-snapshots/query", &body));
            if (status.quota) return status;
            error = "No 5-hour quota found in snapshots";
        } else error = "No active Codex accounts found";
    } catch (const std::exception& e) { error = e.what(); }
    for (const auto* endpoint : {"api/admin/manager-data", "api/v1/admin/manager-data"}) {
        try {
            status.quota = SnapshotQuota(Request(connection, endpoint));
            if (status.quota) return status;
            error = "No 5-hour quota found";
        } catch (const std::exception& e) {
            // Keep a useful primary error rather than replacing it with a fallback 404.
            if (std::string(e.what()) != "HTTP 404" || error.empty()) error = e.what();
        }
    }
    status.error = error;
    return status;
}
std::wstring ResetText(int64_t timestamp) {
    time_t t = static_cast<time_t>(timestamp);
    tm local{};
    if (localtime_s(&local, &t)) return L"--:--";
    wchar_t buffer[32];
    wcsftime(buffer, 32, L"%H:%M", &local);
    return buffer;
}
}
