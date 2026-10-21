#include "platform.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>

using namespace cpamp;
int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
Json Signals(const char* used, const char* reset = "1789109899") {
    return {{"X-Codex-Primary-Window-Minutes", "300"}, {"X-Codex-Primary-Used-Percent", used}, {"X-Codex-Primary-Reset-At", reset}};
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--diagnose") {
            auto settings = LoadSettings(ConfigPath());
            std::cout << "Connections: " << settings.connections.size() << "\n";
            for (size_t i = 0; i < settings.connections.size(); ++i) {
                auto status = Fetch(settings.connections[i]);
                std::cout << "Connection " << i + 1 << ": ";
                if (status.quota) std::cout << "remaining=" << status.quota->remaining << "%, accounts=" << status.quota->accounts << ", reset=" << status.quota->reset << "\n";
                else std::cout << "FAILED: " << status.error << "\n";
            }
            return 0;
        }
        if (argc == 3 && (std::string(argv[1]) == "--write-fixture-config" || std::string(argv[1]) == "--write-lifecycle-config")) {
            const auto scenario = std::string(argv[1]) == "--write-lifecycle-config" ? "/lifecycle" : "/health";
            SaveSettings(Settings{15, {{"LOCAL TEST FIXTURE (not a real account)", std::string(argv[2]) + scenario, "integration-key"}}}, ConfigPath());
            std::cout << "Wrote local fixture configuration\n";
            return 0;
        }
        auto q = ExtractQuota(Json::parse(R"({"fiveHour":{"remainingPercent":73.5,"resetAt":"2026-09-11T12:30:00Z"}})"));
        Check(q && q->remaining == 73.5, "camel-case percent");
        Check(q->reset == *Timestamp("2026-09-11T20:30:00+08:00"), "timezone conversion");
        Check(Timestamp("2026-09-11T12:30:00.123Z") == Timestamp("2026-09-11T12:30:00Z"), "fractional time");
        Check(Timestamp(1789129800000LL) == Timestamp("1789129800"), "milliseconds/numeric string");
        Check(!Timestamp("broken") && !Timestamp(-1), "invalid timestamp");
        Check(!Number("nan") && !Number("12bad"), "invalid number");
        q = ExtractQuota(Json::parse(R"({"five_hour":{"used_percent":65,"reset_at":1789129800}})"));
        Check(q && q->remaining == 35, "used percent");
        q = ExtractQuota(Json::parse(R"({"remaining_fraction":0.42,"resetTime":1789129800})"));
        Check(q && q->remaining == 42, "fraction");
        q = ExtractQuota(Json::parse(R"({"remaining_percent":120,"reset_at":1789129800})"));
        Check(q && q->remaining == 100, "clamp percent");
        q = ExtractQuota(Json::parse(R"({"items":[{"windows":[{"window_kind":"weekly","remaining_percent":90,"reset_at":1789129800},{"window_kind":"five_hour","remaining_percent":61,"current_cycle":{"scheduled_end_ms":1789129800000}}]}]})"));
        Check(q && q->remaining == 61, "prefer five hour over weekly");
        Check(!ExtractQuota(Json::parse(R"({"windows":[{"window_kind":"weekly","remaining_percent":90,"reset_at":1789129800}]})")), "reject weekly-only");
        Check(!ExtractQuota(Json::parse(R"({"remaining_percent":90})")), "missing reset");
        Json auth = {{"files", Json::array({
            {{"provider", "codex"}, {"name", "one.json"}, {"auth_index", "auth-1"}, {"account", "user@example.com"},
                {"model_quotas", {{"old", {{"observed_at", "2026-09-11T17:30:00+08:00"}, {"signals", Signals("90")}}},
                    {"new", {{"observed_at", "2026-09-11T17:31:00+08:00"}, {"signals", Signals("20")}}}}}},
            {{"provider", "codex"}, {"name", "two.json"}, {"model_quotas", {{"model", {{"signals", Signals("60", "1789128128")}}}}}},
            {{"provider", "codex"}, {"name", "disabled.json"}, {"disabled", true}, {"model_quotas", {{"model", {{"signals", Signals("100")}}}}}},
            {{"provider", "other"}, {"name", "other.json"}, {"model_quotas", {{"model", {{"signals", Signals("100")}}}}}}
        })}};
        q = HealthQuota(auth);
        Check(q && q->remaining == 60 && q->accounts == 2 && q->reset == 1789109899, "health latest, mean, earliest, disabled/provider filter");
        auto accounts = SnapshotAccounts(auth);
        Check(accounts.size() == 2 && accounts[0]["row_key"] == "auth-1" && accounts[0]["account"]["auth_file_snapshot"] == "one.json", "snapshot request");
        q = Average({{"a", Quota{80, 1789109899, 2}, ""}, {"b", Quota{20, 1789128128, 1}, ""}, {"offline", {}, "HTTP 401"}});
        Check(q && q->remaining == 60 && q->accounts == 3 && q->reset == 1789109899, "weighted multiple servers");
        Check(!Average({{"offline", {}, "error"}}), "all offline");
        q = SnapshotQuota(Json::parse(R"({"items":[{"windows":[{"window_kind":"five_hour","remaining_percent":80,"reset_at":1789109899}]},{"windows":[{"window_kind":"five_hour","remaining_percent":20,"reset_at":1789128128}]}]})"));
        Check(q && q->remaining == 50 && q->accounts == 2, "aggregate snapshot accounts");
        Check(NormalizeUrl(" https://example.com:66/management.html?foo=1#x ") == "https://example.com:66", "normalize panel page");
        Check(NormalizeUrl("https://example.com/prefix/") == "https://example.com/prefix", "preserve reverse proxy prefix");
        bool invalid = false;
        try { ValidateUrl("file:///tmp/key"); } catch (...) { invalid = true; }
        Check(invalid, "reject invalid URL");
        invalid = false;
        try { ValidateUrl("https://user:pass@example.com"); } catch (...) { invalid = true; }
        Check(invalid, "reject embedded URL credentials");
        auto dir = std::filesystem::temp_directory_path() / (L"CPAMP-test-" + std::to_wstring(GetCurrentProcessId()));
        auto path = dir / "settings.json";
        Settings settings{15, {{"test", "https://example.com/management.html", "secret-unit-test-key"}}};
        SaveSettings(settings, path);
        auto loaded = LoadSettings(path);
        Check(loaded.refresh == 15 && loaded.connections.at(0).key == "secret-unit-test-key", "DPAPI roundtrip");
        std::ifstream stream(path); std::string public_data((std::istreambuf_iterator<char>(stream)), {}); stream.close();
        Check(public_data.find("secret-unit-test-key") == std::string::npos, "no plaintext key on disk");
        settings.refresh = 9999; SaveSettings(settings, path);
        Check(LoadSettings(path).refresh == 3600, "refresh clamp");
        std::filesystem::remove(path); std::filesystem::remove(dir);
        if (argc > 1) {
            std::string base = argv[1];
            for (const auto& scenario : {"health", "snapshot", "legacy", "legacy-v1", "prefix", "partial"}) {
                auto status = Fetch({scenario, base + "/" + scenario, "integration-key"});
                Check(status.quota && std::abs(status.quota->remaining - 60) < 0.001, scenario);
                Check(status.quota->accounts == (std::string(scenario) == "health" || std::string(scenario) == "snapshot" || std::string(scenario) == "partial" || std::string(scenario) == "prefix" ? 2 : 1), "integration account count");
            }
            auto status = Fetch({"unauthorized", base + "/unauthorized", "wrong-key"});
            Check(!status.quota && status.error.find("401") != std::string::npos, "HTTP 401");
            status = Fetch({"malformed", base + "/malformed", "integration-key"});
            Check(!status.quota && status.error.find("JSON") != std::string::npos, "malformed JSON");
            status = Fetch({"redirect", base + "/redirect", "integration-key"});
            Check(!status.quota && status.error.find("302") != std::string::npos, "redirect is not followed");
            status = Fetch({"empty", base + "/empty", "integration-key"});
            Check(!status.quota, "no active accounts");
        }
        std::cout << "PASS: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n"; return 1; }
}
