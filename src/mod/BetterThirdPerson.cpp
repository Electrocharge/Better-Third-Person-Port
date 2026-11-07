/*
 * Better Third Person (Bedrock / LeviLaunchroid native port)
 *
 * Inspired by the Java mod "Better Third Person" (feature description only, no
 * code taken from it). Derivative work based on the FreeLook native mod by
 * NexCaise (CPNC-1.0, see files/LICENSE): the camera hooks, the mod-menu setup
 * and the HUD-button code come from FreeLook. NOT an official version of
 * FreeLook and not affiliated with the Java mod or its authors.
 *
 * How it works (two modes, switch in the Mod Menu):
 *
 *  HEAD mode (default, built on FreeLook's proven tick hooks): the camera and your
 *  movement stay vanilla (movement is camera-relative, aiming is unchanged). Each tick the
 *  mod reads the raw movement input, works out which way you are really heading
 *  (camera yaw minus the input angle - the same formula the game uses for its own
 *  camera-relative scheme) and smoothly steers the head/body yaw toward it, exactly where
 *  FreeLook locks the head yaw.
 *
 *  GAME-SCHEME mode (experimental): forces the game's own "camera relative" control scheme
 *  and uses FreeLook's camera hooks so the camera is fully detached from the player.
 */
#include "mod/BetterThirdPerson.h"
#include "mod/icons.hpp"
#include "mc/ClientInstance.h"
#include "mc/MobBodyRotationComponent.h"
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
const char *kModeKey = "mode";
const char *kTurnSpeedKey = "turn_speed";

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
std::atomic<int> g_mode{0};                   // 0 = FreeLook (original), 1 = BTP game scheme, 2 = BTP head-only
std::atomic<float> g_turnSpeed{30.0f};        // degrees per tick (head mode)
std::mutex g_buttonRegistryMutex;

// Latest raw input/rotation seen by the game's input-transform function (written there, read in tick hooks).
std::atomic<float> g_inX{0.0f};
std::atomic<float> g_inY{0.0f};
std::atomic<float> g_playerYaw{0.0f};
std::atomic_bool g_haveInput{false};
std::atomic_bool g_seenTransform{false};
std::atomic<int> g_dbgBudget{0};

uintptr_t g_addressCameraA = 0;  // camera -> player rotation sync (FreeLook hook A)
uintptr_t g_addressTick = 0;     // LocalPlayer tick (FreeLook hook C)
uintptr_t g_addressMob = 0;      // Mob tick (FreeLook hook E)
uintptr_t g_addressScheme = 0;   // CameraInputTransformSystem per-entity function

void onButtonEvent(std::string_view, pl::modmenu::ButtonEvent event, float value);

bool featureOn() noexcept {
    return g_lifecycleEnabled.load(std::memory_order_relaxed) &&
           g_moduleEnabled.load(std::memory_order_relaxed) &&
           g_active.load(std::memory_order_relaxed);
}

bool freeLookModeOn() noexcept { return featureOn() && g_mode.load(std::memory_order_relaxed) == 0; }
bool schemeModeOn() noexcept { return featureOn() && g_mode.load(std::memory_order_relaxed) == 1; }
bool headModeOn() noexcept { return featureOn() && g_mode.load(std::memory_order_relaxed) == 2; }
// FreeLook's camera hooks (A + D) are used by the original FreeLook mode and by the game-scheme BTP mode.
bool cameraDetached() noexcept { return freeLookModeOn() || schemeModeOn(); }

// FreeLook head lock state (ported 1:1 from FreeLook).
std::atomic_bool g_headCapturePending{false};
std::atomic_bool g_headRotationLocked{false};
std::atomic<float> g_lockedHeadYaw{0.0f};

void logInfo(const std::string &msg) {
    btp::BetterThirdPerson::getInstance().getSelf().getLogger().info("{}", msg);
}

void setActive(bool on) noexcept {
    g_active.store(on, std::memory_order_release);
    g_logNextTransform.store(on, std::memory_order_release);
    g_dbgBudget.store(on ? 12 : 0, std::memory_order_relaxed);
    g_headCapturePending.store(on, std::memory_order_release);
    g_headRotationLocked.store(false, std::memory_order_release);
    logInfo(std::string("BTP button: ") + (on ? "ON" : "OFF") + (" (mode " + std::to_string(g_mode.load()) + ")"));
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


// ---- FREELOOK mode (ported from FreeLook): keep the head yaw locked ----------------
void freeLookLock() noexcept {
    if (!freeLookModeOn() || !g_clientInstance) return;
    Actor *player = g_clientInstance->getLocalPlayer();
    if (!player) return;

    auto *headRotation = player->tryGetComponent<ActorHeadRotationComponent>();
    if (!headRotation) return;

    if (g_headCapturePending.load(std::memory_order_acquire)) {
        if (!std::isfinite(headRotation->mYHeadRot)) return;
        g_lockedHeadYaw.store(headRotation->mYHeadRot, std::memory_order_relaxed);
        g_headCapturePending.store(false, std::memory_order_release);
        g_headRotationLocked.store(true, std::memory_order_release);
    }
    if (!g_headRotationLocked.load(std::memory_order_acquire)) return;

    const float lockedYaw = g_lockedHeadYaw.load(std::memory_order_relaxed);
    if (!std::isfinite(lockedYaw)) return;
    headRotation->mYHeadRotOld = lockedYaw;
    headRotation->mYHeadRot = lockedYaw;
}

// ---- HEAD mode: steer head/body yaw toward the real heading ----------------------
constexpr float kRadToDeg = 57.29577951f;

struct FacingState {
    Actor *actor = nullptr;
    uint32_t entity = 0xFFFFFFFFu;
    bool init = false;
    bool bodyChecked = false;
    bool bodyOk = false;
    float prev = 0.0f;
    float cur = 0.0f;
} g_state;

float wrap180(float a) noexcept {
    a = std::fmod(a + 180.0f, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a - 180.0f;
}

bool plausibleYaw(float v) noexcept { return std::isfinite(v) && std::fabs(v) < 100000.0f; }

bool isLocalPlayer(Actor *actor) noexcept {
    return g_clientInstance && actor && actor == g_clientInstance->getLocalPlayer();
}

void stepFacing(Actor *player) noexcept {
    auto *head = player->tryGetComponent<ActorHeadRotationComponent>();
    if (!head || !readable(head, sizeof(*head)) || !plausibleYaw(head->mYHeadRot)) return;

    const uint32_t entity = player->mEntityContext.entity.rawId;
    if (!g_state.init || g_state.actor != player || g_state.entity != entity) {
        g_state = FacingState{};
        g_state.init = true;
        g_state.actor = player;
        g_state.entity = entity;
        g_state.cur = g_state.prev = head->mYHeadRot;
    }

    if (!g_state.bodyChecked) {
        g_state.bodyChecked = true;
        auto *body = player->tryGetComponent<MobBodyRotationComponent>();
        if (body && readable(body, sizeof(*body)) && plausibleYaw(body->mYBodyRot) &&
            plausibleYaw(body->mYBodyRotOld) &&
            std::fabs(wrap180(body->mYBodyRot - head->mYHeadRot)) < 100.0f &&
            std::fabs(wrap180(body->mYBodyRotOld - head->mYHeadRot)) < 100.0f) {
            g_state.bodyOk = true;
        }
        logInfo(std::string("BTP head mode: body component layout ") + (g_state.bodyOk ? "looks OK" : "rejected (head only)"));
    }

    bool moving = false;
    float target = g_state.cur;
    if (g_haveInput.load(std::memory_order_relaxed)) {
        const float ix = g_inX.load(std::memory_order_relaxed);
        const float iy = g_inY.load(std::memory_order_relaxed);
        if (std::isfinite(ix) && std::isfinite(iy) && ix * ix + iy * iy > 0.04f) {
            // Same formula the game uses for its camera-relative scheme:
            // heading = camera yaw - atan2(input.x, input.y).
            target = g_playerYaw.load(std::memory_order_relaxed) - std::atan2(ix, iy) * kRadToDeg;
            moving = true;
        }
    }

    g_state.prev = g_state.cur;
    if (moving) {
        const float maxStep = std::clamp(g_turnSpeed.load(std::memory_order_relaxed), 1.0f, 180.0f);
        g_state.cur += std::clamp(wrap180(target - g_state.cur), -maxStep, maxStep);
    }

    if (moving && g_dbgBudget.load(std::memory_order_relaxed) > 0) {
        g_dbgBudget.fetch_sub(1, std::memory_order_relaxed);
        logInfo("BTP dbg: in=(" + std::to_string(g_inX.load()) + "," + std::to_string(g_inY.load()) +
                ") camYaw=" + std::to_string(g_playerYaw.load()) + " target=" + std::to_string(target) +
                " cur=" + std::to_string(g_state.cur) + " head=" + std::to_string(head->mYHeadRot));
    }
}

void applyFacing(Actor *player) noexcept {
    if (!g_state.init || g_state.actor != player) return;

    auto *head = player->tryGetComponent<ActorHeadRotationComponent>();
    if (head && readable(head, sizeof(*head))) {
        head->mYHeadRotOld = g_state.prev;
        head->mYHeadRot = g_state.cur;
    }
    if (g_state.bodyOk) {
        auto *body = player->tryGetComponent<MobBodyRotationComponent>();
        if (body && readable(body, sizeof(*body))) {
            body->mYBodyRotOld = g_state.prev;
            body->mYBodyRot = g_state.cur;
        }
    }
}

void onLocalPlayerTick(Actor *player) noexcept {
    if (!headModeOn() || !isLocalPlayer(player)) {
        g_state = FacingState{};
        return;
    }
    stepFacing(player);
    applyFacing(player);
}

void onOtherTick(Actor *actor) noexcept {
    if (!headModeOn() || !isLocalPlayer(actor)) return;
    applyFacing(actor);
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

    if (key == kModeKey) {
        const int m = std::clamp(static_cast<int>(parseOpacity(value, 0.01f) * 100.0f + 0.5f), 0, 2);
        g_mode.store(m, std::memory_order_relaxed);
        g_headCapturePending.store(g_active.load(), std::memory_order_release);
        g_headRotationLocked.store(false, std::memory_order_release);
        logInfo("BTP mode set to " + std::to_string(m));
        return;
    }
    if (key == kTurnSpeedKey) {
        g_turnSpeed.store(std::clamp(parseOpacity(value, 0.3f) * 100.0f, 5.0f, 90.0f), std::memory_order_relaxed);
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

    pl::modmenu::ConfigEntry turn{};
    turn.key = kTurnSpeedKey;
    turn.displayName = "Turn speed (degrees per tick)";
    turn.type = pl::modmenu::ConfigType::SliderInt;
    turn.defaultValue = "30";
    turn.minValue = "5";
    turn.maxValue = "90";
    module.configs.push_back(turn);

    pl::modmenu::ConfigEntry mode{};
    mode.key = kModeKey;
    mode.displayName = "Mode: 0 = FreeLook (original), 1 = Better Third Person, 2 = BTP head-only";
    mode.type = pl::modmenu::ConfigType::SliderInt;
    mode.defaultValue = "0";
    mode.minValue = "0";
    mode.maxValue = "2";
    module.configs.push_back(mode);

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
    // Always record the raw movement input and the player's yaw (camera yaw in vanilla)
    // *before* the game transforms them. HEAD mode uses this to know the real heading.
    if (a1 && readable(static_cast<char *>(a1) + 0x24, 8)) {
        float in[2];
        std::memcpy(in, static_cast<char *>(a1) + 0x24, 8);
        g_inX.store(in[0], std::memory_order_relaxed);
        g_inY.store(in[1], std::memory_order_relaxed);
        g_haveInput.store(true, std::memory_order_relaxed);
    }
    if (a3 && readable(static_cast<char *>(a3), 8)) {
        float rot[2];
        std::memcpy(rot, static_cast<char *>(a3), 8);
        g_playerYaw.store(rot[1], std::memory_order_relaxed);
    }
    if (!g_seenTransform.exchange(true, std::memory_order_relaxed)) {
        BetterThirdPerson::getInstance().getSelf().getLogger().info("BTP: input-transform hook is running");
    }

    auto *scheme = static_cast<uint8_t *>(a2);
    if (!schemeModeOn() || !readable(scheme, 1)) {
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
            "BTP scheme mode: original scheme {}, move=({:.2f},{:.2f}) camVec=({:.2f},{:.2f},{:.2f})",
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
    if (!cameraDetached()) origin(cameraComponent, a2, a3);
}

// Hook 3 (FreeLook hook D): VanillaCameraAPI slot 7 reports 1 while ON.
LL_STATIC_HOOK(
    BtpCameraFree,
    memory::HookPriority::Normal,
    pl::memory::resolveVtableFunction("16VanillaCameraAPI", 7, "libminecraftpe.so"),
    "libminecraftpe.so",
    int, void *a1
) {
    if (!cameraDetached()) return origin(a1);
    return 1;
}

// FreeLook hooks C/E: LocalPlayer tick and Mob tick (HEAD mode writes the head/body yaw here).
LL_TYPED_HOOK(BtpLocalTick, memory::HookPriority::Normal, Actor, g_addressTick, "libminecraftpe.so", void) {
    freeLookLock(); // FreeLook: lock before and after the original tick
    origin();
    freeLookLock();
    onLocalPlayerTick(static_cast<Actor *>(this));
}

LL_TYPED_HOOK(BtpMobTick, memory::HookPriority::Normal, Actor, g_addressMob, "libminecraftpe.so", void) {
    freeLookLock();
    origin();
    freeLookLock();
    onOtherTick(static_cast<Actor *>(this));
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
            BtpMobTick::unhook();
            BtpLocalTick::unhook();
            BtpCameraFree::unhook();
            BtpCameraSync::unhook();
            BtpScheme::unhook();
        };

        if (BtpScheme::hook() != 0 || BtpCameraSync::hook() != 0 || BtpCameraFree::hook() != 0 ||
            BtpLocalTick::hook() != 0 || BtpMobTick::hook() != 0) {
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
    g_state = FacingState{};

    // FreeLook hook A signature (same game build).
    g_addressCameraA = resolveSig("FF C3 01 D1 ? ? ? FD ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 FD E3 00 91 54", "libminecraftpe.so");
    // FreeLook hooks C and E (LocalPlayer tick, Mob tick).
    g_addressTick = resolveSig(
        "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39",
        "libminecraftpe.so");
    g_addressMob = resolveSig(
        "FF 83 06 D1 ? ? ? FD ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 43 05 91 57 D0 3B D5 F3",
        "libminecraftpe.so");
    // CameraInputTransformSystem per-entity function (unique in this build).
    g_addressScheme = resolveSig(
        "EB 2B BB 6D E9 23 01 6D FD 7B 02 A9 F6 57 03 A9 F4 4F 04 A9 FD 83 00 91 F4 03 01 AA 81 82 C4 2D 21 08 21 1E 02 08 20 1E",
        "libminecraftpe.so");
    return true;
}

bool BetterThirdPerson::enable() {
    getSelf().getLogger().info("Enabling...");
    if (g_addressCameraA == 0 || g_addressScheme == 0 || g_addressTick == 0 || g_addressMob == 0) {
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

    BtpMobTick::unhook();
    BtpLocalTick::unhook();
    BtpCameraFree::unhook();
    BtpCameraSync::unhook();
    BtpScheme::unhook();
    BtpClientInit::unhook();
    return true;
}

bool BetterThirdPerson::unload() {
    getSelf().getLogger().info("Unloading...");
    g_clientInstance = nullptr;
    g_state = FacingState{};
    return true;
}

} // namespace btp
