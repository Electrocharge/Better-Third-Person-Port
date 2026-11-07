/*
 * Better Third Person (Bedrock / LeviLaunchroid native port)
 *
 * Inspired by the Java mod "Better Third Person" (feature description only, no
 * code taken from it). Derivative work based on the FreeLook native mod by
 * NexCaise (CPNC-1.0, see files/LICENSE): the camera hooks, the mod-menu setup
 * and the HUD-button code come from FreeLook. NOT an official version of
 * FreeLook and not affiliated with the Java mod or its authors.
 *
 * How it works: Bedrock's own CameraInputTransformSystem already knows a
 * "camera relative" control scheme that (a) converts the joystick/keys into
 * camera-relative movement and (b) turns the player toward the movement
 * direction. While the HUD button is ON we
 *   1. force that control scheme for the movement-input step,
 *   2. stop the camera from dragging the player's rotation (FreeLook hook A),
 *   3. keep the camera "free" (FreeLook hook D).
 * Result: free orbit camera, character walks/turns toward where you move.
 */
#include "mod/BetterThirdPerson.h"
#include "mod/icons.hpp"
#include "mc/ClientInstance.h"
#include "pl_modmenu_compat.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

static ClientInstance *g_clientInstance = nullptr;

namespace {
// ---- ids ------------------------------------------------------------------
const char *kModuleId = "betterthirdperson.main";
const char *kModId = "BetterThirdPerson";
const char *kButtonId = "betterthirdperson.toggle";
const char *kUseCustomButtonKey = "use_custom_button";
const char *kButtonOpacityKey = "button_opacity";

// ControlSchemeComponent byte values handled by the game's input transform
// (verified in libminecraftpe.so): 1 = camera relative (turns the player toward
// movement), 2 = camera relative strafe (keeps the player's rotation).
constexpr uint8_t kSchemeCameraRelative = 1;

// ---- shared state -----------------------------------------------------------
std::atomic_bool g_lifecycleEnabled{false};
std::atomic_bool g_moduleEnabled{false};
std::atomic_bool g_active{false}; // the on-screen button / keybind state
std::atomic_bool g_useCustomButton{true};
std::atomic_bool g_buttonRegistered{false};
std::atomic<float> g_buttonOpacity{1.0f};
std::atomic_bool g_logNextTransform{false};
std::mutex g_buttonRegistryMutex;

uintptr_t g_addressCameraA = 0;  // camera -> player rotation sync (FreeLook hook A)
uintptr_t g_addressScheme = 0;   // CameraInputTransformSystem per-entity function

void onButtonEvent(std::string_view, pl::modmenu::ButtonEvent event, float value);

bool featureOn() noexcept {
    return g_lifecycleEnabled.load(std::memory_order_relaxed) &&
           g_moduleEnabled.load(std::memory_order_relaxed) &&
           g_active.load(std::memory_order_relaxed);
}

void setActive(bool on) noexcept {
    g_active.store(on, std::memory_order_release);
    g_logNextTransform.store(on, std::memory_order_release);
}

// Cheap "is this memory mapped" test: write() copies from the buffer and fails
// with EFAULT instead of crashing if the pointer is bad.
bool readable(const void *p, size_t n) noexcept {
    static int fds[2] = {-1, -1};
    if (!p || n == 0 || n > 64) return false;
    if (fds[0] < 0 && pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        fds[0] = -1;
        return false;
    }
    const ssize_t w = ::write(fds[1], p, n);
    if (w < 0) return false;
    char sink[64];
    (void) ::read(fds[0], sink, sizeof(sink));
    return true;
}

// ---- HUD button (from FreeLook, new icons) -----------------------------------
float parseOpacity(std::string_view value, float fallback) noexcept {
    std::string text(value);
    char *end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(parsed)) return fallback;
    return std::clamp(parsed, 0.0f, 100.0f) / 100.0f;
}

std::string applySvgOpacity(std::string_view svg, float opacity) {
    std::string result(svg);
    const std::size_t svgTagEnd = result.find('>');
    if (svgTagEnd == std::string::npos) return result;
    const std::size_t svgTagStart = result.find("<svg");
    if (svgTagStart == std::string::npos || svgTagStart > svgTagEnd) return result;
    const std::string attribute = " opacity=\"" + std::to_string(opacity) + "\"";
    result.insert(svgTagEnd, attribute);
    return result;
}

void unregisterCustomButton() {
    std::lock_guard lock(g_buttonRegistryMutex);
    if (!g_buttonRegistered.exchange(false, std::memory_order_acq_rel)) return;
    pl::modmenu::unregisterButton(kButtonId);
}

bool registerCustomButton() {
    std::lock_guard lock(g_buttonRegistryMutex);
    if (g_buttonRegistered.load(std::memory_order_acquire)) return true;
    if (!g_useCustomButton.load(std::memory_order_relaxed)) return true;

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
    const std::string disabledSvg = applySvgOpacity(btp::icons::kDisabledSvg, opacity);
    const std::string enabledSvg = applySvgOpacity(btp::icons::kEnabledSvg, opacity);
    button.iconData.assign(disabledSvg.begin(), disabledSvg.end());
    button.activeIconData.assign(enabledSvg.begin(), enabledSvg.end());
    button.onEvent = onButtonEvent;

    if (!pl::modmenu::registerButton(button)) return false;
    g_buttonRegistered.store(true, std::memory_order_release);
    return true;
}

void onModuleToggle(std::string_view, bool enabled) {
    g_moduleEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) setActive(false);
}

void onConfigChanged(std::string_view, std::string_view key, std::string_view value) {
    if (key == kUseCustomButtonKey) {
        const bool use = value == "true" || value == "1" || value == "on" || value == "enabled";
        g_useCustomButton.store(use, std::memory_order_relaxed);
        setActive(false);
        if (use) {
            if (!registerCustomButton()) g_useCustomButton.store(false, std::memory_order_relaxed);
        } else {
            unregisterCustomButton();
        }
        return;
    }

    if (key == kButtonOpacityKey) {
        const float current = g_buttonOpacity.load(std::memory_order_relaxed);
        g_buttonOpacity.store(parseOpacity(value, current), std::memory_order_relaxed);
        if (g_useCustomButton.load(std::memory_order_relaxed)) {
            unregisterCustomButton();
            if (!registerCustomButton()) g_useCustomButton.store(false, std::memory_order_relaxed);
        }
    }
}

void onKeybind(std::string_view, std::string_view, bool isDown) {
    if (!isDown) return;
    if (g_useCustomButton.load(std::memory_order_relaxed)) return; // button mode: ignore keybind
    if (!g_lifecycleEnabled.load(std::memory_order_relaxed)) return;
    if (!g_moduleEnabled.load(std::memory_order_relaxed)) return;
    setActive(!g_active.load(std::memory_order_relaxed));
}

void onButtonEvent(std::string_view, pl::modmenu::ButtonEvent event, float value) {
    if (event != pl::modmenu::ButtonEvent::StateChanged) return;
    if (!g_useCustomButton.load(std::memory_order_relaxed)) return;
    if (!g_lifecycleEnabled.load(std::memory_order_relaxed)) return;
    if (!g_moduleEnabled.load(std::memory_order_relaxed)) return;
    setActive(value >= 0.5f);
}

bool registerUi() {
    pl::modmenu::ModuleInfo module{};
    module.moduleId = kModuleId;
    module.displayName = "Better Third Person";
    module.description =
        "Free-orbit third person: the camera turns independently and your character turns to face "
        "the direction you move. Toggle it with the on-screen button (use it in third person).";
    module.modId = kModId;
    module.defaultEnabled = true;
    module.hideInHudEditor = false;

    pl::modmenu::ConfigEntry triggerMode{};
    triggerMode.key = kUseCustomButtonKey;
    triggerMode.displayName = "Use On-Screen Button";
    triggerMode.type = pl::modmenu::ConfigType::Toggle;
    triggerMode.defaultValue = "true";
    module.configs.push_back(triggerMode);

    pl::modmenu::ConfigEntry keybind{};
    keybind.key = "btp_keybind";
    keybind.displayName = "Keyboard / Mouse Keybind (when button is off)";
    keybind.type = pl::modmenu::ConfigType::Keybind;
    keybind.defaultValue = "F8";
    module.configs.push_back(keybind);

    pl::modmenu::ConfigEntry opacity{};
    opacity.key = kButtonOpacityKey;
    opacity.displayName = "Button Opacity (%)";
    opacity.type = pl::modmenu::ConfigType::SliderFloat;
    opacity.defaultValue = "100";
    opacity.minValue = "0";
    opacity.maxValue = "100";
    opacity.dependsOn = kUseCustomButtonKey;
    module.configs.push_back(opacity);

    module.onToggle = onModuleToggle;
    module.onConfigChanged = onConfigChanged;
    module.onKeybind = onKeybind;

    if (!pl::modmenu::registerModule(module)) return false;

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

uintptr_t resolveSig(const char *sig, const char *module) {
    return ::pl::memory::resolveSignature(sig ? std::string_view(sig) : std::string_view{},
                                          module ? std::string_view(module) : std::string_view{});
}

} // namespace

namespace btp {

// Hook 1: CameraInputTransformSystem per-entity function.
// Args: x0 StrictEntityContext, x1 MoveInputComponent, x2 ControlSchemeComponent,
//       x3 ActorRotationComponent, x4 ActorHeadRotationComponent.
LL_STATIC_HOOK(
    BtpScheme,
    memory::HookPriority::Normal,
    g_addressScheme,
    "libminecraftpe.so",
    void, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7
) {
    auto *scheme = static_cast<uint8_t *>(a2);
    if (!featureOn() || !readable(scheme, 1)) {
        origin(a0, a1, a2, a3, a4, a5, a6, a7);
        return;
    }

    const uint8_t oldScheme = *scheme;
    *scheme = kSchemeCameraRelative;
    origin(a0, a1, a2, a3, a4, a5, a6, a7);
    *scheme = oldScheme;

    if (g_logNextTransform.exchange(false, std::memory_order_relaxed)) {
        float move[2] = {0, 0};
        float cam[3] = {0, 0, 0};
        if (a1 && readable(static_cast<char *>(a1) + 0x24, 8)) std::memcpy(move, static_cast<char *>(a1) + 0x24, 8);
        if (a1 && readable(static_cast<char *>(a1) + 0x54, 12)) std::memcpy(cam, static_cast<char *>(a1) + 0x54, 12);
        BetterThirdPerson::getInstance().getSelf().getLogger().info(
            "BTP active: original scheme {}, move=({:.2f},{:.2f}) camVec=({:.2f},{:.2f},{:.2f})",
            static_cast<int>(oldScheme), move[0], move[1], cam[0], cam[1], cam[2]);
    }
}

// Hook 2 (FreeLook hook A): the camera -> player rotation sync. Skipped while ON so the
// camera can orbit without dragging the player's rotation.
LL_STATIC_HOOK(
    BtpCameraSync,
    memory::HookPriority::Normal,
    g_addressCameraA,
    "libminecraftpe.so",
    void, void *cameraComponent, void *a2, void *a3
) {
    if (!featureOn()) origin(cameraComponent, a2, a3);
}

// Hook 3 (FreeLook hook D): VanillaCameraAPI slot 7 reports 1 while ON.
LL_STATIC_HOOK(
    BtpCameraFree,
    memory::HookPriority::Normal,
    pl::memory::resolveVtableFunction("16VanillaCameraAPI", 7, "libminecraftpe.so"),
    "libminecraftpe.so",
    int, void *a1
) {
    if (!featureOn()) return origin(a1);
    return 1;
}

// ClientInstance init-finished: grab the client instance, install hooks, register the UI.
LL_TYPED_HOOK(
    BtpClientInit,
    memory::HookPriority::Normal,
    ClientInstance,
    pl::memory::resolveVtableFunction("14ClientInstance", 25, "libminecraftpe.so"),
    "libminecraftpe.so",
    bool, bool isInitFinished
) {
    if (isInitFinished) {
        g_clientInstance = (ClientInstance *) this;
        auto &self = BetterThirdPerson::getInstance().getSelf();

        auto unhookAll = [] {
            BtpCameraFree::unhook();
            BtpCameraSync::unhook();
            BtpScheme::unhook();
        };

        if (BtpScheme::hook() != 0 || BtpCameraSync::hook() != 0 || BtpCameraFree::hook() != 0) {
            unhookAll();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to install Better Third Person hooks");
        } else if (!registerUi()) {
            unhookAll();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to register Better Third Person mod-menu controls");
        } else {
            self.getLogger().info("Better Third Person ready (use the on-screen button)");
        }
    }
    return origin(isInitFinished);
}

BetterThirdPerson &BetterThirdPerson::getInstance() {
    static BetterThirdPerson instance;
    return instance;
}

BetterThirdPerson::BetterThirdPerson() : mSelf(*ll::mod::NativeMod::current()) {}

bool BetterThirdPerson::load() {
    getSelf().getLogger().info("Loading...");

    g_lifecycleEnabled.store(true, std::memory_order_relaxed);
    g_moduleEnabled.store(true, std::memory_order_relaxed);
    g_active.store(false, std::memory_order_relaxed);

    // FreeLook hook A signature (same game build).
    g_addressCameraA = resolveSig("FF C3 01 D1 ? ? ? FD ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 FD E3 00 91 54", "libminecraftpe.so");
    // CameraInputTransformSystem per-entity function (unique in this build).
    g_addressScheme = resolveSig(
        "EB 2B BB 6D E9 23 01 6D FD 7B 02 A9 F6 57 03 A9 F4 4F 04 A9 FD 83 00 91 F4 03 01 AA 81 82 C4 2D 21 08 21 1E 02 08 20 1E",
        "libminecraftpe.so");
    return true;
}

bool BetterThirdPerson::enable() {
    getSelf().getLogger().info("Enabling...");
    if (g_addressCameraA == 0 || g_addressScheme == 0) {
        getSelf().getLogger().error("Signatures not found (game version changed?)");
        return false;
    }
    return BtpClientInit::hook() == 0;
}

bool BetterThirdPerson::disable() {
    getSelf().getLogger().info("Disabling...");

    g_lifecycleEnabled.store(false, std::memory_order_relaxed);
    g_moduleEnabled.store(false, std::memory_order_relaxed);
    setActive(false);
    unregisterUi();

    BtpCameraFree::unhook();
    BtpCameraSync::unhook();
    BtpScheme::unhook();
    BtpClientInit::unhook();
    return true;
}

bool BetterThirdPerson::unload() {
    getSelf().getLogger().info("Unloading...");
    g_clientInstance = nullptr;
    return true;
}

} // namespace btp
