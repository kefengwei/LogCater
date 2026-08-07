cmake_minimum_required(VERSION 3.21)
project(LogCater VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Extract version components
set(PROJECT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(PROJECT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# Release build by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# MSVC: treat source as UTF-8 (needed for Chinese text in HelpPanel)
if(MSVC)
    add_compile_options("/utf-8")
endif()

# --- Dependencies via FetchContent ---
include(FetchContent)

# GLFW
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Dear ImGui
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# nlohmann/json
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# QR Code generator (nayuki)
FetchContent_Declare(qrcodegen
    GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG v1.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(qrcodegen)

# --- ImGui library ---
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# --- LogCater application ---
file(GLOB_RECURSE SOURCES src/*.cpp src/*.h)

# Nayuki QR code generator source files
list(APPEND SOURCES
    ${qrcodegen_SOURCE_DIR}/cpp/QrCode.cpp
    ${qrcodegen_SOURCE_DIR}/cpp/QrSegment.cpp
    ${qrcodegen_SOURCE_DIR}/cpp/BitBuffer.cpp
)

# Configure Windows resource files with version info
configure_file(src/resource.h ${CMAKE_CURRENT_BINARY_DIR}/resource.h COPYONLY)
configure_file(src/resource.rc.in ${CMAKE_CURRENT_BINARY_DIR}/resource.rc @ONLY)
configure_file(src/logcater.ico ${CMAKE_CURRENT_BINARY_DIR}/logcater.ico COPYONLY)

add_executable(logcater WIN32 ${SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resource.rc)

target_include_directories(logcater PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${qrcodegen_SOURCE_DIR}/cpp
)

target_link_libraries(logcater PRIVATE
    imgui
    glfw
    nlohmann_json::nlohmann_json
    opengl32
    comdlg32
)

# Version for auto-update check
target_compile_definitions(logcater PRIVATE
    LOGCATER_VERSION="${PROJECT_VERSION}"
)

# Find adb.exe at configure time
find_program(ADB_EXECUTABLE adb)
if(ADB_EXECUTABLE)
    message(STATUS "Found ADB: ${ADB_EXECUTABLE}")
    target_compile_definitions(logcater PRIVATE
        ADB_PATH="${ADB_EXECUTABLE}"
    )
endif()

# --- Tests ---
enable_testing()
add_executable(logcater_tests
    tests/test_core.cpp
    src/core/LogBuffer.cpp
    src/util/Timestamp.cpp
    src/util/StringUtils.cpp
)
target_include_directories(logcater_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(logcater_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME core_tests COMMAND logcater_tests)

# --- Packaging (Windows installer via CPack + NSIS) ---
# CPack's NSIS template creates shortcuts pointing at $INSTDIR\bin\<exe>,
# so install the executable (and its adb) under bin/.
install(TARGETS logcater RUNTIME DESTINATION bin)
if(ADB_EXECUTABLE)
    get_filename_component(ADB_DIR "${ADB_EXECUTABLE}" DIRECTORY)
    install(FILES
        "${ADB_DIR}/adb.exe"
        "${ADB_DIR}/AdbWinApi.dll"
        "${ADB_DIR}/AdbWinUsbApi.dll"
        DESTINATION bin/adb)
endif()

set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "LogCater")
set(CPACK_PACKAGE_VENDOR "kefengwei")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Android logcat viewer and device manager")
set(CPACK_PACKAGE_FILE_NAME "LogCater-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "LogCater")
set(CPACK_PACKAGE_EXECUTABLES "logcater" "LogCater")
set(CPACK_CREATE_DESKTOP_LINKS "logcater")
set(CPACK_NSIS_DISPLAY_NAME "LogCater")
set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/logcater.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/src/logcater.ico")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
               ${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
include(CPack)
