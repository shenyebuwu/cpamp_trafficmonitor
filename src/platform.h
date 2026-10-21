#pragma once
#include "quota.h"
#include <filesystem>

namespace cpamp {
struct Connection {
    std::string name, url, key;
};
struct Settings {
    int refresh = 60;
    std::vector<Connection> connections;
};
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);
std::filesystem::path ConfigPath();
Settings LoadSettings(const std::filesystem::path& path);
void SaveSettings(const Settings& settings, const std::filesystem::path& path);
void ValidateUrl(const std::string& url);
Status Fetch(const Connection& connection);
std::wstring ResetText(int64_t timestamp);
}
