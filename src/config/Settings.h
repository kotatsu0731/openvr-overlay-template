#pragma once

#include <string>

namespace flickvr::config {

struct Settings {
    // General
    bool autostart_steamvr = false;

    // Input
    bool block_vr_input_while_pointing = false;
    static constexpr float pointer_vertical_gain = 1.0F;

    // Display
    float overlay_distance_m = 0.65F;
    float overlay_height_offset_m = -0.20F;
    float overlay_width_m = 0.60F;
    float overlay_tilt_degrees = 0.0F;
    float overlay_opacity = 1.0F;

    static Settings defaults();
    bool validate(std::string* reason = nullptr) const;
    static bool load(const std::string& path, Settings& out, std::string* error = nullptr);
    bool save(const std::string& path, std::string* error = nullptr) const;
};

class SettingsSession {
public:
    explicit SettingsSession(std::string path);
    bool load(Settings& out, std::string* error = nullptr);
    bool save(const Settings& settings, std::string* error = nullptr) const;
    const std::string& warning() const noexcept { return warning_; }

private:
    std::string path_;
    std::string warning_;
    bool save_blocked_{};
};

}  // namespace flickvr::config
