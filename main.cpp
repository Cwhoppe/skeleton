#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "offsets.h"
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_espOn{false};

static HANDLE    g_proc = nullptr;
static uintptr_t g_base = 0;

DWORD GetPid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t GetModuleBase(DWORD pid, const char* module) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            char name[256];
            WideCharToMultiByte(CP_ACP, 0, me.szModule, -1, name, sizeof(name), nullptr, nullptr);
            if (_stricmp(name, module) == 0) { base = (uintptr_t)me.modBaseAddr; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

template<typename T>
bool ReadMem(uintptr_t addr, T& out) {
    return ReadProcessMemory(g_proc, (LPCVOID)addr, &out, sizeof(T), nullptr) != 0;
}

struct Matrix { float m[4][4]; };

bool ReadViewMatrix(Matrix& m) {
    constexpr uintptr_t VIEW_MATRIX = 0x0;
    if (!VIEW_MATRIX) return false;
    return ReadMem(g_base + VIEW_MATRIX, m);
}

struct Vec3 { float x, y, z; };

struct Bone {
    Vec3 pos;
    bool valid;
};

bool ReadBones(uintptr_t entity, Bone* bones) {
    uintptr_t bonePtr = 0;
    if (!ReadMem(entity + Offsets::BONE_MATRIX, bonePtr) || !bonePtr) return false;
    for (int i = 0; i < Offsets::MAX_BONES; ++i) {
        uintptr_t boneAddr = bonePtr + i * 0x30;
        Vec3 p{};
        if (ReadMem(boneAddr, p)) { bones[i].pos = p; bones[i].valid = true; }
        else { bones[i].valid = false; }
    }
    return true;
}

bool WorldToScreen(const Vec3& w, float& sx, float& sy, const Matrix& vm, int W, int H) {
    float cx = w.x * vm.m[0][0] + w.y * vm.m[1][0] + w.z * vm.m[2][0] + vm.m[3][0];
    float cy = w.x * vm.m[0][1] + w.y * vm.m[1][1] + w.z * vm.m[2][1] + vm.m[3][1];
    float cw = w.x * vm.m[0][3] + w.y * vm.m[1][3] + w.z * vm.m[2][3] + vm.m[3][3];
    if (cw < 0.001f) return false;
    float inv = 1.0f / cw;
    sx = (W / 2.0f) * (1.0f + cx * inv);
    sy = (H / 2.0f) * (1.0f - cy * inv);
    return true;
}

struct ScreenBone { float x, y; bool ok; };
struct PlayerDraw { std::vector<ScreenBone> bones; bool visible = false; };

void CollectPlayers(std::vector<PlayerDraw>& out, int W, int H) {
    out.clear();
    if (!g_proc || !g_base) return;

    uintptr_t list = 0, count = 0;
    if (!ReadMem(g_base + Offsets::ENTITY_LIST, list) || !list) return;
    if (!ReadMem(g_base + Offsets::ENTITY_COUNT, count) || count == 0 || count > 256) return;

    Matrix vm{};
    if (!ReadViewMatrix(vm)) return;

    for (uintptr_t i = 0; i < count; ++i) {
        uintptr_t ent = 0;
        if (!ReadMem(list + i * Offsets::ENTITY_STRIDE, ent) || !ent) continue;

        int hp = 0;
        if (!ReadMem(ent + Offsets::HEALTH_OFFSET, hp) || hp <= 0) continue;

        Bone bones[Offsets::MAX_BONES]{};
        if (!ReadBones(ent, bones)) continue;

        PlayerDraw pd;
        pd.bones.resize(Offsets::MAX_BONES);
        bool anyOk = false;
        for (int b = 0; b < Offsets::MAX_BONES; ++b) {
            pd.bones[b].ok = false;
            if (!bones[b].valid) continue;
            float sx, sy;
            if (WorldToScreen(bones[b].pos, sx, sy, vm, W, H)) {
                pd.bones[b] = { sx, sy, true };
                anyOk = true;
            }
        }
        if (anyOk) { pd.visible = true; out.push_back(std::move(pd)); }
    }
}

static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static IDXGISwapChain*         g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND                    g_hwnd = nullptr;

struct VERTEX { float x, y, z, rhw; DWORD color; };
static ID3D11InputLayout*  g_layout = nullptr;
static ID3D11Buffer*       g_vb = nullptr;
static ID3D11VertexShader* g_vs = nullptr;
static ID3D11PixelShader*  g_ps = nullptr;

const char* g_vsSrc = R"(
struct VS_IN { float4 pos : POSITION; float4 col : COLOR; };
struct VS_OUT { float4 pos : SV_POSITION; float4 col : COLOR; };
VS_OUT main(VS_IN i) { VS_OUT o; o.pos = i.pos; o.col = i.col; return o; }
)";
const char* g_psSrc = R"(
struct VS_OUT { float4 pos : SV_POSITION; float4 col : COLOR; };
float4 main(VS_OUT i) : SV_TARGET { return i.col; }
)";

bool InitD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        fls, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx))) return false;

    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();

    ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr; ID3DBlob* err = nullptr;
    D3DCompile(g_vsSrc, strlen(g_vsSrc), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &err);
    D3DCompile(g_psSrc, strlen(g_psSrc), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &err);
    if (!vsBlob || !psBlob) return false;

    g_dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs);
    g_dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps);

    D3D11_INPUT_ELEMENT_DESC ied[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    g_dev->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_layout);
    vsBlob->Release(); psBlob->Release();

    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(VERTEX) * 4096;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_dev->CreateBuffer(&bd, nullptr, &g_vb);

    return true;
}

void DrawLine(std::vector<VERTEX>& v, float x1, float y1, float x2, float y2, DWORD color) {
    v.push_back({ x1, y1, 0, 1, color });
    v.push_back({ x2, y2, 0, 1, color });
}

void Render(std::vector<PlayerDraw>& players, int W, int H) {
    if (!g_rtv) return;
    float clear[4] = { 0, 0, 0, 0 };
    g_ctx->ClearRenderTargetView(g_rtv, clear);

    if (!g_espOn || players.empty()) {
        g_swap->Present(0, 0);
        return;
    }

    std::vector<VERTEX> verts;
    verts.reserve(players.size() * Offsets::MAX_BONES * 2);

    for (auto& pd : players) {
        for (int b = 0; b < (int)pd.bones.size(); ++b) {
            int parent = Offsets::BONE_PARENT[b];
            if (parent < 0 || parent >= (int)pd.bones.size()) continue;
            if (!pd.bones[b].ok || !pd.bones[parent].ok) continue;
            DrawLine(verts, pd.bones[parent].x, pd.bones[parent].y,
                            pd.bones[b].x,        pd.bones[b].y, 0xFF00FF00);
        }
    }

    if (!verts.empty()) {
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (SUCCEEDED(g_ctx->Map(g_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            memcpy(ms.pData, verts.data(), verts.size() * sizeof(VERTEX));
            g_ctx->Unmap(g_vb, 0);

            D3D11_VIEWPORT vp{ 0, 0, (float)W, (float)H, 0, 1 };
            g_ctx->RSSetViewports(1, &vp);
            UINT stride = sizeof(VERTEX), offset = 0;
            g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &offset);
            g_ctx->IASetInputLayout(g_layout);
            g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            g_ctx->VSSetShader(g_vs, nullptr, 0);
            g_ctx->PSSetShader(g_ps, nullptr, 0);
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);

            D3D11_BLEND_DESC bd{};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            ID3D11BlendState* bs = nullptr;
            g_dev->CreateBlendState(&bd, &bs);
            float bf[4] = { 0,0,0,0 };
            g_ctx->OMSetBlendState(bs, bf, 0xFFFFFFFF);
            bs->Release();

            g_ctx->Draw((UINT)verts.size(), 0);
        }
    }
    g_swap->Present(0, 0);
}

static HWND g_btnToggle = nullptr;
static HWND g_btnExit = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if ((HWND)lp == g_btnToggle) {
            g_espOn = !g_espOn.load();
            SetWindowTextW(g_btnToggle, g_espOn ? L"ESP: AÇIK" : L"ESP: KAPALI");
        }
        if ((HWND)lp == g_btnExit) {
            g_running = false;
        }
        return 0;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SkeletonCtrl";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"SkeletonCtrl", L"Zula Skeleton Overlay",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) return 1;

    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_hwnd, &margins);

    if (!InitD3D(g_hwnd)) return 1;

    HWND ctrl = CreateWindowExW(WS_EX_TOPMOST,
        L"SkeletonCtrl", L"Zula Skeleton - Kontrol",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        100, 100, 260, 140,
        nullptr, nullptr, hInst, nullptr);

    g_btnToggle = CreateWindowW(L"BUTTON", L"ESP: KAPALI",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 20, 200, 40, ctrl, (HMENU)1, hInst, nullptr);

    g_btnExit = CreateWindowW(L"BUTTON", L"ÇIKIŞ",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 70, 200, 30, ctrl, (HMENU)2, hInst, nullptr);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    ShowWindow(ctrl, SW_SHOW);

    std::thread procFinder([] {
        while (g_running) {
            if (!g_proc) {
                DWORD pid = GetPid(Offsets::PROCESS_NAME);
                if (pid) {
                    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
                    if (g_proc) g_base = GetModuleBase(pid, Offsets::MODULE_NAME);
                }
            }
            Sleep(2000);
        }
    });
    procFinder.detach();

    MSG msg{};
    std::vector<PlayerDraw> players;

    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_running = false;
        }

        int W = GetSystemMetrics(SM_CXSCREEN);
        int H = GetSystemMetrics(SM_CYSCREEN);

        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, W, H, SWP_NOACTIVATE);

        if (g_espOn && g_proc) {
            CollectPlayers(players, W, H);
        } else {
            players.clear();
        }

        Render(players, W, H);
        Sleep(1);
    }

    if (g_proc) CloseHandle(g_proc);
    return 0;
}
