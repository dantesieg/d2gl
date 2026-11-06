cmake_minimum_required(VERSION 3.16)
project(hdr10_proto CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WARN -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror)

# Portable colour-math tests: build natively (macOS/Linux) or as a Windows exe.
add_executable(test_color tests/test_color.cpp)
target_include_directories(test_color PRIVATE src)
target_compile_options(test_color PRIVATE ${WARN})

if(WIN32)
  add_executable(hdr10_proto
    src/main.cpp src/gl_loader.cpp src/interop.cpp src/d3d_present.cpp
    src/scene.cpp src/font.cpp src/config.cpp src/log.cpp)
  target_include_directories(hdr10_proto PRIVATE src)
  target_compile_options(hdr10_proto PRIVATE ${WARN} -Wno-cast-function-type)
  # d3dcompiler_47 is loaded at runtime (LoadLibrary), not linked.
  target_link_libraries(hdr10_proto PRIVATE d3d11 dxgi opengl32 gdi32 user32 dxguid)
  target_link_options(hdr10_proto PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_options(test_color PRIVATE -static -static-libgcc -static-libstdc++)
endif()
