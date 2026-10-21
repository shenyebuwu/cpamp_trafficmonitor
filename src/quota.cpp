#include "quota.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace cpamp {
namespace {
const Json* Find(const Json& data, std::initializer_list<const char*> keys) {
    if (data.is_object()) {
        for (auto key : keys) {
            auto it = data.find(key);
            if (it != data.end() && !it->is_null()) return &*it;
        }
    }
    if (data.is_structured()) {
        for (const auto& value : data) {
            if (const auto* found = Find(value, keys)) return found;
        }
    }
    return nullptr;
}
Json Value(const Json& data, const char* key) {
    return data.is_object() && data.contains(key) ? data[key] : Json();
}
std::string Text(const Json& data, const char* key) {
    auto v = Value(data, key);
    return v.is_string() ? v.get<std::string>() : "";
}
bool Active(const Json& item) {
    if (!item.is_object() || Text(item, "provider") != "codex") return false;
    auto disabled = Value(item, "disabled");
    return disabled.is_null() || (disabled.is_boolean() && !disabled.get<bool>());
}
std::optional<Quota> Window(const Json& data) {
    if (!data.is_structured()) return {};
    auto p = Find(data, {"remaining_percent", "remainingPercent", "percent_remaining", "percentRemaining"});
    auto percent = p ? Number(*p) : std::nullopt;
    if (!percent) {
        p = Find(data, {"used_percent", "usedPercent", "percent_used", "percentUsed"});
        if (p) if (auto used = Number(*p)) percent = 100 - *used;
    }
    if (!percent) {
        p = Find(data, {"remaining", "remaining_fraction", "remainingFraction"});
        if (p) if (auto r = Number(*p)) percent = *r >= 0 && *r <= 1 ? *r * 100 : *r;
    }
    p = Find(data, {"reset_at", "resetAt", "resets_at", "resetsAt", "reset_time", "resetTime",
        "cycle_end_ms", "cycleEndMS", "scheduled_end_ms", "scheduledEndMS"});
    auto reset = p ? Timestamp(*p) : std::nullopt;
    if (!percent || !reset) return {};
    return Quota{std::clamp(*percent, 0.0, 100.0), *reset, 1};
}
void Windows(const Json& data, std::vector<const Json*>& result, bool& explicit_other) {
    if (data.is_object()) {
        auto windows = data.find("windows");
        if (windows != data.end() && windows->is_array()) {
            for (const auto& w : *windows) {
                if (!w.is_object()) continue;
                auto kind = Text(w, "window_kind");
                if (kind.empty()) kind = Text(w, "windowKind");
                auto duration = Value(w, "duration_seconds");
                if (duration.is_null()) duration = Value(w, "durationSeconds");
                if (duration.is_null()) duration = Value(w, "window_seconds");
                if (kind == "five_hour" || kind == "five-hour" || Number(duration) == 18000) result.push_back(&w);
                else if (!kind.empty() || !duration.is_null()) explicit_other = true;
            }
        }
    }
    if (data.is_structured()) for (const auto& v : data) Windows(v, result, explicit_other);
}
}

std::optional<double> Number(const Json& value) {
    try {
        double result;
        if (value.is_number()) result = value.get<double>();
        else if (value.is_string()) {
            auto text = value.get<std::string>();
            size_t end = 0;
            result = std::stod(text, &end);
            if (end != text.size()) return {};
        } else return {};
        if (std::isfinite(result)) return result;
    } catch (...) {}
    return {};
}

std::optional<int64_t> Timestamp(const Json& value) {
    if (auto n = Number(value)) {
        double seconds = *n > 10000000000.0 ? *n / 1000 : *n;
        if (seconds >= 0 && seconds <= 253402300799.0) return static_cast<int64_t>(seconds);
        return {};
    }
    if (!value.is_string()) return {};
    const auto s = value.get<std::string>();
    int y, mo, d, h, mi, sec, pos = 0;
    if (sscanf_s(s.c_str(), "%d-%d-%dT%d:%d:%d%n", &y, &mo, &d, &h, &mi, &sec, &pos) != 6) return {};
    if (y < 1970 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) return {};
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(y); st.wMonth = static_cast<WORD>(mo); st.wDay = static_cast<WORD>(d);
    st.wHour = static_cast<WORD>(h); st.wMinute = static_cast<WORD>(mi); st.wSecond = static_cast<WORD>(sec);
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return {};
    int64_t result = ((static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000000 - 11644473600LL;
    if (pos < static_cast<int>(s.size()) && s[pos] == '.') {
        ++pos;
        while (pos < static_cast<int>(s.size()) && s[pos] >= '0' && s[pos] <= '9') ++pos;
    }
    if (pos == static_cast<int>(s.size())) return result;
    if (s[pos] == 'Z' && pos + 1 == static_cast<int>(s.size())) return result;
    if (s[pos] != '+' && s[pos] != '-') return {};
    int oh = 0, om = 0, used = 0;
    if (sscanf_s(s.c_str() + pos + 1, "%d:%d%n", &oh, &om, &used) != 2 ||
        pos + 1 + used != static_cast<int>(s.size()) || oh < 0 || oh > 23 || om < 0 || om > 59) return {};
    return result - (s[pos] == '+' ? 1 : -1) * (oh * 3600 + om * 60);
}

std::optional<Quota> ExtractQuota(const Json& data) {
    std::vector<const Json*> windows;
    bool other = false;
    Windows(data, windows, other);
    for (const auto* w : windows) if (auto q = Window(*w)) return q;
    const auto* five = Find(data, {"five_hour", "fiveHour", "five_hour_quota", "fiveHourQuota"});
    if (five) return Window(*five);
    // Never mistake an explicitly weekly window for a five-hour quota.
    if (other || !windows.empty()) return {};
    return Window(data);
}

std::optional<Quota> Average(const std::vector<Status>& statuses) {
    Quota result{0, std::numeric_limits<int64_t>::max(), 0};
    for (const auto& status : statuses) if (status.quota) {
        const auto& q = *status.quota;
        int weight = std::max(1, q.accounts);
        result.remaining += q.remaining * weight;
        result.accounts += weight;
        result.reset = std::min(result.reset, q.reset);
    }
    if (!result.accounts) return {};
    result.remaining /= result.accounts;
    return result;
}

std::optional<Quota> HealthQuota(const Json& data) {
    auto files = Value(data, "files");
    if (!files.is_array()) return {};
    std::vector<Status> statuses;
    for (const auto& file : files) {
        if (!Active(file)) continue;
        auto models = Value(file, "model_quotas");
        if (!models.is_object()) continue;
        std::optional<Quota> latest;
        int64_t observed = -1;
        for (const auto& model : models) {
            auto signals = Value(model, "signals");
            if (Number(Value(signals, "X-Codex-Primary-Window-Minutes")) != 300) continue;
            auto used = Number(Value(signals, "X-Codex-Primary-Used-Percent"));
            auto reset = Timestamp(Value(signals, "X-Codex-Primary-Reset-At"));
            auto time = Timestamp(Value(model, "observed_at")).value_or(0);
            if (used && reset && time > observed) {
                latest = Quota{std::clamp(100 - *used, 0.0, 100.0), *reset, 1};
                observed = time;
            }
        }
        if (latest) statuses.push_back({"", latest, ""});
    }
    return Average(statuses);
}

Json SnapshotAccounts(const Json& data) {
    auto result = Json::array();
    auto files = Value(data, "files");
    if (!files.is_array()) return result;
    for (const auto& file : files) {
        if (!Active(file)) continue;
        auto name = Text(file, "name"), index = Text(file, "auth_index");
        if (name.empty()) continue;
        result.push_back({{"row_key", index.empty() ? name : index}, {"provider", "codex"},
            {"account", {{"auth_file_snapshot", name}, {"auth_provider_snapshot", "codex"},
                {"auth_index", index}, {"account_snapshot", Text(file, "account")}}}});
    }
    return result;
}

std::optional<Quota> SnapshotQuota(const Json& data) {
    auto items = Value(data, "items");
    if (items.is_array()) {
        std::vector<Status> statuses;
        for (const auto& item : items) if (auto q = ExtractQuota(item)) statuses.push_back({"", q, ""});
        return Average(statuses);
    }
    return ExtractQuota(data);
}

std::string NormalizeUrl(std::string url) {
    auto first = url.find_first_not_of(" \t\r\n"), last = url.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    url = url.substr(first, last - first + 1);
    url.erase(url.find_first_of("?#") == std::string::npos ? url.size() : url.find_first_of("?#"));
    while (!url.empty() && url.back() == '/') url.pop_back();
    auto scheme = url.find("://");
    auto path = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
    auto lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    if (path != std::string::npos && lower.size() >= 5 && lower.substr(lower.size() - 5) == ".html") url.erase(path);
    return url;
}
}
