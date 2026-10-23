cmake_minimum_required(VERSION 3.20)
project(skeleton CXX)

add_executable(skeleton WIN32 main.cpp)
target_link_libraries(skeleton d3d11 dxgi dwmapi d3dcompiler user32)
set_target_properties(skeleton PROPERTIES LINK_FLAGS "/ENTRY:wWinMain")