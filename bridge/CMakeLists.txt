# GammaRay MCP Bridge
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Links GammaRay client libs (GPL-2.0-or-later) => this bridge must be
# GPL-compatible. qtmcp is triple-licensed (LGPL-3.0-only / GPL-2.0-only /
# GPL-3.0-only); we comply under GPL-2.0-only.

cmake_minimum_required(VERSION 3.16)
project(GammaRayMcpBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)              # qtmcp requires C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# --- qtmcp: try system package first, fall back to FetchContent ---
# qtmcp registers itself as a Qt6 module; find_package(Qt6 COMPONENTS McpServer)
# looks for Qt6McpServerConfig.cmake installed by the system qtmcp-dev package.
# When falling back to FetchContent, it must run before any other Qt6/GammaRay
# find_package calls because qtmcp's subdirectory promotes 3rd-party targets
# (Threads::Threads) to global scope, and prior top-level find_package calls
# that already created those targets would cause a "not built in this directory"
# error.
find_package(Qt6 QUIET COMPONENTS McpServer McpCommon)

if(NOT Qt6McpServer_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    qtmcp
    GIT_REPOSITORY https://github.com/signal-slot/qtmcp.git
    GIT_TAG 2706345c44e282aba15cf020608af2283e9fb5f9
    GIT_SHALLOW TRUE
    GIT_REMOTE_UPDATE_STRATEGY CHECKOUT
  )
  set(QT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(QT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(qtmcp)
endif()

# --- GammaRay (system package: gammaray-dev >= 3.4.0) ---
find_package(GammaRay REQUIRED)

# --- Qt6 components for the bridge target (Qt6 already found globally by qtmcp) ---
find_package(Qt6 COMPONENTS Core Gui Widgets Network REQUIRED)

add_executable(gammaray-mcp-bridge
    src/main.cpp
    src/gammaray_session.cpp
    src/gammaray_session.h
    src/scenegraph_tools.cpp
    src/scenegraph_tools.h
    src/material_interface.cpp
    src/material_interface.h
    src/quickinspector_proxy.cpp
    src/quickinspector_proxy.h
    src/quickinspector_types.h
    src/property_reader.cpp
    src/property_reader.h
    src/widget_tools.cpp
    src/widget_tools.h
    src/widget_inspector_proxy.cpp
    src/widget_inspector_proxy.h
)

target_link_libraries(gammaray-mcp-bridge PRIVATE
    gammaray_client                    # VERIFIED: no GammaRay:: namespace
    gammaray_common
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets                       # REQUIRED: QApplication for RemoteModel::style()
    Qt6::Network
    Qt6::McpServer                     # qtmcp, provides Qt::McpServer alias
    Qt6::McpCommon
)

install(TARGETS gammaray-mcp-bridge RUNTIME DESTINATION bin)
install(PROGRAMS run.sh DESTINATION bin)

# Runtime: qtmcp shared libs and stdio plugin are built in the build tree.
# For development, run.sh sets LD_LIBRARY_PATH and QT_PLUGIN_PATH.
# For distribution, CPack bundles qtmcp's install rules automatically.
set_target_properties(gammaray-mcp-bridge PROPERTIES
  BUILD_RPATH "$<TARGET_FILE_DIR:Qt6::McpServer>"
  INSTALL_RPATH "$ORIGIN/../lib/x86_64-linux-gnu"
)

# --- CPack packaging ---
set(CPACK_PACKAGE_NAME "gammaray-mcp-bridge")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GammaRay MCP bridge")
set(CPACK_PACKAGE_DESCRIPTION "MCP server that exposes GammaRay probe introspection data (QML SceneGraph, QML items, and Qt Widgets) to LLMs for assisted debugging.")
set(CPACK_PACKAGE_CONTACT "Gary Wang <wangzichong@deepin.org>")
set(CPACK_PACKAGE_VENDOR "gammaray-mcp-bridge")
set(CPACK_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Gary Wang <wangzichong@deepin.org>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/linuxdeepin/gammaray-mcp-bridge")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gammaray-plugin-quickinspector")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_GENERATOR "DEB")
include(CPack)
