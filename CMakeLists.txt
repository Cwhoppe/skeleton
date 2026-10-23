cmake_minimum_required(VERSION 3.20)
project(skeleton CXX)

add_executable(skeleton main.cpp)

target_compile_definitions(skeleton PRIVATE UNICODE _UNICODE)
set_target_properties(skeleton PROPERTIES 
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "/ENTRY:wWinMain"
)

# Tüm gerekli Windows ve C++ kütüphanelerini zorla bağla
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
)
