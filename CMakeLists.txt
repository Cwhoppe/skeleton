cmake_minimum_required(VERSION 3.20)
project(skeleton CXX)

# Standart C++ kütüphanesini ve Windows'u bağla
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(skeleton WIN32 main.cpp)
target_link_libraries(skeleton d3d11 dxgi dwmapi d3dcompiler user32 gdi32 ole32)
set_target_properties(skeleton PROPERTIES LINK_FLAGS "/ENTRY:wWinMain")
