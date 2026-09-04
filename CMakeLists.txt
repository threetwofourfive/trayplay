cmake_minimum_required(VERSION 3.16)
project(TrayConsole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_CORE "Build the core daemon" ON)
option(BUILD_FRONTEND_GNOME "Build GNOME extension frontend" OFF)
option(BUILD_FRONTEND_KDE "Build KDE Plasma frontend" OFF)
option(BUILD_FRONTEND_CINNAMON "Build Cinnamon frontend" OFF)
option(BUILD_FRONTEND_XFCE "Build XFCE frontend" OFF)
option(BUILD_FRONTEND_WAYBAR "Build Waybar frontend" OFF)

if(BUILD_CORE)
    add_executable(tray-console-daemon
        core/main.cpp
    )
endif()

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
    # Search for dependencies optionally so cmake configuration does not fail
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
