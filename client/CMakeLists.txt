cmake_minimum_required(VERSION 3.20)
project(Zaviro VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)

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
)

add_executable(zaviro_client ${SOURCES})

target_include_directories(zaviro_client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

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
