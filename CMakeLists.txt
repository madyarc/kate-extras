cmake_minimum_required(VERSION 3.16)
project(kate-extras VERSION 1.0)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

option(BUILD_SPOTIFY "Build the Spotify player panel" ON)
option(BUILD_DEADLINES "Build the Deadlines calendar panel" ON)
option(BUILD_RESMON "Build the RAM / VRAM resource meter" ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6 REQUIRED COMPONENTS TextEditor I18n CoreAddons XmlGui)

if(BUILD_SPOTIFY)
    find_package(Qt6 REQUIRED COMPONENTS DBus Network)
    add_subdirectory(spotify)
endif()
if(BUILD_DEADLINES)
    add_subdirectory(deadlines)
endif()
if(BUILD_RESMON)
    add_subdirectory(resmon)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
