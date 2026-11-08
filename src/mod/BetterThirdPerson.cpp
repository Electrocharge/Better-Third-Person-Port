/*
 * Better Third Person (Bedrock / LeviLaunchroid native port)
 *
 * Inspired by the Java mod "Better Third Person" (feature description only, no
 * code taken from it). Derivative work based on the FreeLook native mod by
 * NexCaise (CPNC-1.0, see files/LICENSE): the hooks, the head lock, the mod-menu
 * setup and the HUD-button code come from FreeLook. NOT an official version of
 * FreeLook and not affiliated with the Java mod or its authors.
 *
 * The on-screen button works exactly like FreeLook's button (same four hooks,
 * same head lock). On top of that there is one extra Mod Menu toggle:
 *
 *   "Better Third Person movement" (off by default):
 *     while the button is ON, the game's own "camera relative" control scheme is
 *     forced for the input-transform step, so movement follows the camera and
 *     the character turns toward the movement direction. The head lock is
 *     released in that case so the character can turn.
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
const char *kBtpMovementKey = "btp_movement";

// ControlSchemeComponent byte values handled by the game's input transform
// (verified in libminecraftpe.so): 1 = camera relative (turns the player toward
// movement), 2 = camera relative strafe (keeps the player's rotation).
constexpr uint8_t kSchemeCameraRelative = 1;

// ---- shared state (same shape as FreeLook) ---------------------------------------
std::atomic_bool g_lifecycleEnabled{false};
std::atomic_bool g_moduleEnabled{false};
std::atomic_bool g_enabled{false};           // the on-screen button state (FreeLook: g_freelookEnabled)
std::atomic_bool g_useCustomButton{true};
std::atomic_bool g_buttonRegistered{false};
std::atomic<float> g_buttonOpacity{1.0f};
std::atomic_bool g_btpMovement{false};       // extra: game camera-relative scheme while ON
std::mutex g_buttonRegistryMutex;
std::atomic_bool g_headCapturePending{false};
std::atomic_bool g_headRotationLocked{false};
std::atomic<float> g_lockedHeadYaw{0.0f};
std::atomic_bool g_schemeHookInstalled{false};

uintptr_t g_addressOne = 0;     // FreeLook hook A: camera -> player rotation sync
uintptr_t g_addressTwo = 0;     // FreeLook hook C: LocalPlayer tick
uintptr_t g_addressThree = 0;   // FreeLook hook E: Mob tick
uintptr_t g_addressScheme = 0;  // CameraInputTransformSystem per-entity function (optional)

void onButtonEvent(std::string_view, pl::modmenu::ButtonEvent event, float value);

bool featureOn() noexcept {
    return g_lifecycleEnabled.load(std::memory_order_relaxed) &&
           g_moduleEnabled.load(std::memory_order_relaxed) &&
           g_enabled.load(std::memory_order_relaxed);
}

bool movementModeOn() noexcept { return featureOn() && g_btpMovement.load(std::memory_order_relaxed); }

void logInfo(const std::string &msg) {
    btp::BetterThirdPerson::getInstance().getSelf().getLogger().info("{}", msg);
}

void setEnabled(bool enabled) noexcept {
    g_enabled.store(enabled, std::memory_order_release);

    if (enabled) {
        // The actual head angle is captured on the game thread at the first available update.
        g_headCapturePending.store(true, std::memory_order_release);
        g_headRotationLocked.store(false, std::memory_order_release);
    } else {
        g_headCapturePending.store(false, std::memory_order_release);
        g_headRotationLocked.store(false, std::memory_order_release);
    }
}

bool captureHeadRotation(Actor *player) noexcept {
    if (!player || !g_enabled.load(std::memory_order_acquire)) return false;
    if (!g_headCapturePending.load(std::memory_order_acquire)) return false;

    auto *headRotation = player->tryGetComponent<ActorHeadRotationComponent>();
    if (!headRotation || !std::isfinite(headRotation->mYHeadRot)) return false;

    g_lockedHeadYaw.store(headRotation->mYHeadRot, std::memory_order_relaxed);
    g_headCapturePending.store(false, std::memory_order_release);
    g_headRotationLocked.store(true, std::memory_order_release);
    return true;
}

void lockHeadRotation(Actor *player) noexcept {
    if (!player || !g_enabled.load(std::memory_order_acquire)) return;

    auto *headRotation = player->tryGetComponent<ActorHeadRotationComponent>();
    if (!headRotation) return;
    if (!g_headRotationLocked.load(std::memory_order_acquire)) return;

    const float lockedYaw = g_lockedHeadYaw.load(std::memory_order_relaxed);
    if (!std::isfinite(lockedYaw)) return;

    headRotation->mYHeadRotOld = lockedYaw;
    headRotation->mYHeadRot = lockedYaw;
}

void toggleEnabled() noexcept { setEnabled(!g_enabled.load(std::memory_order_relaxed)); }

// FreeLook's executeTheStuff. In "movement" mode the head lock is skipped so the
// game's own camera-relative transform can turn the character.
void executeTheStuff() {
    if (!featureOn()) return;
    if (g_btpMovement.load(std::memory_order_relaxed)) return;
    if (!g_clientInstance) return;

    Actor *globalPlayer = g_clientInstance->getLocalPlayer();
    if (!globalPlayer) return;

    captureHeadRotation(globalPlayer);
    lockHeadRotation(globalPlayer);
}

void onModuleToggle(std::string_view, bool enabled) {
    g_moduleEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) setEnabled(false);
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

void onConfigChanged(std::string_view, std::string_view key, std::string_view value) {
    if (key == kUseCustomButtonKey) {
        const bool useCustomButton = value == "true" || value == "1" || value == "on" || value == "enabled";
        g_useCustomButton.store(useCustomButton, std::memory_order_relaxed);
        setEnabled(false);

        if (useCustomButton) {
            if (!registerCustomButton()) g_useCustomButton.store(false, std::memory_order_relaxed);
        } else {
            unregisterCustomButton();
        }
        return;
    }

    if (key == kBtpMovementKey) {
        g_btpMovement.store(value == "true" || value == "1" || value == "on" || value == "enabled",
                            std::memory_order_relaxed);
        // Re-capture the head if the user goes back to plain FreeLook while it is ON.
        if (g_enabled.load(std::memory_order_relaxed)) setEnabled(true);
        return;
    }

    if (key == kButtonOpacityKey) {
        const float current = g_buttonOpacity.load(std::memory_order_relaxed);
        const float opacity = parseOpacity(value, current);
        g_buttonOpacity.store(opacity, std::memory_order_relaxed);

        if (g_useCustomButton.load(std::memory_order_relaxed)) {
            unregisterCustomButton();
            if (!registerCustomButton()) g_useCustomButton.store(false, std::memory_order_relaxed);
        }
    }
}

void onKeybind(std::string_view, std::string_view, bool isDown) {
    if (!isDown) return;
    if (g_useCustomButton.load(std::memory_order_relaxed)) return;
    if (!g_lifecycleEnabled.load(std::memory_order_relaxed)) return;
    if (!g_moduleEnabled.load(std::memory_order_relaxed)) return;
    toggleEnabled();
}

void onButtonEvent(std::string_view, pl::modmenu::ButtonEvent event, float value) {
    if (event != pl::modmenu::ButtonEvent::StateChanged) return;
    if (!g_useCustomButton.load(std::memory_order_relaxed)) return;
    if (!g_lifecycleEnabled.load(std::memory_order_relaxed)) return;
    if (!g_moduleEnabled.load(std::memory_order_relaxed)) return;

    setEnabled(value >= 0.5f);
}

bool registerUi() {
    pl::modmenu::ModuleInfo module{};
    module.moduleId = kModuleId;
    module.displayName = "Better Third Person";
    module.description =
        "Free camera look-around (FreeLook behaviour). Turn on \"Better Third Person movement\" so movement "
        "follows the camera and your character turns toward where you move.";
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
    keybind.key = "btp_keybind";
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

    pl::modmenu::ConfigEntry movement{};
    movement.key = kBtpMovementKey;
    movement.displayName = "Better Third Person movement (experimental)";
    movement.type = pl::modmenu::ConfigType::Toggle;
    movement.defaultValue = "false";
    module.configs.push_back(movement);

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

void executeTheStuff() { ::executeTheStuff(); }

// ---- FreeLook hooks (unchanged behaviour) -----------------------------------------
LL_TYPED_HOOK(FreeLookE, memory::HookPriority::Normal, Actor, g_addressThree, "libminecraftpe.so", void) {
    executeTheStuff();
    origin();
    executeTheStuff();
}

LL_STATIC_HOOK(
    FreeLookD,
    memory::HookPriority::Normal,
    pl::memory::resolveVtableFunction("16VanillaCameraAPI", 7, "libminecraftpe.so"),
    "libminecraftpe.so",
    int, void *a1
) {
    if (!featureOn()) return origin(a1);
    return 1;
}

LL_TYPED_HOOK(FreeLookC, memory::HookPriority::Normal, Actor, g_addressTwo, "libminecraftpe.so", void) {
    executeTheStuff();
    origin();
    executeTheStuff();
}

LL_STATIC_HOOK(
    FreeLookA,
    memory::HookPriority::Normal,
    g_addressOne,
    "libminecraftpe.so",
    void, void *cameraComponent, void *a2, void *a3
) {
    if (!featureOn()) origin(cameraComponent, a2, a3);
}

// ---- Extra hook: the game's camera-relative control scheme ---------------------------
// CameraInputTransformSystem per-entity function.
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
    if (!movementModeOn() || !readable(scheme, 1)) {
        origin(a0, a1, a2, a3, a4, a5, a6, a7);
        return;
    }

    const uint8_t oldScheme = *scheme;
    *scheme = kSchemeCameraRelative;
    origin(a0, a1, a2, a3, a4, a5, a6, a7);
    *scheme = oldScheme;
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
        g_clientInstance = (ClientInstance *) this;

        auto &self = BetterThirdPerson::getInstance().getSelf();

        if (FreeLookA::hook() != 0 || FreeLookC::hook() != 0 || FreeLookD::hook() != 0 || FreeLookE::hook() != 0) {
            FreeLookE::unhook();
            FreeLookD::unhook();
            FreeLookC::unhook();
            FreeLookA::unhook();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to install one or more FreeLook hooks");
        }

        // Optional: never allowed to break the FreeLook part.
        if (g_addressScheme != 0 && BtpScheme::hook() == 0) {
            g_schemeHookInstalled.store(true, std::memory_order_relaxed);
        } else {
            self.getLogger().warn("Camera-relative scheme hook unavailable; only the FreeLook part works");
        }

        if (!registerUi()) {
            BtpScheme::unhook();
            FreeLookE::unhook();
            FreeLookD::unhook();
            FreeLookC::unhook();
            FreeLookA::unhook();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to register Better Third Person mod-menu controls");
        }

        self.getLogger().info("Better Third Person enabled");
    }
    return origin(isInitFinished);
}

BetterThirdPerson &BetterThirdPerson::getInstance() {
    static BetterThirdPerson instance;
    return instance;
}

BetterThirdPerson::BetterThirdPerson() : mSelf(*ll::mod::NativeMod::current()) {}

bool BetterThirdPerson::load() {
    auto &self = getSelf();
    self.getLogger().info("Loading...");

    g_lifecycleEnabled.store(true, std::memory_order_relaxed);
    g_moduleEnabled.store(true, std::memory_order_relaxed);
    g_enabled.store(false, std::memory_order_relaxed);
    g_headCapturePending.store(false, std::memory_order_relaxed);
    g_headRotationLocked.store(false, std::memory_order_relaxed);
    g_lockedHeadYaw.store(0.0f, std::memory_order_relaxed);

    // FreeLook signatures (unchanged).
    g_addressOne = resolveSig("FF C3 01 D1 ? ? ? FD ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 FD E3 00 91 54", "libminecraftpe.so");
    g_addressTwo = resolveSig(
        "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39",
        "libminecraftpe.so");
    g_addressThree = resolveSig(
        "FF 83 06 D1 ? ? ? FD ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 43 05 91 57 D0 3B D5 F3",
        "libminecraftpe.so");

    // CameraInputTransformSystem per-entity function: try progressively shorter patterns.
    static const char *const kSchemeSigs[] = {
        "EB 2B BB 6D E9 23 01 6D FD 7B 02 A9 F6 57 03 A9 F4 4F 04 A9 FD 83 00 91 F4 03 01 AA 81 82 C4 2D 21 08 21 1E 02 08 20 1E",
        "EB 2B BB 6D E9 23 01 6D FD 7B 02 A9 F6 57 03 A9 F4 4F 04 A9 FD 83 00 91 F4 03 01 AA 81 82 C4 2D",
        "EB 2B BB 6D E9 23 01 6D FD 7B 02 A9 F6 57 03 A9 F4 4F 04 A9",
    };
    g_addressScheme = 0;
    for (const char *sig : kSchemeSigs) {
        g_addressScheme = resolveSig(sig, "libminecraftpe.so");
        if (g_addressScheme != 0) break;
    }

    return true;
}

bool BetterThirdPerson::enable() {
    auto &self = getSelf();
    self.getLogger().info("Enabling...");

    // Only FreeLook's own signatures are required.
    if (g_addressThree == 0 || g_addressTwo == 0 || g_addressOne == 0) return false;

    return FreeLookB::hook() == 0;
}

bool BetterThirdPerson::disable() {
    getSelf().getLogger().info("Disabling...");

    g_lifecycleEnabled.store(false, std::memory_order_relaxed);
    g_moduleEnabled.store(false, std::memory_order_relaxed);
    setEnabled(false);
    unregisterUi();

    BtpScheme::unhook();
    FreeLookE::unhook();
    FreeLookD::unhook();
    FreeLookC::unhook();
    FreeLookA::unhook();
    FreeLookB::unhook();

    return true;
}

bool BetterThirdPerson::unload() {
    getSelf().getLogger().info("Unloading...");

    g_clientInstance = nullptr;
    g_headCapturePending.store(false, std::memory_order_release);
    g_headRotationLocked.store(false, std::memory_order_release);
    g_lockedHeadYaw.store(0.0f, std::memory_order_relaxed);

    return true;
}

} // namespace btp
