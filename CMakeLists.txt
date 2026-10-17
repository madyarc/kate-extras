# SPDX-FileCopyrightText: 2026 Madhesh
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(texmdpreview VERSION 0.1 LANGUAGES CXX)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets WebEngineWidgets)
find_package(KF6 6.0 REQUIRED COMPONENTS TextEditor Parts CoreAddons Config)
find_package(Okular6 REQUIRED) # only for okular/interfaces/viewerinterface.h (SyncTeX)

# ---- the Kate plugin ----
kcoreaddons_add_plugin(texmdpreview
    SOURCES texmdplugin.cpp texmdplugin.h
    INSTALL_NAMESPACE "kf6/ktexteditor")

target_link_libraries(texmdpreview PRIVATE
    Qt6::Widgets Qt6::WebEngineWidgets
    KF6::TextEditor KF6::Parts KF6::CoreAddons KF6::ConfigCore
    Okular::Core)

# ---- web assets: KaTeX + markdown-it, fetched once at configure time ----
set(KATEX_VERSION "0.16.22" CACHE STRING "KaTeX version to bundle")
set(MDIT_VERSION  "14.1.0"  CACHE STRING "markdown-it version to bundle")

# Pass -DKATEX_TARBALL=/path/katex.tgz and -DMDIT_TARBALL=/path/markdown-it.tgz
# to build offline (used by the Arch PKGBUILD); otherwise they are downloaded.
set(KATEX_TARBALL "" CACHE FILEPATH "Local KaTeX npm tarball (optional)")
set(MDIT_TARBALL  "" CACHE FILEPATH "Local markdown-it npm tarball (optional)")

function(fetch_npm name version local_tgz outdir)
    if(NOT EXISTS "${outdir}/package/package.json")
        if(local_tgz)
            set(tgz "${local_tgz}")
        else()
            set(tgz "${CMAKE_BINARY_DIR}/${name}-${version}.tgz")
            message(STATUS "Downloading ${name} ${version}")
            file(DOWNLOAD "https://registry.npmjs.org/${name}/-/${name}-${version}.tgz" "${tgz}"
                 STATUS st TLS_VERIFY ON)
            list(GET st 0 code)
            if(NOT code EQUAL 0)
                message(FATAL_ERROR "Download of ${name} ${version} failed: ${st}")
            endif()
        endif()
        file(ARCHIVE_EXTRACT INPUT "${tgz}" DESTINATION "${outdir}")
    endif()
endfunction()

fetch_npm(katex       ${KATEX_VERSION} "${KATEX_TARBALL}" "${CMAKE_BINARY_DIR}/katex_pkg")
fetch_npm(markdown-it ${MDIT_VERSION}  "${MDIT_TARBALL}"  "${CMAKE_BINARY_DIR}/mdit_pkg")

set(WEBDIR ${KDE_INSTALL_DATADIR}/texmdpreview)
install(FILES web/index.html web/render.js web/style.css DESTINATION ${WEBDIR})
install(FILES "${CMAKE_BINARY_DIR}/mdit_pkg/package/dist/markdown-it.min.js" DESTINATION ${WEBDIR})
install(FILES "${CMAKE_BINARY_DIR}/katex_pkg/package/dist/katex.min.js"
              "${CMAKE_BINARY_DIR}/katex_pkg/package/dist/katex.min.css"
        DESTINATION ${WEBDIR}/katex)
install(DIRECTORY "${CMAKE_BINARY_DIR}/katex_pkg/package/dist/fonts" DESTINATION ${WEBDIR}/katex)
