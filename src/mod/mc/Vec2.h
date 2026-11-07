#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "_HeaderOutputPredefine.h"

constexpr float pi_f = 3.1415926535f;

struct Vec2 final {
    float x;
    float y;

    constexpr Vec2()
        : x(0.f)
        , y(0.f) {}

    constexpr Vec2(float x, float y)
        : x(x)
        , y(y) {}

    constexpr Vec2 operator-(const Vec2& rhs) const {
        return { x - rhs.x, y - rhs.y };
    }

    constexpr Vec2 operator+(const Vec2& rhs) const {
        return { x + rhs.x, y + rhs.y };
    }

    constexpr Vec2 operator/(const Vec2& rhs) const {
        return { x / rhs.x, y / rhs.y };
    }

    constexpr Vec2 operator*(const Vec2& rhs) const {
        return { x * rhs.x, y * rhs.y };
    }

    constexpr bool operator==(const Vec2& rhs) const {
        return x == rhs.x && y == rhs.y;
    }

    constexpr bool operator!=(const Vec2& rhs) const {
        return !(*this == rhs);
    }

    float magnitude() const {
        return std::sqrt(x * x + y * y);
    }
};

struct Vec4 final {
    float x;
    float y;
    float z;
    float w;

    constexpr Vec4()
        : x(0.f)
        , y(0.f)
        , z(0.f)
        , w(0.f) {}

    constexpr Vec4(float x, float y, float z, float w)
        : x(x)
        , y(y)
        , z(z)
        , w(w) {}
};

Vec4 RotToQuaternion(Vec2 const& rot) {
    constexpr float invRadDegrees = pi_f / 180.f;

    float pitch = rot.x * invRadDegrees;
    float yaw = rot.y * invRadDegrees;
    float roll = 0.f;

    float cx = std::cos(pitch * 0.5f);
    float sx = std::sin(pitch * 0.5f);
    float cy = std::cos(yaw * 0.5f);
    float sy = std::sin(yaw * 0.5f);
    float cz = std::cos(roll * 0.5f);
    float sz = std::sin(roll * 0.5f);

    return Vec4 {
        (cx * cy * sz) - (sx * sy * cz), // quat.x
        (cx * cy * cz) + (sx * sy * sz), // quat.y
        (sx * cy * cz) - (cx * sy * sz), // quat.z
        (cx * sy * cz) + (sx * cy * sz)  // quat.w
    };
}

Vec2 QuaternionToRot(const Vec4& quat) {
    constexpr float radDegrees = 180.f / pi_f;

    float pitch = 0.f;
    float yaw = 0.f;
    [[maybe_unused]] float roll = 0.f;

    // roll (x-axis rotation)
    float sinr_cosp = 2.f * (quat.x * quat.y + quat.z * quat.w);
    float cosr_cosp = 1.f - (2.f * (quat.y * quat.y + quat.z * quat.z));
    roll = std::atan2(sinr_cosp, cosr_cosp);

    // pitch (y-axis rotation)
    float sinp = 2.f * (quat.y * quat.w - quat.x * quat.z);
    if (std::abs(sinp) >= 1.f) {
        pitch = std::copysign(pi_f / 2.f, sinp); // use 90 degrees if out of range
    } else {
        pitch = std::asin(sinp);
    }

    // yaw (z-axis rotation)
    float siny_cosp = 2.f * (quat.x * quat.w + quat.y * quat.z);
    float cosy_cosp = 1.f - (2.f * (quat.z * quat.z + quat.w * quat.w));
    yaw = std::atan2(siny_cosp, cosy_cosp);

    return Vec2 { pitch * radDegrees, yaw * radDegrees };
}

class CameraComponent {
public:
    CLASS_FIELD(Vec4, mLookAngles, 0x30);
};