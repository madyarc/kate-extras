cmake_minimum_required(VERSION 3.16)
project(kate-extras VERSION 1.0)

if(APPLE)
    # The macOS backends call AppKit and IOKit
    enable_language(OBJCXX)
endif()

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

option(BUILD_SPOTIFY "Build the Spotify player panel" ON)
option(BUILD_DEADLINES "Build the Deadlines calendar panel" ON)
option(BUILD_RESMON "Build the RAM / VRAM resource meter" ON)
option(BUILD_TEXMDPREVIEW "Build the Markdown / LaTeX preview panel" ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6 REQUIRED COMPONENTS TextEditor I18n CoreAddons XmlGui)

# On macOS, point a plugin at the Qt and KDE Frameworks bundled inside Kate.app
function(kate_extras_plugin target)
    if(APPLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DFILE=$<TARGET_FILE:${target}> -P ${PROJECT_SOURCE_DIR}/macos/BundleLinks.cmake
            VERBATIM)
    endif()
endfunction()

if(BUILD_SPOTIFY)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    if(NOT APPLE)
        find_package(Qt6 REQUIRED COMPONENTS DBus)
    endif()
    add_subdirectory(spotify)
endif()
if(BUILD_DEADLINES)
    add_subdirectory(deadlines)
endif()
if(BUILD_RESMON)
    add_subdirectory(resmon)
endif()
if(BUILD_TEXMDPREVIEW)
    add_subdirectory(texmdpreview)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
