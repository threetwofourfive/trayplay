cmake_minimum_required(VERSION 3.16)

# Prefer Clang if no compiler is explicitly specified
if(NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    find_program(CLANG_CXX clang++)
    if(CLANG_CXX)
        set(CMAKE_CXX_COMPILER clang++)
        set(CMAKE_C_COMPILER clang)
    endif()
endif()

project(TrayConsole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
find_package(Threads REQUIRED)

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED)
endif()

find_package(nlohmann_json CONFIG REQUIRED)

# Prefer Lua 5.4 for Sol2 compatibility, fallback to general Lua
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    foreach(_lua_pc IN ITEMS lua54 lua-5.4 lua5.4 lua)
        pkg_check_modules(LUA QUIET ${_lua_pc})
        if(LUA_FOUND)
            break()
        endif()
    endforeach()
endif()
if(NOT LUA_FOUND)
    find_package(Lua REQUIRED)
    set(LUA_LIBRARIES ${LUA_LIBRARIES})
    set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
endif()

# miniz
find_package(miniz CONFIG QUIET)
if(NOT miniz_FOUND AND PKG_CONFIG_FOUND)
    pkg_check_modules(MINIZ QUIET miniz)
endif()

# sol2 (System package)
find_package(sol2 CONFIG QUIET)
if(NOT sol2_FOUND)
    find_path(SOL2_INCLUDE_DIR sol/sol.hpp)
    if(NOT SOL2_INCLUDE_DIR)
        message(FATAL_ERROR "sol2 not found. Install 'sol2' (e.g. 'paru -S sol2' on Arch/CachyOS, 'libsol2-dev' on Debian/Ubuntu, or 'sol2-devel' on Fedora).")
    endif()
endif()

# ------------------------------------------------------------------------------
# Core Daemon
# ------------------------------------------------------------------------------
option(BUILD_CORE "Build the core daemon" ON)

if(BUILD_CORE)
    add_executable(tray-console-daemon)
    target_sources(tray-console-daemon PRIVATE
        core/main.cpp
        core/framebuffer.cpp
        core/font.cpp
        core/audio.cpp
        core/vm.cpp
        core/cartridge.cpp
        core/game_loop.cpp
        core/ipc_server.cpp
    )

    target_include_directories(tray-console-daemon PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${LUA_INCLUDE_DIRS}
    )

    target_link_libraries(tray-console-daemon PRIVATE
        Threads::Threads
        nlohmann_json::nlohmann_json
        ${LUA_LIBRARIES}
    )

    if(TARGET sol2::sol2)
        target_link_libraries(tray-console-daemon PRIVATE sol2::sol2)
    else()
        target_include_directories(tray-console-daemon PRIVATE ${SOL2_INCLUDE_DIR})
    endif()

    if(TARGET SDL2::SDL2)
        target_link_libraries(tray-console-daemon PRIVATE SDL2::SDL2)
    elseif(DEFINED SDL2_LIBRARIES)
        target_link_libraries(tray-console-daemon PRIVATE ${SDL2_LIBRARIES})
        target_include_directories(tray-console-daemon PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()

    if(TARGET miniz::miniz)
        target_link_libraries(tray-console-daemon PRIVATE miniz::miniz)
    elseif(MINIZ_FOUND)
        target_link_libraries(tray-console-daemon PRIVATE ${MINIZ_LIBRARIES})
        target_include_directories(tray-console-daemon PRIVATE ${MINIZ_INCLUDE_DIRS})
    else()
        target_link_libraries(tray-console-daemon PRIVATE miniz)
    endif()

    # Copy games directory to build directory for instant access
    add_custom_target(copy-games)
    add_custom_command(TARGET copy-games POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/games
        ${CMAKE_CURRENT_BINARY_DIR}/games
    )
    add_dependencies(tray-console-daemon copy-games)
endif()

# ------------------------------------------------------------------------------
# Platform-specific Frontends
# ------------------------------------------------------------------------------

if(WIN32)
    message(STATUS "Configuring for Windows...")
    option(BUILD_FRONTEND_WINDOWS "Build Windows frontend" ON)

    if(BUILD_FRONTEND_WINDOWS)
        add_executable(frontend-windows WIN32
            frontends/windows/main.cpp
        )
    endif()

elseif(APPLE)
    message(STATUS "Configuring for macOS...")
    option(BUILD_FRONTEND_MACOS "Build macOS frontend" ON)

    if(BUILD_FRONTEND_MACOS)
        enable_language(Swift)
        set(CMAKE_Swift_LANGUAGE_VERSION 5.0)
        add_executable(frontend-macos MACOSX_BUNDLE
            frontends/macos/Main.swift
        )
        target_link_libraries(frontend-macos PRIVATE
            "-framework Cocoa"
            "-framework AppKit"
            "-framework SwiftUI"
        )
        set_target_properties(frontend-macos PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/frontends/macos/Info.plist"
        )
    endif()

elseif(UNIX AND NOT APPLE)
    message(STATUS "Configuring for Linux/UNIX...")

    option(BUILD_FRONTEND_GNOME "Build GNOME extension frontend" ON)
    option(BUILD_FRONTEND_KDE "Build KDE Plasma frontend" OFF)
    option(BUILD_FRONTEND_CINNAMON "Build Cinnamon frontend" OFF)
    option(BUILD_FRONTEND_XFCE "Build XFCE frontend" OFF)
    option(BUILD_FRONTEND_WAYBAR "Build Waybar frontend" OFF)

    # Helper macro for copying frontend static files
    macro(add_frontend_copy_target target_name source_subpath)
        add_custom_target(${target_name} ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/${source_subpath}
            ${CMAKE_CURRENT_BINARY_DIR}/${source_subpath}
        )
    endmacro()

    if(BUILD_FRONTEND_GNOME)
        add_frontend_copy_target(frontend-gnome frontends/gnome)

        # Install extension directly to local user directory
        add_custom_target(install-gnome-extension)
        add_custom_command(TARGET install-gnome-extension POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/frontends/gnome
            $ENV{HOME}/.local/share/gnome-shell/extensions/trayplay@threetwofourfive.github.io
        )
    endif()

    if(BUILD_FRONTEND_KDE)
        add_frontend_copy_target(frontend-kde frontends/kde)
    endif()

    if(BUILD_FRONTEND_CINNAMON)
        add_frontend_copy_target(frontend-cinnamon frontends/cinnamon)
    endif()

    if(BUILD_FRONTEND_XFCE)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(GTK3 QUIET gtk+-3.0)
        endif()

        add_library(frontend-xfce SHARED frontends/xfce/main.cpp)
        set_target_properties(frontend-xfce PROPERTIES PREFIX "")

        if(GTK3_FOUND)
            target_include_directories(frontend-xfce PRIVATE ${GTK3_INCLUDE_DIRS})
            target_link_libraries(frontend-xfce PRIVATE ${GTK3_LIBRARIES})
        else()
            message(WARNING "GTK3 not found, building frontend-xfce without actual GTK support for skeleton.")
        endif()
    endif()

    if(BUILD_FRONTEND_WAYBAR)
        add_library(frontend-waybar SHARED frontends/waybar/module.cpp)
        set_target_properties(frontend-waybar PROPERTIES PREFIX "")
    endif()

    # --------------------------------------------------------------------------
    # System Installation & Packaging (CPack)
    # --------------------------------------------------------------------------
    include(GNUInstallDirs)

    if(BUILD_CORE AND TARGET tray-console-daemon)
        install(TARGETS tray-console-daemon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    install(PROGRAMS packaging/bin/trayplay DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/trayplay.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/icons/trayplay.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES packaging/metainfo/io.github.threetwofourfive.trayplay.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(DIRECTORY games/ DESTINATION ${CMAKE_INSTALL_DATADIR}/trayplay/games PATTERN ".*" EXCLUDE)

    if(BUILD_FRONTEND_GNOME)
        install(DIRECTORY frontends/gnome/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions/trayplay@threetwofourfive.github.io
            PATTERN ".*" EXCLUDE
        )
    endif()

    # CPack (.deb, .rpm & .tar.gz)
    set(CPACK_PACKAGE_NAME "trayplay")
    set(CPACK_PACKAGE_VENDOR "threetwofourfive")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Virtual Microconsole in the System Tray")
    set(CPACK_PACKAGE_DESCRIPTION "TrayPlay is an ultra-lightweight virtual microconsole living directly in your system tray.")
    set(CPACK_PACKAGE_VERSION "0.1.0")
    set(CPACK_PACKAGE_CONTACT "threetwofourfive@users.noreply.github.com")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/threetwofourfive/trayplay")
    set(CPACK_PACKAGE_LICENSE "MIT")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "threetwofourfive")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm"
    )
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gnome-shell | xfce4-panel | plasma-workspace")

    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)

    include(CPack)

    # --------------------------------------------------------------------------
    # Clickable packaging targets
    # --------------------------------------------------------------------------
    add_custom_target(package-deb)
    add_custom_command(TARGET package-deb POST_BUILD COMMAND cpack -G DEB)

    add_custom_target(package-rpm)
    add_custom_command(TARGET package-rpm POST_BUILD COMMAND cpack -G RPM)

    add_custom_target(package-tar)
    add_custom_command(TARGET package-tar POST_BUILD COMMAND cpack -G TGZ)
endif()
