#include "config/Settings.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <toml++/toml.hpp>

namespace flickvr::config {
namespace {

std::filesystem::path PathFromUtf8(const std::string& path) {
#if defined(__cpp_lib_char8_t)
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
#else
    return std::filesystem::u8path(path);
#endif
}

bool fail(std::string* out, const std::string& message) {
    if (out) *out = message;
    return false;
}

bool in_range(float value, float low, float high) {
    return std::isfinite(value) && value >= low && value <= high;
}

bool read_bool(const toml::table& table, const char* key, bool& target, std::string* error) {
    if (const auto* node = table.get(key)) {
        if (const auto value = node->value<bool>()) {
            target = *value;
            return true;
        }
        return fail(error, std::string(key) + " must be a boolean");
    }
    return true;
}

bool read_float(const toml::table& table, const char* key, float& target, std::string* error) {
    if (const auto* node = table.get(key)) {
        if (const auto value = node->value<double>()) {
            target = static_cast<float>(*value);
            return true;
        }
        if (const auto value = node->value<std::int64_t>()) {
            target = static_cast<float>(*value);
            return true;
        }
        return fail(error, std::string(key) + " must be a number");
    }
    return true;
}

}  // namespace

Settings Settings::defaults() {
    return Settings{};
}

bool Settings::validate(std::string* reason) const {
    if (!in_range(overlay_distance_m, 0.30F, 1.50F)) {
        return fail(reason, "overlay_distance_m must be between 0.30 and 1.50");
    }
    if (!in_range(overlay_height_offset_m, -0.60F, 0.40F)) {
        return fail(reason, "overlay_height_offset_m must be between -0.60 and 0.40");
    }
    if (!in_range(overlay_width_m, 0.30F, 1.50F)) {
        return fail(reason, "overlay_width_m must be between 0.30 and 1.50");
    }
    if (!in_range(overlay_tilt_degrees, -45.0F, 45.0F)) {
        return fail(reason, "overlay_tilt_degrees must be between -45.0 and 45.0");
    }
    if (!in_range(overlay_opacity, 0.15F, 1.0F)) {
        return fail(reason, "overlay_opacity must be between 0.15 and 1.0");
    }
    return true;
}

bool Settings::load(const std::string& path, Settings& out, std::string* error) {
    const auto filePath = PathFromUtf8(path);
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        out = defaults();
        return true;
    }

    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return fail(error, "Could not open settings file: " + path);
    }

    toml::table root;
    try {
        root = toml::parse(input, path);
    } catch (const toml::parse_error& parseError) {
        return fail(error, std::string("Settings parse error: ") + parseError.what());
    }

    Settings candidate = defaults();
    if (const auto* general = root["general"].as_table()) {
        if (!read_bool(*general, "autostart_steamvr", candidate.autostart_steamvr, error)) return false;
    }
    if (const auto* inputSection = root["input"].as_table()) {
        if (!read_bool(*inputSection, "block_vr_input_while_pointing",
                       candidate.block_vr_input_while_pointing, error)) {
            return false;
        }
    }
    if (const auto* display = root["display"].as_table()) {
        if (!read_float(*display, "overlay_distance_m", candidate.overlay_distance_m, error) ||
            !read_float(*display, "overlay_height_offset_m", candidate.overlay_height_offset_m, error) ||
            !read_float(*display, "overlay_width_m", candidate.overlay_width_m, error) ||
            !read_float(*display, "overlay_tilt_degrees", candidate.overlay_tilt_degrees, error) ||
            !read_float(*display, "overlay_opacity", candidate.overlay_opacity, error)) {
            return false;
        }
    }

    if (!candidate.validate(error)) return false;
    out = candidate;
    return true;
}

bool Settings::save(const std::string& path, std::string* error) const {
    if (!validate(error)) return false;

    const auto filePath = PathFromUtf8(path);
    std::error_code ec;
    if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path(), ec);
        if (ec) {
            return fail(error, "Could not create settings directory: " + ec.message());
        }
    }

    toml::table root;
    toml::table general;
    general.insert("autostart_steamvr", autostart_steamvr);
    root.insert("general", std::move(general));

    toml::table inputSection;
    inputSection.insert("block_vr_input_while_pointing", block_vr_input_while_pointing);
    root.insert("input", std::move(inputSection));

    toml::table display;
    display.insert("overlay_distance_m", static_cast<double>(overlay_distance_m));
    display.insert("overlay_height_offset_m", static_cast<double>(overlay_height_offset_m));
    display.insert("overlay_width_m", static_cast<double>(overlay_width_m));
    display.insert("overlay_tilt_degrees", static_cast<double>(overlay_tilt_degrees));
    display.insert("overlay_opacity", static_cast<double>(overlay_opacity));
    root.insert("display", std::move(display));

    std::ostringstream buffer;
    buffer << root << '\n';

    const auto tempPath = filePath.parent_path() / (filePath.filename().wstring() + L".tmp");
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) return fail(error, "Could not write temporary settings file");
        const std::string data = buffer.str();
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!output) return fail(error, "Failed while writing temporary settings file");
    }

    std::filesystem::rename(tempPath, filePath, ec);
    if (ec) {
        std::filesystem::remove(filePath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, filePath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return fail(error, "Could not replace settings file: " + ec.message());
        }
    }
    return true;
}

SettingsSession::SettingsSession(std::string path) : path_(std::move(path)) {}

bool SettingsSession::load(Settings& out, std::string* error) {
    warning_.clear();
    save_blocked_ = false;
    if (Settings::load(path_, out, error)) return true;
    save_blocked_ = true;
    warning_ = "Settings file could not be loaded; saving is paused until fixed.";
    out = Settings::defaults();
    return false;
}

bool SettingsSession::save(const Settings& settings, std::string* error) const {
    if (save_blocked_) {
        return fail(error, warning_);
    }
    return settings.save(path_, error);
}

}  // namespace flickvr::config
