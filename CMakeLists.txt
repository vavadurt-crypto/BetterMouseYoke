cmake_minimum_required(VERSION 3.16)
project(BetterMouseYoke)

set(CMAKE_CXX_STANDARD 17)

# -----------------------------------------------------------------------
# SDK path — expects SDK folder alongside CMakeLists.txt
# -----------------------------------------------------------------------
set(SDK_DIR "${CMAKE_SOURCE_DIR}/SDK")

include_directories(
    "${SDK_DIR}/CHeaders/XPLM"
    "${SDK_DIR}/CHeaders/Widgets"
)

add_library(BetterMouseYoke SHARED BetterMouseYoke.cpp)

# -----------------------------------------------------------------------
# Windows
# -----------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(BetterMouseYoke PRIVATE
        IBM=1 XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1 XPLM411=1 XPLM420=1)
    target_link_libraries(BetterMouseYoke
        "${SDK_DIR}/Libraries/Win/XPLM_64.lib"
    )
    set_target_properties(BetterMouseYoke PROPERTIES
        OUTPUT_NAME "win"
        SUFFIX ".xpl"
        PREFIX ""
    )

# -----------------------------------------------------------------------
# macOS
# -----------------------------------------------------------------------
elseif(APPLE)
    target_compile_definitions(BetterMouseYoke PRIVATE
        APL=1 XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1 XPLM411=1 XPLM420=1)
    find_library(CORE_GRAPHICS CoreGraphics)
    find_library(APP_KIT AppKit)
    target_link_libraries(BetterMouseYoke
        "${SDK_DIR}/Libraries/Mac/XPLM.framework"
        ${CORE_GRAPHICS}
        ${APP_KIT}
    )
    set_target_properties(BetterMouseYoke PROPERTIES
        OUTPUT_NAME "mac"
        SUFFIX ".xpl"
        PREFIX ""
        MACOSX_BUNDLE FALSE
    )

# -----------------------------------------------------------------------
# Linux
# -----------------------------------------------------------------------
elseif(UNIX)
    target_compile_definitions(BetterMouseYoke PRIVATE
        LIN=1 XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1 XPLM411=1 XPLM420=1)
    find_package(X11 REQUIRED)
    target_include_directories(BetterMouseYoke PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(BetterMouseYoke
        ${X11_LIBRARIES}
        Xtst
    )
    set_target_properties(BetterMouseYoke PROPERTIES
        OUTPUT_NAME "lin"
        SUFFIX ".xpl"
        PREFIX ""
    )
endif()
