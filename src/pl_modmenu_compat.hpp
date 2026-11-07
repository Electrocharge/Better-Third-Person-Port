#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// LeviLaunchroid/Preloader mod-menu ABI used by BetterShieldBlocking-style mods.
// Kept separate from pl/Mod.hpp because the latter does not expose mod-menu
// registration types in older SDK layouts.
namespace pl::modmenu {

enum class ConfigType {
    Toggle,
    SliderInt,
    SliderFloat,
    Radio,
    Color,
    Keybind,
    Text,
    Button
};

enum class ButtonBehavior {
    Click,
    Hold,
    Toggle
};

enum class ButtonEvent {
    Click,
    Down,
    Up,
    StateChanged,
    Scroll
};

enum class ButtonStylePreset {
    Keycap,
    Accent
};

enum class ButtonIconFormat {
    Auto,
    Png,
    Webp,
    Svg,
    Resource
};

struct ConfigEntry {
    std::string key;
    std::string displayName;
    ConfigType type{};
    std::string defaultValue;
    std::string minValue;
    std::string maxValue;
    std::string dependsOn;
};

struct ModuleInfo {
    std::string moduleId;
    std::string displayName;
    std::string description;
    std::string modId;
    bool defaultEnabled{};
    bool hideInHudEditor{};
    std::vector<ConfigEntry> configs;
    std::function<void(std::string_view, bool)> onToggle;
    std::function<void(std::string_view, std::string_view, std::string_view)> onConfigChanged;
    std::function<void(std::string_view, std::string_view, bool)> onKeybind;
};

struct ButtonInfo {
    std::string buttonId;
    std::string moduleId;
    std::string displayName;
    std::string modId;
    std::string label;
    int androidKeyCode{};
    ButtonBehavior behavior{ButtonBehavior::Click};
    bool defaultVisible{true};
    ButtonStylePreset stylePreset{ButtonStylePreset::Keycap};
    uint32_t normalBgColor{};
    uint32_t activeBgColor{};
    uint32_t borderColor{};
    uint32_t textColor{};
    uint32_t activeTextColor{};
    float widthScale{};
    float heightScale{1.0f};
    ButtonIconFormat iconFormat{ButtonIconFormat::Auto};
    bool hideLabelWhenIconPresent{true};
    std::vector<unsigned char> iconData;
    std::vector<unsigned char> activeIconData;
    std::function<void(std::string_view, ButtonEvent, float)> onEvent;
};

bool registerModule(const ModuleInfo& info);
void unregisterModule(std::string_view moduleId);
bool registerButton(const ButtonInfo& info);
void unregisterButton(std::string_view buttonId);

} // namespace pl::modmenu
