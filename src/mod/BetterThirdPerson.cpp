/*
 * Better Third Person (Bedrock / LeviLaunchroid native port)
 *
 * Inspired by the Java mod "Better Third Person" (feature description only,
 * no code taken from it). Derivative work based on the FreeLook native mod by
 * NexCaise (CPNC-1.0, see files/LICENSE). NOT an official version of FreeLook
 * and not affiliated with the Java mod or its authors.
 *
 * What it does: in Bedrock the camera already orbits with your look input and
 * your movement is relative to the camera. This mod keeps that and changes only
 * which way the *model* faces: the head and body turn smoothly toward the
 * direction you are actually moving instead of always facing the camera
 * direction. Aiming, attacking and block placing still use the real look
 * direction, because the actor's look rotation is never modified.
 */
#include "mod/BetterThirdPerson.h"
#include "mc/ClientInstance.h"
#include "mc/StateVectorComponent.h"
#include "mc/MobBodyRotationComponent.h"
#include "pl_modmenu_compat.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

static ClientInstance *g_clientInstance = nullptr;

namespace {
// ---- ids ------------------------------------------------------------------
const char *kModuleId = "betterthirdperson.main";
const char *kModId = "BetterThirdPerson";
const char *kTurnSpeedKey = "turn_speed";
const char *kRotateBodyKey = "rotate_body";
const char *kToggleKeybindKey = "btp_toggle";

// ---- settings (written from the UI thread, read on the game thread) --------
std::atomic_bool g_lifecycleEnabled{false};
std::atomic_bool g_moduleEnabled{false};
std::atomic_bool g_active{true};      // soft toggle (keybind)
std::atomic_bool g_rotateBody{true};
std::atomic<float> g_turnSpeed{30.0f}; // degrees per game tick

uintptr_t g_addressTick = 0; // LocalPlayer tick (same function FreeLook hooks)
uintptr_t g_addressMob = 0;  // Mob tick (same function FreeLook hooks)

// ---- tuning ----------------------------------------------------------------
constexpr float kMinMoveSpeed = 0.02f; // blocks/tick before we treat it as moving
constexpr float kRadToDeg = 57.29577951f;

// ---- game-thread state -----------------------------------------------------
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

bool isLocalPlayer(Actor *actor) noexcept {
    return g_clientInstance && actor && actor == g_clientInstance->getLocalPlayer();
}

bool featureOn() noexcept {
    return g_lifecycleEnabled.load(std::memory_order_relaxed) &&
           g_moduleEnabled.load(std::memory_order_relaxed) &&
           g_active.load(std::memory_order_relaxed);
}

// Called once per game tick for the local player, after the original tick ran.
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

    // One-time sanity check of the assumed MobBodyRotationComponent layout.
    if (!g_state.bodyChecked) {
        g_state.bodyChecked = true;
        auto *body = player->tryGetComponent<MobBodyRotationComponent>();
        if (body && readable(body, sizeof(*body)) && plausibleYaw(body->mYBodyRot) &&
            plausibleYaw(body->mYBodyRotOld) &&
            std::fabs(wrap180(body->mYBodyRot - head->mYHeadRot)) < 100.0f &&
            std::fabs(wrap180(body->mYBodyRotOld - head->mYHeadRot)) < 100.0f) {
            g_state.bodyOk = true;
        }
    }

    bool moving = false;
    float target = g_state.cur;
    auto *sv = player->tryGetComponent<StateVectorComponent>();
    if (sv && readable(sv, sizeof(*sv))) {
        const float dx = sv->mPosDelta[0];
        const float dz = sv->mPosDelta[2];
        if (std::isfinite(dx) && std::isfinite(dz) && std::fabs(dx) < 10.0f && std::fabs(dz) < 10.0f &&
            dx * dx + dz * dz > kMinMoveSpeed * kMinMoveSpeed) {
            // Minecraft yaw: 0 = +Z, 90 = -X.
            target = std::atan2(-dx, dz) * kRadToDeg;
            moving = true;
        }
    }

    g_state.prev = g_state.cur;
    if (moving) {
        const float maxStep = std::clamp(g_turnSpeed.load(std::memory_order_relaxed), 1.0f, 180.0f);
        const float diff = std::clamp(wrap180(target - g_state.cur), -maxStep, maxStep);
        g_state.cur += diff;
    }
}

// Writes the smoothed facing into the head (and body) rotation. Safe to call
// any number of times per tick: it only re-applies the stored pair.
void applyFacing(Actor *player) noexcept {
    if (!g_state.init || g_state.actor != player) return;

    auto *head = player->tryGetComponent<ActorHeadRotationComponent>();
    if (head && readable(head, sizeof(*head))) {
        head->mYHeadRotOld = g_state.prev;
        head->mYHeadRot = g_state.cur;
    }

    if (g_state.bodyOk && g_rotateBody.load(std::memory_order_relaxed)) {
        auto *body = player->tryGetComponent<MobBodyRotationComponent>();
        if (body && readable(body, sizeof(*body))) {
            body->mYBodyRotOld = g_state.prev;
            body->mYBodyRot = g_state.cur;
        }
    }
}

void onLocalPlayerTick(Actor *player) noexcept {
    if (!featureOn() || !isLocalPlayer(player)) {
        // Feature off: forget state so we re-sync from the game's own head yaw when re-enabled.
        g_state = FacingState{};
        return;
    }
    stepFacing(player);
    applyFacing(player);
}

void onOtherTick(Actor *actor) noexcept {
    if (!featureOn() || !isLocalPlayer(actor)) return;
    applyFacing(actor);
}

// ---- mod menu ---------------------------------------------------------------
float parseFloat(std::string_view value, float fallback) noexcept {
    std::string text(value);
    char *end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(parsed)) return fallback;
    return parsed;
}

void onModuleToggle(std::string_view, bool enabled) { g_moduleEnabled.store(enabled, std::memory_order_relaxed); }

void onConfigChanged(std::string_view, std::string_view key, std::string_view value) {
    if (key == kTurnSpeedKey) {
        g_turnSpeed.store(std::clamp(parseFloat(value, 30.0f), 1.0f, 180.0f), std::memory_order_relaxed);
    } else if (key == kRotateBodyKey) {
        g_rotateBody.store(value == "true" || value == "1" || value == "on" || value == "enabled",
                           std::memory_order_relaxed);
    }
}

void onKeybind(std::string_view, std::string_view, bool isDown) {
    if (!isDown) return;
    g_active.store(!g_active.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

bool registerUi() {
    pl::modmenu::ModuleInfo module{};
    module.moduleId = kModuleId;
    module.displayName = "Better Third Person";
    module.description =
        "Third person where the camera orbits freely and your character turns to face the "
        "direction you are moving. Aiming and interaction still use the camera direction.";
    module.modId = kModId;
    module.defaultEnabled = true;
    module.hideInHudEditor = true;

    pl::modmenu::ConfigEntry turn{};
    turn.key = kTurnSpeedKey;
    turn.displayName = "Turn speed (degrees per tick)";
    turn.type = pl::modmenu::ConfigType::SliderInt;
    turn.defaultValue = "30";
    turn.minValue = "5";
    turn.maxValue = "90";
    module.configs.push_back(turn);

    pl::modmenu::ConfigEntry body{};
    body.key = kRotateBodyKey;
    body.displayName = "Turn body too (not just head)";
    body.type = pl::modmenu::ConfigType::Toggle;
    body.defaultValue = "true";
    module.configs.push_back(body);

    pl::modmenu::ConfigEntry key{};
    key.key = kToggleKeybindKey;
    key.displayName = "Quick toggle keybind";
    key.type = pl::modmenu::ConfigType::Keybind;
    key.defaultValue = "F8";
    module.configs.push_back(key);

    module.onToggle = onModuleToggle;
    module.onConfigChanged = onConfigChanged;
    module.onKeybind = onKeybind;

    return pl::modmenu::registerModule(module);
}

void unregisterUi() { pl::modmenu::unregisterModule(kModuleId); }

uintptr_t resolveSig(const char *sig, const char *module) {
    return ::pl::memory::resolveSignature(sig ? std::string_view(sig) : std::string_view{},
                                          module ? std::string_view(module) : std::string_view{});
}

} // namespace

namespace btp {

// LocalPlayer tick: runs once per game tick for the local player.
LL_TYPED_HOOK(BtpLocalTick, memory::HookPriority::Normal, Actor, g_addressTick, "libminecraftpe.so", void) {
    origin();
    onLocalPlayerTick(static_cast<Actor *>(this));
}

// Mob tick: shared by every mob, so only act when `this` is the local player.
LL_TYPED_HOOK(BtpMobTick, memory::HookPriority::Normal, Actor, g_addressMob, "libminecraftpe.so", void) {
    origin();
    onOtherTick(static_cast<Actor *>(this));
}

// ClientInstance init-finished: grab the client instance and install the other hooks.
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

        if (BtpLocalTick::hook() != 0 || BtpMobTick::hook() != 0) {
            BtpMobTick::unhook();
            BtpLocalTick::unhook();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to install Better Third Person hooks");
        } else if (!registerUi()) {
            BtpMobTick::unhook();
            BtpLocalTick::unhook();
            g_lifecycleEnabled.store(false, std::memory_order_relaxed);
            self.getLogger().error("Failed to register Better Third Person mod-menu controls");
        } else {
            self.getLogger().info("Better Third Person enabled");
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
    g_active.store(true, std::memory_order_relaxed);
    g_state = FacingState{};

    // Same two functions FreeLook hooks (LocalPlayer tick, Mob tick) for this game build.
    g_addressTick = resolveSig(
        "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39",
        "libminecraftpe.so");
    g_addressMob = resolveSig(
        "FF 83 06 D1 ? ? ? FD ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 43 05 91 57 D0 3B D5 F3",
        "libminecraftpe.so");
    return true;
}

bool BetterThirdPerson::enable() {
    getSelf().getLogger().info("Enabling...");
    if (g_addressTick == 0 || g_addressMob == 0) {
        getSelf().getLogger().error("Tick signatures not found (game version changed?)");
        return false;
    }
    return BtpClientInit::hook() == 0;
}

bool BetterThirdPerson::disable() {
    getSelf().getLogger().info("Disabling...");

    g_lifecycleEnabled.store(false, std::memory_order_relaxed);
    g_moduleEnabled.store(false, std::memory_order_relaxed);
    unregisterUi();

    BtpMobTick::unhook();
    BtpLocalTick::unhook();
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
