cmake_minimum_required(VERSION 3.16)
project(TrayConsole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Core Dependencies (Optional for skeleton build)
# ------------------------------------------------------------------------------
find_package(Boost QUIET COMPONENTS system thread)
find_package(SDL2 QUIET)

# ------------------------------------------------------------------------------
# Core Daemon
# ------------------------------------------------------------------------------
option(BUILD_CORE "Build the core daemon" ON)

if(BUILD_CORE)
    add_executable(tray-console-daemon
        core/main.cpp
    )
    
    if(Boost_FOUND)
        target_link_libraries(tray-console-daemon PRIVATE Boost::system Boost::thread)
    endif()
    
    if(SDL2_FOUND)
        target_link_libraries(tray-console-daemon PRIVATE SDL2::SDL2)
    endif()
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
        # Enable Objective-C++
        enable_language(OBJCXX)
        add_executable(frontend-macos MACOSX_BUNDLE
            frontends/macos/AppDelegate.mm
        )
        target_link_libraries(frontend-macos PRIVATE "-framework Cocoa" "-framework AppKit")
    endif()

elseif(UNIX AND NOT APPLE)
    message(STATUS "Configuring for Linux/UNIX...")
    
    # Build options for Linux
    option(BUILD_FRONTEND_GNOME "Build GNOME extension frontend" OFF)
    option(BUILD_FRONTEND_KDE "Build KDE Plasma frontend" OFF)
    option(BUILD_FRONTEND_CINNAMON "Build Cinnamon frontend" OFF)
    option(BUILD_FRONTEND_XFCE "Build XFCE frontend" OFF)
    option(BUILD_FRONTEND_WAYBAR "Build Waybar frontend" OFF)

    # GNOME Frontend (Custom Target)
    if(BUILD_FRONTEND_GNOME)
        add_custom_target(frontend-gnome ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/frontends/gnome
            ${CMAKE_CURRENT_BINARY_DIR}/frontends/gnome
            COMMENT "Copying GNOME frontend files"
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
endif()
