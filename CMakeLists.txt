cmake_minimum_required(VERSION 3.21)
project(LogCater VERSION 1.2.1 LANGUAGES CXX)

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

# Configure Windows resource files with version info
configure_file(src/resource.rc.in ${CMAKE_CURRENT_BINARY_DIR}/resource.rc @ONLY)
configure_file(src/logcater.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/logcater.manifest @ONLY)
configure_file(src/logcater.ico ${CMAKE_CURRENT_BINARY_DIR}/logcater.ico COPYONLY)

add_executable(logcater WIN32 ${SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resource.rc)

target_include_directories(logcater PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(logcater PRIVATE
    imgui
    glfw
    nlohmann_json::nlohmann_json
    opengl32
    comdlg32
    winhttp
)

# Version for auto-update check
target_compile_definitions(logcater PRIVATE
    LOGCATER_VERSION="${PROJECT_VERSION}"
)

# Disable linker-generated manifest — we provide our own via resource.rc
target_link_options(logcater PRIVATE "/MANIFEST:NO")

# Find adb.exe at configure time
find_program(ADB_EXECUTABLE adb)
if(ADB_EXECUTABLE)
    message(STATUS "Found ADB: ${ADB_EXECUTABLE}")
    target_compile_definitions(logcater PRIVATE
        ADB_PATH="${ADB_EXECUTABLE}"
    )
endif()
