cmake_minimum_required(VERSION 3.16)

# Automatically detect and use bundled vcpkg submodule if not explicitly specified
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain")
endif()

# Auto-configure MinGW triplet on Windows when using GCC
if(WIN32 AND MINGW)
    if(NOT DEFINED VCPKG_TARGET_TRIPLET)
        set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "Vcpkg target triplet for MinGW")
    endif()
endif()

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(TrayConsole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Core Dependencies (vcpkg will install these automatically)
# ------------------------------------------------------------------------------
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(SDL2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)

# ------------------------------------------------------------------------------
# Core Daemon
# ------------------------------------------------------------------------------
option(BUILD_CORE "Build the core daemon" ON)

if(BUILD_CORE)
    add_executable(tray-console-daemon 
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
        ${LUA_INCLUDE_DIR}
    )

    target_link_libraries(tray-console-daemon PRIVATE 
        Boost::system 
        Boost::thread 
        SDL2::SDL2 
        nlohmann_json::nlohmann_json
        ${LUA_LIBRARIES}
        sol2::sol2
        miniz::miniz
    )

    # Copy games directory to build directory for instant access
    add_custom_target(copy-games ALL
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
        # Dummy linking for C++/WinRT if needed in the future
    endif()

elseif(APPLE)
    message(STATUS "Configuring for macOS...")
    option(BUILD_FRONTEND_MACOS "Build macOS frontend" ON)
    
    if(BUILD_FRONTEND_MACOS)
        # Enable Swift and configure native macOS bundle
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
    
    # Build options for Linux
    option(BUILD_FRONTEND_GNOME "Build GNOME extension frontend" ON)
    option(BUILD_FRONTEND_KDE "Build KDE Plasma frontend" OFF)
    option(BUILD_FRONTEND_CINNAMON "Build Cinnamon frontend" OFF)
    option(BUILD_FRONTEND_XFCE "Build XFCE frontend" OFF)
    option(BUILD_FRONTEND_WAYBAR "Build Waybar frontend" OFF)

    # GNOME Frontend (Custom Targets)
    if(BUILD_FRONTEND_GNOME)
        add_custom_target(frontend-gnome ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/frontends/gnome
            ${CMAKE_CURRENT_BINARY_DIR}/frontends/gnome
            COMMENT "Copying GNOME frontend files"
        )
        
        # Install extension directly to local user directory
        add_custom_target(install-gnome-extension
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/frontends/gnome
            $ENV{HOME}/.local/share/gnome-shell/extensions/trayplay@threetwofourfive.github.io
            COMMENT "Installing TrayPlay GNOME extension into ~/.local/share/gnome-shell/extensions"
        )
    endif()

    # KDE Frontend (Custom Target)
    if(BUILD_FRONTEND_KDE)
        add_custom_target(frontend-kde ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/frontends/kde
            ${CMAKE_CURRENT_BINARY_DIR}/frontends/kde
            COMMENT "Copying KDE frontend files"
        )
    endif()

    # Cinnamon Frontend (Custom Target)
    if(BUILD_FRONTEND_CINNAMON)
        add_custom_target(frontend-cinnamon ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/frontends/cinnamon
            ${CMAKE_CURRENT_BINARY_DIR}/frontends/cinnamon
            COMMENT "Copying Cinnamon frontend files"
        )
    endif()

    # XFCE Frontend (C++ Shared Library)
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

    # Waybar Frontend (C++ Shared Library)
    if(BUILD_FRONTEND_WAYBAR)
        add_library(frontend-waybar SHARED frontends/waybar/module.cpp)
        set_target_properties(frontend-waybar PROPERTIES PREFIX "")
    endif()

    # --------------------------------------------------------------------------
    # System Installation & Packaging (CPack)
    # --------------------------------------------------------------------------
    include(GNUInstallDirs)

    # 1. Core Daemon binary
    if(BUILD_CORE AND TARGET tray-console-daemon)
        install(TARGETS tray-console-daemon
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    # 2. Desktop Launcher script (/usr/bin/trayplay)
    install(PROGRAMS packaging/bin/trayplay
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # 3. Desktop Entry (/usr/share/applications/trayplay.desktop)
    install(FILES packaging/trayplay.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )

    # 4. Scalable Vector Application Icon (/usr/share/icons/hicolor/scalable/apps/trayplay.svg)
    install(FILES packaging/icons/trayplay.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )

    # 5. AppStream Metadata (/usr/share/metainfo/io.github.threetwofourfive.trayplay.metainfo.xml)
    install(FILES packaging/metainfo/io.github.threetwofourfive.trayplay.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )

    # 6. Games Library (cartridges in /usr/share/trayplay/games)
    install(DIRECTORY games/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/trayplay/games
        PATTERN ".*" EXCLUDE
    )

    # 7. GNOME Shell Extension
    if(BUILD_FRONTEND_GNOME)
        install(DIRECTORY frontends/gnome/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions/trayplay@threetwofourfive.github.io
            PATTERN ".*" EXCLUDE
        )
    endif()

    # --------------------------------------------------------------------------
    # CPack Configuration (.deb & .rpm)
    # --------------------------------------------------------------------------
    set(CPACK_PACKAGE_NAME "trayplay")
    set(CPACK_PACKAGE_VENDOR "threetwofourfive")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Virtual Microconsole in the System Tray")
    set(CPACK_PACKAGE_DESCRIPTION "TrayPlay is an ultra-lightweight virtual microconsole living directly in your system tray. Features sandboxed Lua runtime, chiptune synthesizer, 60 FPS fixed-timestep loop, and native desktop integration.")
    set(CPACK_PACKAGE_VERSION "0.1.0")
    set(CPACK_PACKAGE_CONTACT "threetwofourfive@users.noreply.github.com")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/threetwofourfive/trayplay")
    set(CPACK_PACKAGE_LICENSE "MIT")

    # Debian Package (.deb)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "threetwofourfive")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm"
    )
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gnome-shell | xfce4-panel | plasma-workspace")

    # RPM Package (.rpm)
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)

    include(CPack)
endif()
