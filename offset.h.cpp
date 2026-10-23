#pragma once
#include <cstdint>

namespace Offsets {
    constexpr const wchar_t* PROCESS_NAME = L"Zula.exe";
    constexpr const char*    MODULE_NAME  = "GameAssembly.dll";

    constexpr uintptr_t ENTITY_LIST   = 0x0;
    constexpr uintptr_t ENTITY_COUNT  = 0x0;
    constexpr uintptr_t ENTITY_STRIDE = 0x0;
    constexpr uintptr_t BONE_MATRIX   = 0x0;
    constexpr uintptr_t TEAM_OFFSET   = 0x0;
    constexpr uintptr_t HEALTH_OFFSET = 0x0;

    constexpr int MAX_BONES = 20;
    constexpr int BONE_PARENT[MAX_BONES] = {
        -1, 0, 1, 2, 0, 4, 5, 0, 7, 8, 8, 10, 8, 12, 13, 13, 15, 13, 17, 18
    };
    constexpr int BONE_COUNT = 19;
}