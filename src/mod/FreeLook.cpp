#include "mod/FreeLook.h"
#include "mod/icons.hpp"
#include "mc/ClientInstance.h"
#include "pl_modmenu_compat.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

static ClientInstance* g_clientInstance = nullptr;

namespace {
// konran:off
const char* kModuleId = "betterthirdperson.main";
const char* kModId = "BetterThirdPerson";
const char* kButtonId = "betterthirdperson.toggle";
const char* kUseCustomButtonKey = "use_custom_button";
const char* kButtonOpacityKey = "button_opacity";
// konran:on

std::atomic_bool g_lifecycleEnabled{false};
std::atomic_bool g_moduleEnabled{false};
std::atomic_bool g_freelookEnabled{false};
std::atomic_bool g_useCustomButton{true};
std::atomic_bool g_buttonRegistered{false};
std::atomic<float> g_buttonOpacity{1.0f};
std::mutex g_buttonRegistryMutex;
std::atomic_bool g_headCapturePending{false};
std::atomic_bool g_headRotationLocked{false};
std::atomic<float> g_lockedHeadYaw{0.0f};

uintptr_t g_addressOne = 0;
uintptr_t g_addressTwo = 0;
uintptr_t g_addressThree = 0;


void onButtonEvent(std::string_view, pl::modmenu::ButtonEvent event, float value);

void setFreeLookEnabled(bool enabled) noexcept {
    g_freelookEnabled.store(enabled, std::memory_order_release);

    if (enabled) {
        // The actual head angle is captured on the game thread at the first available update.
        // This avoids touching Actor state from the UI callback thread.
        g_headCapturePending.store(true, std::memory_order_release);
        g_headRotationLocked.store(false, std::memory_order_release);
    } else {
        g_headCapturePending.store(false, std::memory_order_release);
        g_headRotationLocked.store(false, std::memory_order_release);
    }
}

bool captureHeadRotation(Actor* player) noexcept {
    if (!player || !g_freelookEnabled.load(std::memory_order_acquire)) {
        return false;
    }

    if (!g_headCapturePending.load(std::memory_order_acquire)) {
        return false;
    }

    auto* headRotation = player->tryGetComponent<ActorHeadRotationComponent>();
    if (!headRotation || !std::isfinite(headRotation->mYHeadRot)) {
        return false;
    }

    // Capture only once, on the game thread. Do not derive this from camera
    // rotation because camera and actor head rotation can use different spaces.
    g_lockedHeadYaw.store(headRotation->mYHeadRot, std::memory_order_relaxed);
    g_headCapturePending.store(false, std::memory_order_release);
    g_headRotationLocked.store(true, std::memory_order_release);
    return true;
}

void lockHeadRotation(Actor* player) noexcept {
    if (!player || !g_freelookEnabled.load(std::memory_order_acquire)) {
        return;
    }

    auto* headRotation = player->tryGetComponent<ActorHeadRotationComponent>();
    if (!headRotation) {
        return;
    }

    if (!g_headRotationLocked.load(std::memory_order_acquire)) {
        return;
    }

    const float lockedYaw = g_lockedHeadYaw.load(std::memory_order_relaxed);
    if (!std::isfinite(lockedYaw)) {
        return;
    }

    // Write old first, then current. Keeping the interpolation pair identical
    // prevents a render/update pass from observing a mixed pair.
    headRotation->mYHeadRotOld = lockedYaw;
    headRotation->mYHeadRot = lockedYaw;
}

void toggleFreeLook() noexcept {
    const bool next = !g_freelookEnabled.load(std::memory_order_relaxed);
    setFreeLookEnabled(next);
}

void executeTheStuff() {
    if (!g_lifecycleEnabled.load(std::memory_order_relaxed) ||
        !g_moduleEnabled.load(std::memory_order_relaxed) ||
        !g_freelookEnabled.load(std::memory_order_relaxed)) {
        return;
    }

    if (!g_clientInstance) return;

    Actor* globalPlayer = g_clientInstance->getLocalPlayer();
    if (!globalPlayer) return;

    // The first lock must happen at the earliest game-thread point after the
    // toggle. This prevents the first active frame from capturing a head angle
    // after the camera has already moved.
    captureHeadRotation(globalPlayer);
    lockHeadRotation(globalPlayer);
}

void onModuleToggle(std::string_view, bool enabled) {
    g_moduleEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        setFreeLookEnabled(false);
    }
}

float parseOpacity(std::string_view value, float fallback) noexcept {
    std::string text(value);
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);

    if (end == text.c_str() || !std::isfinite(parsed)) {
        return fallback;
    }

    return std::clamp(parsed, 0.0f, 100.0f) / 100.0f;
}

std::string applySvgOpacity(std::string_view svg, float opacity) {
    std::string result(svg);
    const std::size_t svgTagEnd = result.find('>');

    if (svgTagEnd == std::string::npos) {
        return result;
    }

    const std::size_t svgTagStart = result.find("<svg");
    if (svgTagStart == std::string::npos || svgTagStart > svgTagEnd) {
        return result;
    }

    const std::string attribute = " opacity=\"" + std::to_string(opacity) + "\"";
    result.insert(svgTagEnd, attribute);
    return result;
}

void unregisterCustomButton() {
    std::lock_guard lock(g_buttonRegistryMutex);

    if (!g_buttonRegistered.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    pl::modmenu::unregisterButton(kButtonId);
}

bool registerCustomButton() {
    std::lock_guard lock(g_buttonRegistryMutex);

    if (g_buttonRegistered.load(std::memory_order_acquire)) {
        return true;
    }

    if (!g_useCustomButton.load(std::memory_order_relaxed)) {
        return true;
    }

    pl::modmenu::ButtonInfo button{};
    button.buttonId = kButtonId;
    button.moduleId = kModuleId;
    button.displayName = "Better Third Person";
    button.modId = kModId;
    button.label = "";
    button.androidKeyCode = 0;
    button.behavior = pl::modmenu::ButtonBehavior::Toggle;
    button.defaultVisible = true;
    button.stylePreset = pl::modmenu::ButtonStylePreset::Keycap;
    button.normalBgColor = 0x01000000u;
    button.activeBgColor = 0x01000000u;
    button.borderColor = 0x01000000u;
    button.textColor = 0x01000000u;
    button.activeTextColor = 0x01000000u;
    button.widthScale = 1.0f;
    button.heightScale = 1.0f;
    button.iconFormat = pl::modmenu::ButtonIconFormat::Svg;
    button.hideLabelWhenIconPresent = true;

    const float opacity = g_buttonOpacity.load(std::memory_order_relaxed);
    const std::string disabledSvg = applySvgOpacity(
        nexcaise::freelook_icons::kDisabledSvg, opacity);
    const std::string enabledSvg = applySvgOpacity(
        nexcaise::freelook_icons::kEnabledSvg, opacity);

    button.iconData.assign(disabledSvg.begin(), disabledSvg.end());
    button.activeIconData.assign(enabledSvg.begin(), enabledSvg.end());
    button.onEvent = onButtonEvent;

    if (!pl::modmenu::registerButton(button)) {
        return false;
    }

    g_buttonRegistered.store(true, std::memory_order_release);
    return true;
}

void onConfigChanged(std::string_view, std::string_view key, std::string_view value) {
    if (key == kUseCustomButtonKey) {
        const bool useCustomButton =
            value == "true" || value == "1" || value == "on" || value == "enabled";

        g_useCustomButton.store(useCustomButton, std::memory_order_relaxed);
        setFreeLookEnabled(false);

        if (useCustomButton) {
            if (!registerCustomButton()) {
                g_useCustomButton.store(false, std::memory_order_relaxed);
            }
        } else {
            unregisterCustomButton();
        }

        return;
    }

    if (key == kButtonOpacityKey) {
        const float current = g_buttonOpacity.load(std::memory_order_relaxed);
        const float opacity = parseOpacity(value, current);
        g_buttonOpacity.store(opacity, std::memory_order_relaxed);

        if (g_useCustomButton.load(std::memory_order_relaxed)) {
            unregisterCustomButton();
            if (!registerCustomButton()) {
                g_useCustomButton.store(false, std::memory_order_relaxed);
            }
        }
    }
}

void onKeybind(std::string_view, std::string_view, bool isDown) {
    if (!isDown) return;
    if (g_useCustomButton.load(std::memory_order_relaxed)) return;
    if (!g_lifecycleEnabled.load(std::memory_order_relaxed)) return;
    if (!g_moduleEnabled.load(std::memory_order_relaxed)) return;

    // The module owns exactly one keybind entry, so any keybind callback is
    // the user's selected keyboard/mouse trigger. Toggle on press only.
    toggleFreeLook();
}

void onButtonEvent(std::string_view, pl::modmenu::ButtonEvent event, float value) {
    if (event != pl::modmenu::ButtonEvent::StateChanged) return;
    if (!g_useCustomButton.load(std::memory_order_relaxed)) return;
    if (!g_lifecycleEnabled.load(std::memory_order_relaxed)) return;
    if (!g_moduleEnabled.load(std::memory_order_relaxed)) return;

    setFreeLookEnabled(value >= 0.5f);
}

bool registerUi() {
    pl::modmenu::ModuleInfo module{};
    module.moduleId = kModuleId;
    module.displayName = "Better Third Person (FreeLook base)";
    module.description =
        "Look around independently without changing movement direction. "
        "Choose a custom HUD button or a rebindable keyboard/mouse trigger.";
    module.modId = kModId;
    module.defaultEnabled = true;
    module.hideInHudEditor = false;

    pl::modmenu::ConfigEntry triggerMode{};
    triggerMode.key = kUseCustomButtonKey;
    triggerMode.displayName = "Use Custom Button";
    triggerMode.type = pl::modmenu::ConfigType::Toggle;
    triggerMode.defaultValue = "true";
    module.configs.push_back(triggerMode);

    pl::modmenu::ConfigEntry keybind{};
    keybind.key = "freelook_keybind";
    keybind.displayName = "Keyboard / Mouse Keybind";
    keybind.type = pl::modmenu::ConfigType::Keybind;
    keybind.defaultValue = "F6";
    module.configs.push_back(keybind);

    pl::modmenu::ConfigEntry opacity{};
    opacity.key = kButtonOpacityKey;
    opacity.displayName = "Custom Button Opacity (%)";
    opacity.type = pl::modmenu::ConfigType::SliderFloat;
    opacity.defaultValue = "100";
    opacity.minValue = "0";
    opacity.maxValue = "100";
    opacity.dependsOn = kUseCustomButtonKey;
    module.configs.push_back(opacity);

    module.onToggle = onModuleToggle;
    module.onConfigChanged = onConfigChanged;
    module.onKeybind = onKeybind;

    if (!pl::modmenu::registerModule(module)) {
        return false;
    }

    g_buttonRegistered.store(false, std::memory_order_relaxed);
    if (g_useCustomButton.load(std::memory_order_relaxed) && !registerCustomButton()) {
        pl::modmenu::unregisterModule(kModuleId);
        return false;
    }

    return true;
}

void unregisterUi() {
    unregisterCustomButton();
    pl::modmenu::unregisterModule(kModuleId);
}

uintptr_t nxcResolveSig(const char* sig, const char* module) {
    return ::pl::memory::resolveSignature(sig ? std::string_view(sig) : std::string_view{}, module ? std::string_view(module) : std::string_view{});
};

} // namespace

namespace nexcaise {

void executeTheStuff() {
    ::executeTheStuff();
}

LL_TYPED_HOOK(
    FreeLookE,
    memory::HookPriority::Normal,
    Actor,
    g_addressThree,
    "libminecraftpe.so",
    void
) {
    executeTheStuff();
    origin();
    executeTheStuff();
}

LL_STATIC_HOOK(
    FreeLookD,
    memory::HookPriority::Normal,
    pl::memory::resolveVtableFunction("16VanillaCameraAPI", 7, "libminecraftpe.so"),
    "libminecraftpe.so",
    int, void* a1
) {
    if(!g_lifecycleEnabled.load(std::memory_order_relaxed) || !g_moduleEnabled.load(std::memory_order_relaxed) || !g_freelookEnabled.load(std::memory_order_relaxed)) return origin(a1);
    return 1;
}

LL_TYPED_HOOK(
    FreeLookC,
    memory::HookPriority::Normal,
    Actor,
    g_addressTwo,
    "libminecraftpe.so",
    void
) {
    // Lock both before and after the camera/player update. The pre-lock stops
    // the original routine from starting with a moving head, while the post-lock
    // restores the value in case the routine writes the camera yaw into the head.
    executeTheStuff();
    origin();
    executeTheStuff();
};

LL_STATIC_HOOK(
    FreeLookA,
    memory::HookPriority::Normal,
    g_addressOne,
    "libminecraftpe.so",
    void, CameraComponent* cameraComponent, void* a2, void* a3
) {
    if(!g_lifecycleEnabled.load(std::memory_order_relaxed) || !g_moduleEnabled.load(std::memory_order_relaxed) || !g_freelookEnabled.load(std::memory_order_relaxed)) origin(cameraComponent, a2, a3);
}

LL_TYPED_HOOK(
    FreeLookB,
    memory::HookPriority::Normal,
    ClientInstance,
    pl::memory::resolveVtableFunction("14ClientInstance", 25, "libminecraftpe.so"),
    "libminecraftpe.so",
    bool, bool isInitFinished
) {
    if (isInitFinished) {
        g_clientInstance = (ClientInstance*)this;

        auto& self = ::nexcaise::FreeLook::getInstance().getSelf();

        if (FreeLookA::hook() != 0 || FreeLookC::hook() != 0 || FreeLookD::hook() != 0 || FreeLookE::hook() != 0) {
            FreeLookE::unhook();
            FreeLookD::unhook();
            FreeLookC::unhook();
            FreeLookA::unhook();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to install one or more FreeLook hooks");
        }
    
        if (!registerUi()) {
            FreeLookE::unhook();
            FreeLookD::unhook();
            FreeLookC::unhook();
            FreeLookA::unhook();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to register FreeLook mod-menu controls");
        }
    
        self.getLogger().info("FreeLook enabled");
    }
    return origin(isInitFinished);
};

FreeLook& FreeLook::getInstance() {
    static FreeLook instance;
    return instance;
}

FreeLook::FreeLook() : mSelf(*ll::mod::NativeMod::current()) {}

bool FreeLook::load() {
    auto& self = getSelf();
    self.getLogger().info("Loading...");

    g_lifecycleEnabled.store(true, std::memory_order_relaxed);
    g_moduleEnabled.store(true, std::memory_order_relaxed);
    g_freelookEnabled.store(false, std::memory_order_relaxed);
    g_headCapturePending.store(false, std::memory_order_relaxed);
    g_headRotationLocked.store(false, std::memory_order_relaxed);
    g_lockedHeadYaw.store(0.0f, std::memory_order_relaxed);

    g_addressOne = nxcResolveSig("FF C3 01 D1 ? ? ? FD ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 FD E3 00 91 54", "libminecraftpe.so");
    g_addressTwo = nxcResolveSig("? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39", "libminecraftpe.so");
    g_addressThree = nxcResolveSig("FF 83 06 D1 ? ? ? FD ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 43 05 91 57 D0 3B D5 F3", "libminecraftpe.so");

    return true;
}

bool FreeLook::enable() {
    auto& self = getSelf();
    self.getLogger().info("Enabling...");

    if (g_addressThree == 0 || g_addressTwo == 0 || g_addressOne == 0) return false;

    return FreeLookB::hook() == 0;
}

bool FreeLook::disable() {
    getSelf().getLogger().info("Disabling...");

    g_lifecycleEnabled.store(false, std::memory_order_relaxed);
    g_moduleEnabled.store(false, std::memory_order_relaxed);
    setFreeLookEnabled(false);
    unregisterUi();

    FreeLookE::unhook();
    FreeLookD::unhook();
    FreeLookC::unhook();
    FreeLookA::unhook();
    FreeLookB::unhook();

    return true;
}

bool FreeLook::unload() {
    getSelf().getLogger().info("Unloading...");

    g_clientInstance = nullptr;
    g_headCapturePending.store(false, std::memory_order_release);
    g_headRotationLocked.store(false, std::memory_order_release);
    g_lockedHeadYaw.store(0.0f, std::memory_order_relaxed);

    return true;
}

} // namespace nexcaise
