cmake_minimum_required(VERSION 3.20)
project(Zaviro VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Detect cross-compilation for Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CROSS_WIN32 TRUE)
else()
    set(CROSS_WIN32 FALSE)
endif()

# Dependencies
if(CROSS_WIN32)
    # When cross-compiling, use find_package with custom paths
    find_package(glfw3 3.3 REQUIRED)
    # glm and nlohmann_json are header-only — use FetchContent
    include(FetchContent)
    FetchContent_Declare(
        glm
        URL https://github.com/g-truc/glm/releases/download/1.0.1/glm-1.0.1.zip
    )
    FetchContent_MakeAvailable(glm)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 REQUIRED)
    find_package(glm REQUIRED)
    find_package(nlohmann_json 3.11 REQUIRED)
endif()

# GLAD (bundled, generated with glad2)
set(GLAD_DIR ${CMAKE_SOURCE_DIR}/third_party/glad)
add_library(glad STATIC ${GLAD_DIR}/src/gl.c)
target_include_directories(glad PUBLIC ${GLAD_DIR}/include)

# Sources
set(SOURCES
    src/main.cpp
    src/engine/engine.cpp
    src/engine/renderer.cpp
    src/engine/window.cpp
    src/network/client.cpp
    src/game/world.cpp
    src/game/player.cpp
    src/util/stb_image_impl.cpp
)

add_executable(zaviro_client WIN32 ${SOURCES})

target_include_directories(zaviro_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

# Platform-specific linking
if(CROSS_WIN32)
    target_link_libraries(zaviro_client PRIVATE
        glfw
        glad
        glm::glm
        nlohmann_json::nlohmann_json
        opengl32
        ws2_32
    )
else()
    target_link_libraries(zaviro_client PRIVATE
        OpenGL::GL
        glfw
        glad
        glm::glm
        nlohmann_json::nlohmann_json
        pthread
    )
    target_compile_definitions(zaviro_client PRIVATE
        _GLFW_X11
    )
endif()
