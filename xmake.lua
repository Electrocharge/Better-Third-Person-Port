add_rules("mode.debug", "mode.release")

set_project("BetterThirdPerson")
set_version("1.0.0")
set_languages("cxx20")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

add_requires("preloader_android 0.2.3")
add_requires("boost_pfr boost-1.88.0")
add_requires("nlohmann_json v3.11.3")
add_requires("magic_enum v0.9.7")
add_requires("fmt 11.2.0", {configs = {header_only = false}})
add_requires("lunasvg v3.5.0")
add_requires("entt v3.16.0")

target("BetterThirdPerson")
    set_kind("shared")
    set_basename("BetterThirdPerson")
    set_symbols("hidden")

    add_files("src/*.cpp")
    add_files("src/**/*.cpp")

    add_includedirs("src")
    add_defines("BetterThirdPerson_VERSION=\"1.0.0\"")

    add_packages(
        "preloader_android",
        "nlohmann_json",
        "boost_pfr",
        "magic_enum",
        "fmt",
        "lunasvg",
        "entt"
    )

    add_cxxflags(
        "-Oz",
        "-flto=thin",
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-semantic-interposition",
        "-fno-ident",
        "-fno-rtti",
        "-fomit-frame-pointer",
        "-fdeclspec",
        "-Wno-unused-parameter",
        {force = true}
    )

    add_ldflags(
        "-Wl,-z,max-page-size=16384",
        "-Wl,--gc-sections",
        "-Wl,--icf=safe",
        "-Wl,--as-needed",
        "-Wl,--lto-O2",
        "-Wl,--strip-all",
        "-Wl,-z,relro",
        "-Wl,-z,now",
        "-Wl,--pack-dyn-relocs=android",
        {force = true}
    )

    add_syslinks("dl", "android", "log")
