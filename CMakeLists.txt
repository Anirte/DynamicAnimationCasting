cmake_minimum_required(VERSION 3.21)

project(
    DynamicAnimationCasting
    VERSION 3.2.5
    DESCRIPTION "A SKSE plugin to trigger spells on animation events."
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR "in-source builds are not allowed")
endif()

# ---- CommonLibSSE-NG (GPL-3.0-or-later), built from source for SE + AE ----
#
# Pinned to v10.1.0. Pass -DCOMMONLIB_DIR=<path> to use an existing checkout instead of downloading.

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

if(DEFINED COMMONLIB_DIR)
    add_subdirectory("${COMMONLIB_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)
    include("${COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
else()
    include(FetchContent)
    FetchContent_Declare(
        CommonLibSSE
        GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
        GIT_TAG 39f9d07a6ffabea8fb559eee87ab7d27cd463e8a # v10.1.0
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(CommonLibSSE)
    include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# toml++ is used header-only: the plugin sets TOML_EXCEPTIONS=0, which the prebuilt library does not provide
find_path(TOMLPP_INCLUDE_DIR "toml++/toml.h" REQUIRED)

# ---- Plugin ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    @ONLY)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR ArcEarth
    SOURCES
        src/main.cpp
        src/DynamicAnimationCasting.cpp
        src/DynamicAnimationCasting.h
        src/Framework.cpp
        src/Framework.h
        src/AutoTest.cpp
        src/AutoTest.h
        src/PCH.h
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc")

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "loki_DynamicAnimationCasting")

# Test build: adds the DynAnimTest console command (in-game self test of the HUD flash fix). Never release it.
option(DAC_AUTOTEST "Build the in-game self test into the plugin" OFF)
if(DAC_AUTOTEST)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAC_AUTOTEST=1)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE NOMINMAX _AMD64_ WIN32_LEAN_AND_MEAN TOML_HEADER_ONLY=1)
target_compile_options(${PROJECT_NAME} PRIVATE /bigobj /utf-8)
target_include_directories(${PROJECT_NAME} PRIVATE src "${TOMLPP_INCLUDE_DIR}")
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog fmt::fmt)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
