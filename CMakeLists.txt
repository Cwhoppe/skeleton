cmake_minimum_required(VERSION 3.20)
project(skeleton CXX)

# MSVC'nin standart kütüphaneleri bulması için Runtime ayarını zorla
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(skeleton WIN32 main.cpp)

# Tüm Windows ve C++ kütüphanelerini açıkça belirt
target_link_libraries(skeleton 
    d3d11 
    dxgi 
    dwmapi 
    d3dcompiler 
    user32 
    gdi32 
    ole32 
    msvcrt 
    vcruntime 
    kernel32
    advapi32
)

set_target_properties(skeleton PROPERTIES LINK_FLAGS "/ENTRY:wWinMain")
