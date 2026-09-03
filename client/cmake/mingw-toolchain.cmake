# CMake toolchain file for cross-compiling to Windows x64 with mingw-w64
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Search paths — all deps installed to /opt/cross-win64 in Docker
set(CMAKE_FIND_ROOT_PATH /opt/cross-win64 /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Hint cmake where to find package configs
set(glfw3_DIR /opt/cross-win64/lib/cmake/glfw3)
set(glm_DIR /opt/cross-win64/lib/cmake/glm)
set(nlohmann_json_DIR /opt/cross-win64/lib/cmake/nlohmann_json)
