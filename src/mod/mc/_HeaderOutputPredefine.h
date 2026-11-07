#pragma once

#pragma warning(disable : 4099)
#pragma warning(disable : 4201)
#pragma warning(disable : 4373)

#if defined(__cplusplus)
    #define NXC_CXX 1
#else
    #define NXC_CXX 0
#endif

#if !defined(__ANDROID__)
    #error "This header is for Android only."
#endif

#define NXC_STRINGIFY_IMPL(x) #x
#define NXC_STRINGIFY(x) NXC_STRINGIFY_IMPL(x)
#define NXC_CONXCAT_IMPL(a, b) a##b
#define NXC_CONXCAT(a, b) NXC_CONXCAT_IMPL(a, b)

#if defined(__clang__) || defined(__GNUC__)
    #define NXC_EXPORT __attribute__((visibility("default")))
    #define NXC_IMPORT __declspec(dllimport)
    #define NXC_HIDDEN  __attribute__((visibility("hidden")))
    #define NXC_UNUSED  __attribute__((unused))
    #define NXC_USED    __attribute__((used))
    #define NXC_NORETURN __attribute__((noreturn))
    #define NXC_ALWAYS_INLINE inline __attribute__((always_inline))
    #define NXC_NEVER_INLINE __attribute__((noinline))
    #define NXC_PACKED __attribute__((packed))
    #define NXC_ALIGNED(n) __attribute__((aligned(n)))
    #define NXC_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define NXC_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define NXC_UNREACHABLE() __builtin_unreachable()
    #define NXC_RESTRICT __restrict__
    #define NXC_ONLOAD __attribute__((constructor))
    #define NXC_ONUNLOAD __attribute__((destructor))
#else
    #define NXC_EXPORT
    #define NXC_HIDDEN
    #define NXC_UNUSED
    #define NXC_USED
    #define NXC_NORETURN
    #define NXC_ALWAYS_INLINE inline
    #define NXC_NEVER_INLINE
    #define NXC_PACKED
    #define NXC_ALIGNED(n)
    #define NXC_LIKELY(x) (x)
    #define NXC_UNLIKELY(x) (x)
    #define NXC_UNREACHABLE() ((void)0)
    #define NXC_RESTRICT
    #define NXC_ONLOAD
    #define NXC_ONUNLOAD
#endif

#if defined(__IMPORT__)
    #define NXC NXC_IMPORT
#else
    #define NXC NXC_EXPORT
#endif

#if NXC_CXX
    #define NXC_EXTERN_C_BEGIN extern "C" {
    #define NXC_EXTERN_C_END }
#else
    #define NXC_EXTERN_C_BEGIN
    #define NXC_EXTERN_C_END
#endif

#if NXC_CXX
    #if defined(__has_cpp_attribute)
        #if __has_cpp_attribute(nodiscard)
            #define NXC_NODISCARD [[nodiscard]]
        #else
            #define NXC_NODISCARD __attribute__((warn_unused_result))
        #endif

        #if __has_cpp_attribute(maybe_unused)
            #define NXC_MAYBE_UNUSED [[maybe_unused]]
        #else
            #define NXC_MAYBE_UNUSED __attribute__((unused))
        #endif

        #if __has_cpp_attribute(fallthrough)
            #define NXC_FALLTHROUGH [[fallthrough]]
        #else
            #define NXC_FALLTHROUGH __attribute__((fallthrough))
        #endif
    #else
        #define NXC_NODISCARD __attribute__((warn_unused_result))
        #define NXC_MAYBE_UNUSED __attribute__((unused))
        #define NXC_FALLTHROUGH __attribute__((fallthrough))
    #endif
#else
    #define NXC_NODISCARD __attribute__((warn_unused_result))
    #define NXC_MAYBE_UNUSED __attribute__((unused))
    #define NXC_FALLTHROUGH __attribute__((fallthrough))
#endif

#define API NXC_MAYBE_UNUSED NXC
#define NAPI NXC_HIDDEN
#define HIDDEN NXC_HIDDEN

#if NXC_CXX
    #define CAPI extern "C" API
#else
    #define CAPI extern API
#endif

#define API_IMPORT  NXC_IMPORT
#define API_EXPORT  NXC_EXPORT
#define API_HIDDEN  NXC_HIDDEN
#define API_UNUSED   NXC_UNUSED
#define API_USED     NXC_USED
#define API_NODISCARD NXC_NODISCARD
#define API_MAYBE_UNUSED NXC_MAYBE_UNUSED
#define API_NORETURN NXC_NORETURN
#define API_ALWAYS_INLINE NXC_ALWAYS_INLINE
#define API_NEVER_INLINE NXC_NEVER_INLINE
#define API_PACKED NXC_PACKED
#define API_ALIGNED(n) NXC_ALIGNED(n)
#define API_LIKELY(x) NXC_LIKELY(x)
#define API_UNLIKELY(x) NXC_UNLIKELY(x)
#define API_UNREACHABLE() NXC_UNREACHABLE()
#define API_RESTRICT NXC_RESTRICT
#define API_FALLTHROUGH NXC_FALLTHROUGH

#define NXCIS API_HIDDEN inline static
#define NXCS API_HIDDEN static

#define MCAPI  API
#define MCTAPI template<> MCAPI
#define MCFOLD MCAPI /*Identical COMDAT Folding*/
#define MCNAPI [[deprecated("This API is not available. Open an ticket if you need it. "\
                            "https://nexcaise.my.id/feedback")]] MCAPI

#if defined(NXC_CXX) && NXC_CXX
#include <expected>
#endif

#include <android/log.h>
#include <android/dlext.h>
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <dlfcn.h>
#include <deque>
#include <dirent.h>
#include <exception>
#include <elf.h>
#include <filesystem>
#include <fcntl.h>
#include <forward_list>
#include <fstream>
#include <future>
#include <functional>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <jni.h>
#include <limits>
#include <list>
#include <link.h>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <ostream>
#include <pthread.h>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <sys/mman.h>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <unistd.h>
#include <variant>
#include <vector>

#include <entt/entt.hpp>
#include "Gloss.h"
#include "Hook.h"

using ushort = unsigned short;
using uint = unsigned int;
using ulong = unsigned long;
using llong = long long;
using ullong = unsigned long long;
using uchar = unsigned char;
using schar = signed char;
using byte = uchar;
using ldouble = long double;
using int64 = long long;
using int32 = int;
using int16 = short;
using int8 = char;
using uint64 = unsigned long long;
using uint32 = unsigned int;
using uint16 = unsigned short;
using uint8 = unsigned char;

namespace fs = std::filesystem;
using scsclock = std::chrono::steady_clock;

template <typename TreatAs, typename Pointer>
extern TreatAs& directAccess(Pointer ptr, size_t offset) {
    return *reinterpret_cast<TreatAs*>(reinterpret_cast<uintptr_t>(ptr) + offset);
}

#define CLASS_FIELD(type, name, offset)                                                                          \
    __declspec(property(get = __get_field_##name, put = __set_field_##name)) type name;                          \
    type& __get_field_##name() const { return directAccess<type>(this, offset); }                                 \
    template <typename T>                                                                                        \
    void __set_field_##name(const T& value) { directAccess<type>(this, offset) = value; }

template <std::size_t IIdx, typename TRet, typename... TArgs>
static TRet callVirtualFunc(void* thisptr, TArgs... args) {
    using Fn = TRet(*)(void*, TArgs...);
    return (*reinterpret_cast<Fn**>(thisptr))[IIdx](thisptr, args...);
}