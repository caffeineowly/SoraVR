// SoraVR - alternate-eye stereo for Trails in the Sky 1st Chapter (D3D11).
//
// Step 1: d3d11.dll proxy that finds the game's per-frame camera constant buffer
// (cb_scene, 1184 bytes: view, viewInv, proj, projInv, viewProj, viewProjInv - layout
// taken from the geo-11 shader fixes) and shifts the camera half an eye distance
// left / right on alternate frames. No VR output yet.
//
// Hotkeys (configurable in SoraVR.ini):
//   F10  stereo on/off
//   F11  mode: alternate L/R -> left only -> right only
//   F7/F8 separation smaller/larger (x0.8 / x1.25)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <dxgi1_2.h>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "xr.h"

// ----------------------------------------------------------------------------
// logging / config
// ----------------------------------------------------------------------------
static FILE*      g_log = nullptr;
static std::mutex g_logMx;
static char       g_dir[MAX_PATH];        // game exe directory, with trailing backslash
static char       g_ini[MAX_PATH];

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMx);
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

struct Config {
    bool  enabled    = true;
    int   mode       = 0;       // 0 alternate, 1 left only, 2 right only
    float separation = 0.064f;  // full eye distance, game units
    int   keyToggle  = VK_F10;
    int   keyMode    = VK_F11;
    int   keySepDown = VK_F7;
    int   keySepUp   = VK_F8;
    bool  swapEyes   = false;
    int   keySwapEyes = VK_F6;
    bool  headTracking = true;   // use the headset pose for the camera (needs OpenXR running)
    float autoRecenterSec = 3.0f; // recenter once this long after head tracking starts (0 = off)
    bool  levelHorizon = false;  // drop the game camera's pitch/roll; your head does that
    float worldScale = 1.0f;     // game units per real metre (scales IPD and head movement)
    float fovScale   = 1.0f;     // >1 renders a wider view than the game's own camera
    int   keyRecenter = VK_F1;
    int   keyTracking = VK_F4;
    int   keyLevel    = VK_F5;
    int   keyFovDown  = VK_F2;
    int   keyFovUp    = VK_F3;
    // HUD panel
    bool  hud         = true;    // separate the game's UI onto a floating panel
    bool  hudHeadLock = false;
    bool  panelCursor = true;    // draw the Windows cursor onto the HUD panel
    bool  pointer     = true;    // VR controller laser pointer moves the cursor / clicks   // panel follows the head instead of staying in the room
    float hudDistance = 1.6f;    // metres
    float hudWidth    = 1.6f;    // metres
    int   keyHud = VK_INSERT, keyHudLock = VK_DELETE, keyHudNear = VK_PRIOR, keyHudFar = VK_NEXT, keyHudBig = VK_HOME, keyHudSmall = VK_END;
    // game FOV override (needs the FOV probe, F12, to have found the camera's FOV in memory)
    bool  dual        = true;    // render the frame twice (left + right eye) via the game's own render call
    bool  paired      = false;    // freeze the game clock on every 2nd frame so each eye pair shows one moment
    int   keyPaired   = VK_F9;   // F9 toggles pairing; Ctrl+F9 = diagnostic capture
    bool  fovLock     = true;    // force the rendered FOV to GameFov (ignores the game's title-card zooms etc.)
    int   keyFovLock  = VK_F12;  // F12 toggles the lock; Ctrl+F12 runs the manual FOV probe
    bool  autoFov     = true;    // find the camera FOV automatically (saved pointer paths, then safe probe)
    bool  hideLetterbox = true;
    bool  holdScene   = false;   // menus: pin the flat snapshot in the room (experimental, off)
    bool  stereoAllCameras = false;  // head tracking + eye offset for menu cameras too (8e test: made menu models worse)
    bool  menuBackdrop = true;
    float backdropScale = 1.4f;  // backdrop screen size vs the view it shows (title screen)
    bool  panelPerEye = true;    // dual render: each eye gets its own HUD panel image
    bool  menuModelStereo = true; float menuModelSep = 0.14f;    // 3D models in menus: eye separation (user-tuned)
    float menuModelConv = 4.0f;  // distance (game units) that sits exactly on the panel; smaller = model further back   // menus: the game's flat snapshot shown as a screen in the room
    float backdropDist = 4.0f;    // menus: keep the last 3D view instead of the game's flat snapshot
    bool  controllers = true;
    bool  physics     = true;    // controllers push hair / cloth (DynamicBone physics)
    float physRadius  = 0.010f;  // extra radius on top of the orb's radius (user-tuned in 10d with world scale fixed; total 0.038 m)
    float physOffset  = 0.0f;    // move the sphere along the pointing direction (+ = forward)
    bool  physDebug   = false;   // draw the physics sphere (Ctrl+Insert)    // draw the controllers (orb) in the world
    bool  ctrlFade    = true;
    float orbSize     = 0.055f;  // orb diameter, metres
    float ctrlHold    = 1.5f, ctrlFadeTime = 1.0f, ctrlMargin = 0.015f;  // hide the black cutscene bars on the HUD panel
    bool  outlineFix  = true;    // keep toon outlines the same width when the game FOV is overridden
    float gameFov     = 90.0f;   // vertical degrees the game camera is forced to while the override is active
    int   keyFovProbe = VK_F12;
    int   keyLiveWorld = 0;          // experiment: extra key for the live world toggle (default: Ctrl+F10 only)
    bool  liveWorld   = false;
    bool  liveTitle   = false;
    bool  menuBlur    = true;    // blur + dim the live world behind menus (follows the game's menu fade)
    float menuBlurRadius = 1.5f, menuDim = 0.6f;   // experiment: also draw the title screen / transitions live
} g_cfg;

static void LoadWorldVs();
struct ID3D11Resource;
static void RecordPrep(ID3D11Resource* res, const void* mono, UINT size, bool dynamic);
static void InstallRenderHook();
static float g_monoViewInv[16], g_monoView[16]; static bool g_haveMonoView = false;
static void InstallClockHooks();
static void LoadBillboardCfg();
static bool g_billboardFix = true;   // head-tracked replacement for the world-UI billboard VS

static int IniKey(const char* name, int def) {
    char buf[64];
    GetPrivateProfileStringA("Keys", name, "", buf, sizeof buf, g_ini);
    if (!buf[0]) return def;
    if ((buf[0] == 'F' || buf[0] == 'f') && buf[1] >= '1' && buf[1] <= '9') {   // F1..F24
        int n = atoi(buf + 1); if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (!strncmp(buf, "0x", 2) || !strncmp(buf, "0X", 2)) return (int)strtol(buf, nullptr, 16);
    static const struct { const char* n; int vk; } names[] = {
        { "Insert", VK_INSERT }, { "Delete", VK_DELETE }, { "Home", VK_HOME }, { "End", VK_END },
        { "PageUp", VK_PRIOR }, { "PageDown", VK_NEXT }, { "Pause", VK_PAUSE }, { "ScrollLock", VK_SCROLL },
        { "Backspace", VK_BACK }, { "Tab", VK_TAB }, { "Space", VK_SPACE } };
    for (const auto& k : names) if (!_stricmp(buf, k.n)) return k.vk;
    if (buf[1] == 0 && buf[0] >= 'a' && buf[0] <= 'z') return buf[0] - 'a' + 'A';
    if (buf[1] == 0 && ((buf[0] >= 'A' && buf[0] <= 'Z') || (buf[0] >= '0' && buf[0] <= '9'))) return buf[0];
    return def;
}

// Key with a newer, clearer name; the old name (from older inis) still works.
static int IniKey2(const char* name, const char* oldName, int def) { return IniKey(name, IniKey(oldName, def)); }

static void LoadConfig() {
    char buf[64];
    g_cfg.enabled = GetPrivateProfileIntA("Stereo", "Enabled", 1, g_ini) != 0;
    g_cfg.mode    = GetPrivateProfileIntA("Stereo", "Mode", 0, g_ini) % 3;
    GetPrivateProfileStringA("Stereo", "Separation", "0.064", buf, sizeof buf, g_ini);
    g_cfg.separation = (float)atof(buf);
    if (!(g_cfg.separation > 0.0f && g_cfg.separation < 100.0f)) g_cfg.separation = 0.064f;
    g_cfg.keyToggle  = IniKey2("Stereo3D", "Toggle", VK_F10);
    g_cfg.keyMode    = IniKey2("EyeTest", "Mode", VK_F11);
    g_cfg.keySepDown = IniKey2("WorldScaleDown", "SeparationDown", VK_F7);
    g_cfg.keySepUp   = IniKey2("WorldScaleUp", "SeparationUp", VK_F8);
    g_cfg.swapEyes   = GetPrivateProfileIntA("Stereo", "SwapEyes", 0, g_ini) != 0;
    g_cfg.keySwapEyes = IniKey("SwapEyes", VK_F6);
    // Always on at start (F4 toggles it for the session only; it used to be saved, so a session that
    // ended with tracking off started the next one off). [VR] HeadTrackingAtStart=0 to start off.
    g_cfg.headTracking = GetPrivateProfileIntA("VR", "HeadTrackingAtStart", 1, g_ini) != 0;
    {   char b[32]; GetPrivateProfileStringA("VR", "AutoRecenterSeconds", "3", b, sizeof b, g_ini); g_cfg.autoRecenterSec = (float)atof(b); }
    g_cfg.levelHorizon = GetPrivateProfileIntA("VR", "LevelHorizon", 0, g_ini) != 0;
    GetPrivateProfileStringA("VR", "WorldScale", "1.0", buf, sizeof buf, g_ini);
    g_cfg.worldScale = (float)atof(buf); if (!(g_cfg.worldScale > 0.01f && g_cfg.worldScale < 100.0f)) g_cfg.worldScale = 1.0f;
    GetPrivateProfileStringA("VR", "FovScale", "1.0", buf, sizeof buf, g_ini);
    g_cfg.fovScale = (float)atof(buf); if (!(g_cfg.fovScale >= 0.5f && g_cfg.fovScale <= 2.5f)) g_cfg.fovScale = 1.0f;
    g_cfg.keyRecenter = IniKey("Recenter", VK_F1);
    g_cfg.keyTracking = IniKey("HeadTracking", VK_F4);
    g_cfg.keyLevel    = IniKey("LevelHorizon", VK_F5);
    g_cfg.keyFovDown  = IniKey("FovDown", VK_F2);
    g_cfg.keyFovUp    = IniKey("FovUp", VK_F3);
    g_cfg.hud         = GetPrivateProfileIntA("HUD", "Enabled", 1, g_ini) != 0;
    g_cfg.hudHeadLock = GetPrivateProfileIntA("HUD", "HeadLocked", 0, g_ini) != 0;
    g_cfg.panelCursor = GetPrivateProfileIntA("HUD", "Cursor", 1, g_ini) != 0;
    g_cfg.pointer     = GetPrivateProfileIntA("HUD", "ControllerPointer", 1, g_ini) != 0;
    GetPrivateProfileStringA("HUD", "Distance", "1.6", buf, sizeof buf, g_ini); g_cfg.hudDistance = fminf(fmaxf((float)atof(buf), 0.3f), 20.0f);
    GetPrivateProfileStringA("HUD", "Width", "1.6", buf, sizeof buf, g_ini);    g_cfg.hudWidth = fminf(fmaxf((float)atof(buf), 0.2f), 20.0f);
    g_cfg.outlineFix = GetPrivateProfileIntA("VR", "OutlineFix", 1, g_ini) != 0;
    g_cfg.hideLetterbox = GetPrivateProfileIntA("HUD", "HideLetterbox", 1, g_ini) != 0;
    {
        char b[64];
        auto rf = [&](const char* k, const char* d) { GetPrivateProfileStringA("Controllers", k, d, b, sizeof b, g_ini); return (float)atof(b); };
        g_cfg.controllers = GetPrivateProfileIntA("Controllers", "Show", 1, g_ini) != 0;
        g_cfg.physics = GetPrivateProfileIntA("Controllers", "Physics", 1, g_ini) != 0;
        g_cfg.physRadius = rf("PhysicsExtraRadius", "0.010");
        g_cfg.physOffset = rf("PhysicsOffset", "0.0");
        g_cfg.holdScene = GetPrivateProfileIntA("Stereo", "HoldSceneInMenus", 0, g_ini) != 0;
        g_cfg.stereoAllCameras = GetPrivateProfileIntA("Stereo", "StereoAllCameras", 0, g_ini) != 0;
        g_cfg.menuBackdrop = GetPrivateProfileIntA("Stereo", "MenuBackdropScreen", 1, g_ini) != 0;
        g_cfg.panelPerEye = GetPrivateProfileIntA("HUD", "PanelPerEye", 1, g_ini) != 0;
        g_cfg.menuModelStereo = GetPrivateProfileIntA("HUD", "MenuModelStereo", 1, g_ini) != 0;
        GetPrivateProfileStringA("HUD", "MenuModelSeparation", "0.14", b, sizeof b, g_ini); g_cfg.menuModelSep = (float)atof(b);
        GetPrivateProfileStringA("HUD", "MenuModelConvergence", "4.0", b, sizeof b, g_ini); g_cfg.menuModelConv = (float)atof(b);
        GetPrivateProfileStringA("Stereo", "MenuBackdropScale", "1.4", b, sizeof b, g_ini); g_cfg.backdropScale = (float)atof(b);
        GetPrivateProfileStringA("Stereo", "MenuBackdropDistance", "4.0", b, sizeof b, g_ini); g_cfg.backdropDist = (float)atof(b);
        if (g_cfg.backdropDist < 1.0f) g_cfg.backdropDist = 1.0f;
        g_cfg.ctrlFade = GetPrivateProfileIntA("Controllers", "FadeWhenStill", 1, g_ini) != 0;
        g_cfg.orbSize = rf("OrbSize", "0.055");
        g_cfg.ctrlHold = rf("StillSeconds", "1.5");
        g_cfg.ctrlFadeTime = rf("FadeSeconds", "1.0");
        g_cfg.ctrlMargin = rf("MoveMargin", "0.015");
        xr::ControllerOptions o; o.show = g_cfg.controllers; o.orbSize = g_cfg.orbSize; o.fade = g_cfg.ctrlFade; o.holdSec = g_cfg.ctrlHold; o.fadeSec = g_cfg.ctrlFadeTime; o.moveMargin = g_cfg.ctrlMargin;
        xr::SetControllerOptions(o);
    }
    g_cfg.autoFov    = GetPrivateProfileIntA("VR", "AutoFov", 1, g_ini) != 0;
    g_cfg.fovLock    = GetPrivateProfileIntA("VR", "FovLock", 1, g_ini) != 0;
    g_cfg.paired     = GetPrivateProfileIntA("Stereo", "PairedFrames", 0, g_ini) != 0;
    g_cfg.dual       = GetPrivateProfileIntA("Stereo", "DualRender", 1, g_ini) != 0;
    g_cfg.keyPaired  = IniKey("DualRender", VK_F9);
    GetPrivateProfileStringA("VR", "GameFov", "90", buf, sizeof buf, g_ini);    g_cfg.gameFov = fminf(fmaxf((float)atof(buf), 20.0f), 140.0f);
    g_cfg.keyHud = IniKey2("Panel", "HudToggle", VK_INSERT);   g_cfg.keyHudLock = IniKey2("PanelFollowHead", "HudHeadLock", VK_DELETE);
    g_cfg.keyHudNear = IniKey2("PanelNearer", "HudNearer", VK_PRIOR); g_cfg.keyHudFar = IniKey2("PanelFarther", "HudFarther", VK_NEXT);
    g_cfg.keyHudBig = IniKey2("PanelBigger", "HudBigger", VK_HOME);   g_cfg.keyHudSmall = IniKey2("PanelSmaller", "HudSmaller", VK_END);
    g_cfg.keyFovProbe = IniKey("FovProbe", VK_F12);
    g_cfg.keyLiveWorld = IniKey("LiveWorldInMenus", 0);
    g_cfg.liveWorld = GetPrivateProfileIntA("Stereo", "LiveWorldInMenus", 1, g_ini) != 0;
    g_cfg.liveTitle = GetPrivateProfileIntA("Stereo", "LiveTitleScreen", 0, g_ini) != 0;
    g_cfg.menuBlur = GetPrivateProfileIntA("Stereo", "MenuBlur", 1, g_ini) != 0;
    {   char b[32];
        GetPrivateProfileStringA("Stereo", "MenuBlurRadius", "1.5", b, sizeof b, g_ini); g_cfg.menuBlurRadius = (float)atof(b);
        GetPrivateProfileStringA("Stereo", "MenuDim", "0.6", b, sizeof b, g_ini); g_cfg.menuDim = (float)atof(b); }
    g_cfg.keyFovLock  = IniKey("FovLock", VK_F12);
    LoadWorldVs();
    LoadBillboardCfg();
}

// Saves only the preferences the hotkeys adjust (world scale, FOV, panel size/distance/mode, eye
// swap). On/off toggles (F4, F5, F9-F12, Insert) last for the session only, so a key pressed while
// testing doesn't stick, and the ini never gains keys it didn't have.
static void SaveConfig() {
    char buf[64];
    snprintf(buf, sizeof buf, "%.4f", g_cfg.worldScale); WritePrivateProfileStringA("VR", "WorldScale", buf, g_ini);
    snprintf(buf, sizeof buf, "%.1f", g_cfg.gameFov);    WritePrivateProfileStringA("VR", "GameFov", buf, g_ini);
    WritePrivateProfileStringA("Stereo", "SwapEyes", g_cfg.swapEyes ? "1" : "0", g_ini);
    WritePrivateProfileStringA("HUD", "HeadLocked", g_cfg.hudHeadLock ? "1" : "0", g_ini);
    snprintf(buf, sizeof buf, "%.2f", g_cfg.hudDistance); WritePrivateProfileStringA("HUD", "Distance", buf, g_ini);
    snprintf(buf, sizeof buf, "%.2f", g_cfg.hudWidth);    WritePrivateProfileStringA("HUD", "Width", buf, g_ini);
}

#include "default_ini.h"      // kDefaultIni, generated by build.sh from dist/SoraVR.ini
static void WriteDefaultIniIfMissing() {
    if (GetFileAttributesA(g_ini) != INVALID_FILE_ATTRIBUTES) return;
    FILE* f = fopen(g_ini, "wb");
    if (!f) return;
    fputs(kDefaultIni, f);
    fclose(f);
    Log("created %s with the default settings", g_ini);
}

// ----------------------------------------------------------------------------
// real d3d11
// ----------------------------------------------------------------------------
using PFN_CreateDevice = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PFN_CreateDeviceAndSwapChain = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**,
    D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static HMODULE                      g_real = nullptr;
static PFN_CreateDevice             g_realCreateDevice = nullptr;
static PFN_CreateDeviceAndSwapChain g_realCreateDeviceAndSwapChain = nullptr;
static std::once_flag               g_initOnce;

static void InstallCrashLogger();
struct GameAddrs;
static const GameAddrs& Game();
static void Init() {
    std::call_once(g_initOnce, [] {
        GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
        char* s = strrchr(g_dir, '\\'); if (s) s[1] = 0; else g_dir[0] = 0;
        snprintf(g_ini, sizeof g_ini, "%sSoraVR.ini", g_dir);
        std::string logPath = std::string(g_dir) + "SoraVR.log";
        g_log = fopen(logPath.c_str(), "w");
        Log("SoraVR v1.0-rc3 (game code found by signatures: 1.06.1 and 1.07)");
        InstallCrashLogger();
        {   // the mod was called SkyStereo until v1.0-rc2: keep the user's settings
            char old[MAX_PATH]; snprintf(old, sizeof old, "%sSkyStereo.ini", g_dir);
            if (GetFileAttributesA(g_ini) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(old) != INVALID_FILE_ATTRIBUTES) {
                if (MoveFileA(old, g_ini)) Log("renamed your SkyStereo.ini to SoraVR.ini (settings kept)");
                else Log("found SkyStereo.ini but could not rename it to SoraVR.ini (error %lu)", GetLastError());
            }
        }
        WriteDefaultIniIfMissing();
        LoadConfig();
        Log("config: enabled=%d mode=%d separation=%.4f", g_cfg.enabled, g_cfg.mode, g_cfg.separation);
        Game();                                        // find the game code to patch (logs what it found)

        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH);
        strcat(sys, "\\d3d11.dll");
        g_real = LoadLibraryA(sys);
        if (!g_real) { Log("FATAL: could not load %s", sys); return; }
        g_realCreateDevice = (PFN_CreateDevice)GetProcAddress(g_real, "D3D11CreateDevice");
        g_realCreateDeviceAndSwapChain = (PFN_CreateDeviceAndSwapChain)GetProcAddress(g_real, "D3D11CreateDeviceAndSwapChain");
        Log("real d3d11 loaded from %s", sys);
        InstallClockHooks();
        static const char* watch[] = { "amd_ags_x64.dll", "nvapi64.dll", "d3d12.dll", "dxgi.dll", "d3d11on12.dll", "openxr_loader.dll", "GameOverlayRenderer64.dll", "RTSSHooks64.dll" };
        for (const char* m : watch) if (GetModuleHandleA(m)) Log("module already loaded: %s", m);
    });
}

// ----------------------------------------------------------------------------
// vtable patching
// ----------------------------------------------------------------------------
static void* PatchVtable(void* obj, int index, void* hook) {
    void** vt = *(void***)obj;
    if (vt[index] == hook) return nullptr;            // already ours
    DWORD old;
    VirtualProtect(&vt[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    void* orig = vt[index];
    vt[index] = hook;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
    return orig;
}

// ----------------------------------------------------------------------------
// stereo state
// ----------------------------------------------------------------------------
static volatile LONG g_frame = 0;         // incremented every Present
static LONG  g_sceneMatches = 0, g_sceneRejects = 0, g_sceneMatchesLastReport = 0;
static bool  g_loggedFirstScene = false;
static float g_tanH = 0.0f, g_tanV = 0.0f;   // half-FOV tangents actually rendered (after FovScale)
static float g_gameP00 = 0.0f, g_gameP11 = 0.0f;   // the game's own projection scale, before our changes

// eye for the frame currently being rendered: -1 left, +1 right, 0 none
static int  g_dualEye = 0;          // eye of the dual-render pass in progress (0 = none)
static bool g_inRender = false;     // inside the game's render call
static bool g_dualActive = false;   // dual rendering is running this frame
static int CurrentEye() {
    if (!g_cfg.enabled) return 0;
    if (g_dualActive) return g_dualEye ? g_dualEye : -1;   // prepare phase uses the left eye
    switch (g_cfg.mode) {
    case 1: return -1;
    case 2: return +1;
    default: return (g_frame & 1) ? +1 : -1;
    }
}

// cb_scene layout (floats): view 0, viewInv 16, proj 32, projInv 48, viewProj 64, viewProjInv 80
enum { kView = 0, kViewInv = 16, kProj = 32, kProjInv = 48, kViewProj = 64, kViewProjInv = 80 };
static const UINT kSceneSize = 1184;   // cb_scene: 74 registers (shadowMtx_g[4] ends at c74)

// Matrices are stored one shader register per row: out[r] = dot(reg[r], v).
// So element (r,c) of the math matrix A (out = A * v) is m[r*4+c].
static inline float& E(float* m, int r, int c) { return m[r * 4 + c]; }

static bool LooksLikeScene(const float* f, bool* perspective) {
    // view rotation rows ~unit length
    for (int r = 0; r < 3; r++) {
        float l = f[kView + r*4]*f[kView + r*4] + f[kView + r*4+1]*f[kView + r*4+1] + f[kView + r*4+2]*f[kView + r*4+2];
        if (!(fabsf(l - 1.0f) < 0.02f)) return false;
    }
    // viewProj == proj * view
    float maxErr = 0, maxVal = 1e-6f;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += f[kProj + r*4 + k] * f[kView + k*4 + c];
            maxErr = fmaxf(maxErr, fabsf(s - f[kViewProj + r*4 + c]));
            maxVal = fmaxf(maxVal, fabsf(s));
        }
    if (!(maxErr <= 1e-3f * maxVal + 1e-4f)) return false;
    // perspective: last row (0,0,+-1,0); orthographic (shadow passes): (0,0,0,1)
    const float* p = f + kProj;
    *perspective = fabsf(p[12]) < 1e-4f && fabsf(p[13]) < 1e-4f && fabsf(fabsf(p[14]) - 1.0f) < 1e-3f && fabsf(p[15]) < 1e-4f;
    return true;
}

static bool Invert44(const float* m, float* out) {      // general 4x4 inverse (Gauss-Jordan, partial pivoting)
    double a[4][8];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 8; c++) a[r][c] = c < 4 ? m[r*4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; c++) {
        int piv = c; for (int r = c + 1; r < 4; r++) if (fabs(a[r][c]) > fabs(a[piv][c])) piv = r;
        if (fabs(a[piv][c]) < 1e-12) return false;
        if (piv != c) for (int k = 0; k < 8; k++) { double t = a[c][k]; a[c][k] = a[piv][k]; a[piv][k] = t; }
        double d = a[c][c]; for (int k = 0; k < 8; k++) a[c][k] /= d;
        for (int r = 0; r < 4; r++) if (r != c) { double x = a[r][c]; if (x != 0) for (int k = 0; k < 8; k++) a[r][k] -= x * a[c][k]; }
    }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out[r*4 + c] = (float)a[r][c + 4];
    return true;
}
static void Mul44(const float* a, const float* b, float* out);

// Convergence for a stereo pair rendered onto a flat panel (menu models): after ApplyEyeOffset(o),
// shift the image so points at view distance C have zero disparity (they sit on the panel; nearer
// ones come out of it, farther ones go behind). NDC x -= P00*o/C  ->  P[0][2] += P00*o/C.
static void ApplyConvergence(float* f, float o, float C) {
    if (C <= 0.01f) return;
    float* P = f + kProj;
    P[2] += P[0] * o / C;
    float Pi[16]; if (!Invert44(P, Pi)) return;
    memcpy(f + kProjInv, Pi, 64);
    Mul44(P, f + kView, f + kViewProj);
    Mul44(f + kViewInv, f + kProjInv, f + kViewProjInv);
}

static void ApplyEyeOffset(float* f, float o) {
    // o = view-space x translation to add (+half sep for the left eye, -half for the right)
    float* V = f + kView; float* Vi = f + kViewInv; float* P = f + kProj;
    float* Pi = f + kProjInv; float* VP = f + kViewProj; float* VPi = f + kViewProjInv;
    E(V, 0, 3) += o;                                        // V'  = T(o) V
    for (int r = 0; r < 4; r++) E(VP, r, 3) += o * E(P, r, 0);   // VP' = P V'
    for (int r = 0; r < 4; r++) E(Vi, r, 3) -= o * E(Vi, r, 0);  // V'^-1 = V^-1 T(-o)
    float out[16];                                          // VP'^-1 = V'^-1 P^-1
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += E(Vi, r, k) * E(Pi, k, c);
            out[r*4 + c] = s;
        }
    memcpy(VPi, out, sizeof out);
}

// --- 4x4 helpers in the same row-per-register layout (out = M * v) ---
static void Mul44(const float* a, const float* b, float* out) {
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[r*4 + k] * b[k*4 + c];
            t[r*4 + c] = s;
        }
    memcpy(out, t, sizeof t);
}
static void RigidInverse(const float* m, float* out) {      // [R|t] -> [R^T | -R^T t]
    float t[16] = {};
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) t[r*4 + c] = m[c*4 + r];
    for (int r = 0; r < 3; r++) t[r*4 + 3] = -(t[r*4]*m[3] + t[r*4+1]*m[7] + t[r*4+2]*m[11]);
    t[15] = 1;
    memcpy(out, t, sizeof t);
}
static void QuatToMat3(const float* q, float R[3][3]) {      // q = x,y,z,w
    float x = q[0], y = q[1], z = q[2], w = q[3];
    R[0][0] = 1 - 2*(y*y + z*z); R[0][1] = 2*(x*y - z*w);     R[0][2] = 2*(x*z + y*w);
    R[1][0] = 2*(x*y + z*w);     R[1][1] = 1 - 2*(x*x + z*z); R[1][2] = 2*(y*z - x*w);
    R[2][0] = 2*(x*z - y*w);     R[2][1] = 2*(y*z + x*w);     R[2][2] = 1 - 2*(x*x + y*y);
}

// Force the main camera's projection to a fixed vertical FOV (keeps the game's aspect).
static void ApplyFovLock(float* f, float vfovDeg) {
    float p11 = 1.0f / tanf(vfovDeg / 57.29578f * 0.5f);
    float ratio = (fabsf(f[kProj + 5]) > 1e-6f) ? f[kProj + 0] / f[kProj + 5] : 9.0f / 16.0f;
    f[kProj + 0] = p11 * ratio; f[kProj + 5] = p11;
    f[kProjInv + 0] = 1.0f / f[kProj + 0]; f[kProjInv + 5] = 1.0f / p11;
    Mul44(f + kProj, f + kView, f + kViewProj);
    Mul44(f + kViewInv, f + kProjInv, f + kViewProjInv);
}

static void ApplyFovScale(float* f, float s) {
    // symmetric projection: only the x/y scale terms change (and their inverses)
    f[kProj + 0] /= s; f[kProj + 5] /= s;
    f[kProjInv + 0] *= s; f[kProjInv + 5] *= s;
    Mul44(f + kProj, f + kView, f + kViewProj);
    Mul44(f + kViewInv, f + kProjInv, f + kViewProjInv);
}

// Remove the game camera's pitch and roll, keeping its position and heading.
static void LevelView(float* V) {
    float right[3] = { V[0], V[1], V[2] }, up[3] = { V[4], V[5], V[6] }, back[3] = { V[8], V[9], V[10] };
    float t[3] = { V[3], V[7], V[11] };
    float cam[3];   // camera position in world = -R^T t
    for (int i = 0; i < 3; i++) cam[i] = -(right[i]*t[0] + up[i]*t[1] + back[i]*t[2]);
    // world up = the world axis the camera's up vector points along most
    int k = 0; for (int i = 1; i < 3; i++) if (fabsf(up[i]) > fabsf(up[k])) k = i;
    float U[3] = { 0, 0, 0 }; U[k] = up[k] > 0 ? 1.0f : -1.0f;
    float fw[3] = { -back[0], -back[1], -back[2] };
    float d = fw[0]*U[0] + fw[1]*U[1] + fw[2]*U[2];
    for (int i = 0; i < 3; i++) fw[i] -= d * U[i];
    float len = sqrtf(fw[0]*fw[0] + fw[1]*fw[1] + fw[2]*fw[2]);
    if (len < 1e-3f) return;                                   // looking straight up/down: leave it
    for (int i = 0; i < 3; i++) fw[i] /= len;
    float R0[3] = { fw[1]*U[2] - fw[2]*U[1], fw[2]*U[0] - fw[0]*U[2], fw[0]*U[1] - fw[1]*U[0] };   // right = fw x up
    // keep the original basis handedness (if the game's view were mirrored, mirror ours too)
    float det = right[0]*(up[1]*back[2] - up[2]*back[1]) - right[1]*(up[0]*back[2] - up[2]*back[0]) + right[2]*(up[0]*back[1] - up[1]*back[0]);
    if (det < 0) for (int i = 0; i < 3; i++) R0[i] = -R0[i];
    float rows[3][3] = { { R0[0], R0[1], R0[2] }, { U[0], U[1], U[2] }, { -fw[0], -fw[1], -fw[2] } };
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) V[r*4 + c] = rows[r][c];
        V[r*4 + 3] = -(rows[r][0]*cam[0] + rows[r][1]*cam[1] + rows[r][2]*cam[2]);
    }
}

// Camera = game camera (optionally levelled) seen from the eye's pose relative to the
// recenter reference. OpenXR and the game's view space share axes (x right, y up, -z forward).
// Minv = inverse of the eye's pose relative to the recenter reference (scaled by WorldScale).
static void EyeMinv(const xr::Pose& ps, float* Minv) {
    // eye pose relative to the reference: q_rel = conj(refQ) * q, p_rel = R(refQ)^T (p - refP)
    float Rref[3][3], Reye[3][3];
    QuatToMat3(ps.refQ, Rref);
    QuatToMat3(ps.q, Reye);
    float Rrel[3][3], dp[3] = { ps.p[0] - ps.refP[0], ps.p[1] - ps.refP[1], ps.p[2] - ps.refP[2] }, prel[3];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) Rrel[r][c] = Rref[0][r]*Reye[0][c] + Rref[1][r]*Reye[1][c] + Rref[2][r]*Reye[2][c];
        prel[r] = (Rref[0][r]*dp[0] + Rref[1][r]*dp[1] + Rref[2][r]*dp[2]) * g_cfg.worldScale;
    }
    // inverse of [Rrel | prel]
    memset(Minv, 0, 64);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) Minv[r*4 + c] = Rrel[c][r];
        Minv[r*4 + 3] = -(Rrel[0][r]*prel[0] + Rrel[1][r]*prel[1] + Rrel[2][r]*prel[2]);
    }
    Minv[15] = 1;
}

static void ApplyHeadPose(float* f, const xr::Pose& ps) {
    float V[16]; memcpy(V, f + kView, sizeof V);
    if (g_cfg.levelHorizon) LevelView(V);
    float Minv[16];
    EyeMinv(ps, Minv);
    Mul44(Minv, V, f + kView);
    RigidInverse(f + kView, f + kViewInv);
    Mul44(f + kProj, f + kView, f + kViewProj);
    Mul44(f + kViewInv, f + kProjInv, f + kViewProjInv);
}

static bool FovOverrideActive();

// ---- copies of the camera matrix in other constant buffers ----
// Some shaders (e.g. world-anchored UI: item markers, speech indicators, nearby waypoints)
// position things with their own copy of the game's view or view-projection matrix instead
// of cb_scene. Those copies still hold the mono, non-head-tracked camera, so the elements
// drift when you move your head. Every constant-buffer upload is scanned for copies of the
// mono matrices (as stored, or transposed) and the same correction cb_scene got is applied:
//   VP' = K VP with K = VP_eye * VP_mono^-1,   V' = KV V with KV = V_eye * V_mono^-1
struct MatRef { float vp[16], vpT[16], v[16], vT[16], K[16], KV[16]; bool valid; LONG frame; };
static MatRef g_mref;
// For view-projection copies written BEFORE cb_scene in a frame (so they can't be matched against
// this frame's camera), recognise them by structure instead: M = P_game * V with V a rigid
// camera transform. Their correction only depends on the head pose: M' = P_eye * Minv * P_game^-1 * M.
static float g_Pgame[16], g_PgameInv[16], g_Peye[16];
static bool  g_haveProj = false;
static bool FrameEyeMinv(float* Minv);   // defined after the config helpers below
static void Transpose44(const float* a, float* o) { for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) o[c*4 + r] = a[r*4 + c]; }
static void MatRefUpdate(bool changed, const float* V, const float* VP, const float* Vi, const float* VPi, const float* Veye, const float* VPeye) {
    g_mref.valid = changed;
    if (!changed) return;
    memcpy(g_mref.v, V, 64); memcpy(g_mref.vp, VP, 64);
    Transpose44(V, g_mref.vT); Transpose44(VP, g_mref.vpT);
    Mul44(VPeye, VPi, g_mref.K);
    Mul44(Veye, Vi, g_mref.KV);
    g_mref.frame = g_frame;
}
static inline bool Same16(const float* a, const float* b) {
    for (int i = 0; i < 16; i++) { float d = fabsf(a[i] - b[i]); if (d > 1e-3f * (1.0f + fabsf(b[i]))) return false; }
    return true;
}
struct CopyStat { UINT bytes, off; int kind; int count; bool sameFrame; };
static std::vector<CopyStat> g_copyStats;
static bool g_copyFix = true;
static bool g_structuralCopies = false;   // step 4g experiment: matched unrelated matrix arrays -> flicker
static const char* kCopyKind[4] = { "viewProj", "viewProj (transposed)", "view", "view (transposed)" };

// Is m = P_game * V with V a proper camera transform (rotation rows unit + orthogonal, det > 0,
// last row 0 0 0 1, not identity)?
[[maybe_unused]] static bool IsCameraVP(const float* m) {
    if (!g_haveProj) return false;
    // quick reject: with P row3 = (0,0,-1,0), row3 of VP is -(V row2): xyz must be unit length
    float l = m[12]*m[12] + m[13]*m[13] + m[14]*m[14];
    if (fabsf(l - 1.0f) > 0.01f) return false;
    float V[16]; Mul44(g_PgameInv, m, V);
    for (int r = 0; r < 3; r++) {
        float n = V[r*4]*V[r*4] + V[r*4+1]*V[r*4+1] + V[r*4+2]*V[r*4+2];
        if (fabsf(n - 1.0f) > 0.01f) return false;
    }
    float d01 = V[0]*V[4] + V[1]*V[5] + V[2]*V[6], d02 = V[0]*V[8] + V[1]*V[9] + V[2]*V[10], d12 = V[4]*V[8] + V[5]*V[9] + V[6]*V[10];
    if (fabsf(d01) > 0.01f || fabsf(d02) > 0.01f || fabsf(d12) > 0.01f) return false;
    if (fabsf(V[12]) > 1e-3f || fabsf(V[13]) > 1e-3f || fabsf(V[14]) > 1e-3f || fabsf(V[15] - 1.0f) > 1e-3f) return false;
    float det = V[0]*(V[5]*V[10] - V[6]*V[9]) - V[1]*(V[4]*V[10] - V[6]*V[8]) + V[2]*(V[4]*V[9] - V[5]*V[8]);
    if (det < 0.5f) return false;
    bool identity = fabsf(V[0] - 1) < 1e-4f && fabsf(V[5] - 1) < 1e-4f && fabsf(V[10] - 1) < 1e-4f &&
                    fabsf(V[3]) < 1e-4f && fabsf(V[7]) < 1e-4f && fabsf(V[11]) < 1e-4f;
    return !identity;                       // a bare projection matrix is not a camera copy
}

// Returns true if anything was patched.
static bool PatchMatrixCopies(float* data, UINT bytes) {
    if (!g_copyFix || bytes < 64) return false;
    float Kframe[16]; bool haveK = false;
    if (g_structuralCopies && !g_cfg.levelHorizon && g_haveProj) {
        float Minv[16];
        if (FrameEyeMinv(Minv)) { float t[16]; Mul44(g_Peye, Minv, t); Mul44(t, g_PgameInv, Kframe); haveK = true; }
    }
    if (!haveK && !g_mref.valid) return false;
    bool any = false;
    for (UINT o = 0; o + 64 <= bytes; o += 16) {
        float* m = data + o / 4;
        int kind = -1;
        if (haveK) {   // structural: works no matter when in the frame the copy is uploaded
            float t[16], r[16];
            if (IsCameraVP(m)) { Mul44(Kframe, m, r); memcpy(m, r, 64); kind = 0; }
            else { Transpose44(m, t); if (IsCameraVP(t)) { Mul44(Kframe, t, r); Transpose44(r, m); kind = 1; } }
            if (kind >= 0) goto patched;
        }
        if (!g_mref.valid) continue;
        if      (fabsf(m[0] - g_mref.vp[0])  <= 1e-3f * (1 + fabsf(g_mref.vp[0]))  && Same16(m, g_mref.vp))  kind = 0;
        else if (fabsf(m[0] - g_mref.vpT[0]) <= 1e-3f * (1 + fabsf(g_mref.vpT[0])) && Same16(m, g_mref.vpT)) kind = 1;
        else if (fabsf(m[0] - g_mref.v[0])   <= 1e-3f * (1 + fabsf(g_mref.v[0]))   && Same16(m, g_mref.v))   kind = 2;
        else if (fabsf(m[0] - g_mref.vT[0])  <= 1e-3f * (1 + fabsf(g_mref.vT[0]))  && Same16(m, g_mref.vT))  kind = 3;
        if (kind < 0) continue;
        {
            float t[16], r[16];
            const float* K = (kind <= 1) ? g_mref.K : g_mref.KV;
            if (kind == 0 || kind == 2) { Mul44(K, m, r); memcpy(m, r, 64); }
            else { Transpose44(m, t); Mul44(K, t, r); Transpose44(r, m); }
        }
    patched:
        any = true;
        bool same = g_mref.frame == g_frame;
        CopyStat* cs = nullptr;
        for (auto& c : g_copyStats) if (c.bytes == bytes && c.off == o && c.kind == kind) { cs = &c; break; }
        if (!cs && g_copyStats.size() < 64) {
            g_copyStats.push_back({ bytes, o, kind, 0, same });
            cs = &g_copyStats.back();
            Log("camera copy found: %u-byte constant buffer, offset %u, %s - head tracking applied to it too", bytes, o, kCopyKind[kind]);
        }
        if (cs) cs->count++;
        o += 48;                                   // skip the rest of this matrix
    }
    return any;
}

// Returns true if the data was modified.
// ---- call-stack logging (engine investigation) ----
// Logs where in the game exe a call came from, as exe+RVA, so the render code can be found
// in a disassembly. Up to 6 distinct stacks per tag.
#include <set>
static void LogStack(const char* tag);
// Crash logger: fatal exceptions are written to the log (module+offset and call stack) before the
// game dies, so a crash report says where it happened. First-chance only for real faults.
static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != 0xC0000409 /*stack buffer overrun*/ && code != EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    static LONG n = 0;
    if (InterlockedIncrement(&n) > 5) return EXCEPTION_CONTINUE_SEARCH;
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE m = nullptr; char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m) && m) {
        GetModuleFileNameA(m, name, sizeof name);
    }
    const char* base = strrchr(name, '\\'); base = base ? base + 1 : name;
    Log("EXCEPTION 0x%08lX at %s+%llx (thread %lu, frame %ld)%s", code, base, (unsigned long long)((uintptr_t)addr - (uintptr_t)m),
        GetCurrentThreadId(), (long)g_frame,
        code == EXCEPTION_ACCESS_VIOLATION ? (ep->ExceptionRecord->ExceptionInformation[0] ? " writing" : " reading") : "");
    if (code == EXCEPTION_ACCESS_VIOLATION) Log("  address %p", (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    LogStack("exception");
    {   // The crashed thread's own stack (LogStack above only shows this handler): code addresses
        // found on it, nearest first. For a call through a null pointer, the first is the caller.
        CONTEXT* c = ep->ContextRecord;
        Log("  rip %p rsp %p rax %p rcx %p rdx %p r8 %p r9 %p", (void*)c->Rip, (void*)c->Rsp, (void*)c->Rax, (void*)c->Rcx, (void*)c->Rdx, (void*)c->R8, (void*)c->R9);
        std::string line; int found = 0;
        auto* sp = (uintptr_t*)c->Rsp;
        for (int i = 0; i < 512 && found < 16; i++) {
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(sp + i, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) break;
            uintptr_t v = sp[i];
            HMODULE mm = nullptr;
            if (v < 0x10000 || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &mm) || !mm) continue;
            if (!VirtualQuery((void*)v, &mi, sizeof mi) || !(mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) continue;
            char mn[MAX_PATH] = "?"; GetModuleFileNameA(mm, mn, MAX_PATH);
            const char* b = strrchr(mn, '\\'); b = b ? b + 1 : mn;
            char t[128]; snprintf(t, sizeof t, " [%d]%s+%llx", i, b, (unsigned long long)(v - (uintptr_t)mm));
            line += t; found++;
        }
        Log("  thread stack:%s", line.c_str());
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void InstallCrashLogger() { static bool done = false; if (!done) { done = true; AddVectoredExceptionHandler(1, CrashHandler); } }

static void LogStack(const char* tag) {
    static std::map<std::string, std::set<ULONG>> seen;
    void* frames[28]; ULONG hash = 0;
    USHORT n = CaptureStackBackTrace(1, 28, frames, &hash);
    auto& set = seen[tag];
    if (set.count(hash) || set.size() >= 6) return;
    set.insert(hash);
    uintptr_t exe = (uintptr_t)GetModuleHandleA(nullptr);
    std::string line;
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &m);
        char t[96];
        if ((uintptr_t)m == exe) snprintf(t, sizeof t, " exe+%llx", (unsigned long long)((uintptr_t)frames[i] - exe));
        else {
            char name[MAX_PATH] = "?"; if (m) GetModuleFileNameA(m, name, MAX_PATH);
            const char* b = strrchr(name, '\\'); b = b ? b + 1 : name;
            snprintf(t, sizeof t, " %s+%llx", b, (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)m));
        }
        line += t;
    }
    Log("STACK %s #%zu:%s", tag, set.size(), line.c_str());
}

// Game time as seen by the shaders (cb_scene), to check whether paired frames really show
// the same moment: logged for 16 frames on Ctrl+F10 (and once automatically).
struct TimeRec { LONG frame; int eye; float gameTime, sceneTime; };
static TimeRec g_timeRec[16]; static int g_timeRecN = -1;
static int CurrentEye();
static void RecordGameTime(float gt, float st) {
    if (g_timeRecN < 0 || g_timeRecN >= 16) return;
    if (g_timeRecN > 0 && g_timeRec[g_timeRecN - 1].frame == g_frame) return;   // once per frame
    g_timeRec[g_timeRecN++] = { g_frame, CurrentEye(), gt, st };
    if (g_timeRecN == 16) {
        Log("game time per frame (paired frames %s) - with pairing working, each L frame and the R after it match:", g_cfg.paired ? "ON" : "OFF");
        for (auto& r : g_timeRec) Log("   frame %ld %s  gameTime %.5f  sceneTime %.5f", r.frame, r.eye < 0 ? "L" : r.eye > 0 ? "R" : "-", r.gameTime, r.sceneTime);
        g_timeRecN = -1;
    }
}

// The game can upload cb_scene more than once per frame: in menus a second camera renders the
// equipment / character models shown on the menu panel. Only the MAIN camera must get the eye
// offset and head pose; the others stay mono (otherwise the models flicker between eyes).
// The main buffer is learned during normal gameplay as the one cb_scene buffer that matches.
static void* g_mainSceneBuf = nullptr;
static void* g_sceneBufThisFrame[4]; static int g_sceneBufCount = 0;
static void* g_mainCand = nullptr; static int g_mainCandFrames = 0;
static LONG  g_lastSecondaryFrame = -100000;
static void SceneFrameEnd() {   // called once per frame
    if (g_sceneBufCount == 1) {
        if (g_sceneBufThisFrame[0] == g_mainCand) g_mainCandFrames++; else { g_mainCand = g_sceneBufThisFrame[0]; g_mainCandFrames = 1; }
        if (g_mainCandFrames == 120 && g_mainSceneBuf != g_mainCand) { g_mainSceneBuf = g_mainCand; Log("main camera buffer learned (%p); other camera buffers (menu models) stay mono", g_mainSceneBuf); }
    }
    g_sceneBufCount = 0;
}

static bool ProcessSceneBuffer(float* f, UINT bytes, void* res) {
    if (bytes < kSceneSize) return false;
    bool persp = false;
    if (!LooksLikeScene(f, &persp)) {
        if (InterlockedIncrement(&g_sceneRejects) == 50) {
            Log("a %u-byte constant buffer that is NOT cb_scene (first 24 floats, for reference):", bytes);
            for (int r = 0; r < 6; r++) Log("   %10.4f %10.4f %10.4f %10.4f", f[r*4], f[r*4+1], f[r*4+2], f[r*4+3]);
        }
        return false;
    }
    if (!persp) return false;                               // leave shadow / ortho cameras alone
    {
        bool seen = false;
        for (int i = 0; i < g_sceneBufCount; i++) if (g_sceneBufThisFrame[i] == res) seen = true;
        if (!seen && g_sceneBufCount < 4) g_sceneBufThisFrame[g_sceneBufCount++] = res;
    }
    if (g_mainSceneBuf && res && res != g_mainSceneBuf) {     // a secondary camera (menus: models, maybe the backdrop)
        static bool logged = false;
        if (!logged) { logged = true; Log("secondary camera buffer %p in use (menus) - %s", res,
                                          g_cfg.stereoAllCameras ? "stereo + head tracking (StereoAllCameras=1)" : "left mono"); }
        g_lastSecondaryFrame = g_frame;
        LogStack("cb_scene_secondary");
        if (!g_cfg.stereoAllCameras) {
            // menu models: eye separation only (no head rotation - that made them follow the head in 8e).
            // Each eye's panel is kept separately, so this gives them real depth on the panel.
            int e2 = CurrentEye(), pe = g_cfg.swapEyes ? -e2 : e2;
            if (g_cfg.menuModelStereo && e2) {
                float o = -pe * 0.5f * g_cfg.menuModelSep;
                ApplyEyeOffset(f, o);
                ApplyConvergence(f, o, g_cfg.menuModelConv);
                return true;
            }
            return false;
        }
        // Step 8e: with dual render the old reason for keeping these mono (flicker from alternating
        // eyes) is gone, and a mono menu backdrop is flat and head-locked. Eye pose only: no FOV lock
        // (menu model cameras use their own narrow FOV) and no update of the main camera's globals.
        int eye = CurrentEye(), poseEye = g_cfg.swapEyes ? -eye : eye;
        xr::Pose pose;
        if (g_cfg.headTracking && xr::GetRenderEye(poseEye, &pose)) { ApplyHeadPose(f, pose); return true; }
        if (eye) { ApplyEyeOffset(f, -poseEye * 0.5f * g_cfg.separation); return true; }
        return false;
    }
    InterlockedIncrement(&g_sceneMatches);
    if (!g_loggedFirstScene) {
        g_loggedFirstScene = true;
        const float* p = f + kProj;
        Log("cb_scene found (%u bytes). proj row0=(%.4f %.4f %.4f %.4f) row1=(%.4f %.4f %.4f %.4f)",
            bytes, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        Log("  proj row2=(%.6f %.6f %.6f %.6f) row3=(%.4f %.4f %.4f %.4f)",
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        Log("  view row0=(%.3f %.3f %.3f %.3f) row3=(%.3f %.3f %.3f %.3f)",
            f[0], f[1], f[2], f[3], f[12], f[13], f[14], f[15]);
    }
    int eye = CurrentEye();
    int poseEye = g_cfg.swapEyes ? -eye : eye;              // see xr.cpp: image slot follows the pose
    bool changed = false;
    g_gameP00 = f[kProj + 0]; g_gameP11 = f[kProj + 5];
    LogStack(g_mainSceneBuf ? "cb_scene_main" : "cb_scene_unlearned");
    memcpy(g_Pgame, f + kProj, 64); memcpy(g_PgameInv, f + kProjInv, 64);
    RecordGameTime(f[26*4 + 3], f[28*4 + 3]);   // gameTime_g (c26.w), sceneTime_g (c28.w)
    float monoV[16], monoVP[16], monoVi[16], monoVPi[16];
    memcpy(monoV, f + kView, 64); memcpy(monoVP, f + kViewProj, 64); memcpy(monoVi, f + kViewInv, 64); memcpy(monoVPi, f + kViewProjInv, 64);
    memcpy(g_monoViewInv, monoVi, 64); memcpy(g_monoView, monoV, 64); g_haveMonoView = true;     // controller physics: view space <-> game world
    if (g_cfg.fovLock) { ApplyFovLock(f, g_cfg.gameFov); changed = true; }
    else if (g_cfg.fovScale != 1.0f && !FovOverrideActive()) { ApplyFovScale(f, g_cfg.fovScale); changed = true; }
    xr::Pose pose;
    if (g_cfg.headTracking && xr::GetRenderEye(poseEye, &pose)) {
        ApplyHeadPose(f, pose);
        changed = true;
    } else if (eye) {
        ApplyEyeOffset(f, -poseEye * 0.5f * g_cfg.separation);   // left eye (-1) -> +half
        changed = true;
    }
    if (fabsf(f[kProj + 0]) > 1e-3f && fabsf(f[kProj + 5]) > 1e-3f) { g_tanH = 1.0f / fabsf(f[kProj + 0]); g_tanV = 1.0f / fabsf(f[kProj + 5]); }
    memcpy(g_Peye, f + kProj, 64); g_haveProj = true;
    MatRefUpdate(changed, monoV, monoVP, monoVi, monoVPi, f + kView, f + kViewProj);
    return changed;
}

// The eye transform the current frame is rendered with (same as ProcessSceneBuffer uses).
static bool FrameEyeMinv(float* Minv) {
    int eye = CurrentEye();
    int poseEye = g_cfg.swapEyes ? -eye : eye;
    xr::Pose pose;
    if (g_cfg.headTracking && xr::GetRenderEye(poseEye, &pose)) { EyeMinv(pose, Minv); return true; }
    if (!eye) return false;
    float o = -poseEye * 0.5f * g_cfg.separation;             // view-space x offset, as ApplyEyeOffset
    memset(Minv, 0, 64); Minv[0] = Minv[5] = Minv[10] = Minv[15] = 1; Minv[3] = o;
    return true;
}

// ----------------------------------------------------------------------------
// Diagnostics: per-frame inventory of how constant buffers are updated / bound.
// Captures are triggered by F9 and automatically at a few frame numbers.
// ----------------------------------------------------------------------------
#include <map>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

struct BufStat {
    UINT width = 0, bind = 0, usage = 0, cpu = 0;
    int mapDiscard = 0, mapNoOverwrite = 0, mapOther = 0, upd = 0, updBox = 0, upd1 = 0, copyDst = 0;
    int vs0 = 0, vsAny = 0, psAny = 0, csAny = 0;
    std::vector<std::pair<UINT, UINT>> vs0Ranges;   // VSSetConstantBuffers1 slot-0 (first,num)
};
static std::mutex g_invMx;
static std::map<void*, BufStat> g_inv;
static volatile LONG g_capture = 0;                  // frames left to capture
static bool g_dumped2800 = false;
static int g_capExec = 0, g_capCreateCB = 0, g_capDeferred = 0;

static BufStat* Stat(ID3D11Resource* r) {         // caller holds g_invMx
    auto it = g_inv.find(r);
    if (it != g_inv.end()) return &it->second;
    BufStat st;
    D3D11_RESOURCE_DIMENSION dim; r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3D11_BUFFER_DESC d; static_cast<ID3D11Buffer*>(r)->GetDesc(&d);
        st.width = d.ByteWidth; st.bind = d.BindFlags; st.usage = d.Usage; st.cpu = d.CPUAccessFlags;
    }
    return &(g_inv[r] = st);
}
#define INV(stmt) do { if (g_capture) { std::lock_guard<std::mutex> lk(g_invMx); stmt; } } while (0)

static void DumpInventory(LONG frame) {
    std::lock_guard<std::mutex> lk(g_invMx);
    std::vector<std::pair<void*, BufStat>> v(g_inv.begin(), g_inv.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.width > b.second.width; });
    Log("==== CB INVENTORY frame %ld: %zu resources, ExecuteCommandList=%d, CB creations=%d, deferred ctx created(total)=%d ====",
        frame, v.size(), g_capExec, g_capCreateCB, g_capDeferred);
    int n = 0;
    for (auto& e : v) {
        const BufStat& b = e.second;
        if (!(b.bind & D3D11_BIND_CONSTANT_BUFFER) && !b.vsAny && !b.psAny && !b.csAny) continue;
        if (++n > 60) { Log("  ..."); break; }
        char ranges[160] = "";
        for (size_t i = 0; i < b.vs0Ranges.size() && i < 6; i++) {
            char t[32]; snprintf(t, sizeof t, " %u+%u", b.vs0Ranges[i].first, b.vs0Ranges[i].second); strncat(ranges, t, sizeof ranges - strlen(ranges) - 1);
        }
        Log("  %p w=%u bind=0x%x usage=%u cpu=0x%x | mapD=%d mapNO=%d mapO=%d upd=%d updBox=%d upd1=%d copyDst=%d | VS0=%d VS=%d PS=%d CS=%d%s%s",
            e.first, b.width, b.bind, b.usage, b.cpu, b.mapDiscard, b.mapNoOverwrite, b.mapOther, b.upd, b.updBox, b.upd1,
            b.copyDst, b.vs0, b.vsAny, b.psAny, b.csAny, ranges[0] ? " | VS0 ranges(first+num):" : "", ranges);
    }
    g_inv.clear(); g_capExec = 0; g_capCreateCB = 0;
}

// ----------------------------------------------------------------------------
// ID3D11DeviceContext hooks. Originals are stored per vtable so immediate and
// deferred contexts (which may be different classes) each call their own.
// ----------------------------------------------------------------------------
using PFN_Map = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using PFN_Unmap = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using PFN_UpdateSubresource = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
using PFN_UpdateSubresource1 = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT, UINT);
using PFN_SetCB = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);
using PFN_SetCB1 = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
using PFN_CopyResource = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
using PFN_CopySubresourceRegion = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*);
using PFN_ExecuteCommandList = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
using PFN_CreateDeferredContext = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, UINT, ID3D11DeviceContext**);
using PFN_CreateBuffer = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**);

enum { I_VSSetCB = 7, I_Map = 14, I_Unmap = 15, I_PSSetCB = 16, I_CopySubresourceRegion = 46, I_CopyResource = 47,
       I_UpdateSubresource = 48, I_ExecuteCommandList = 58, I_CSSetCB = 71,
       I_UpdateSubresource1 = 116, I_VSSetCB1 = 119, I_PSSetCB1 = 123, I_CSSetCB1 = 124, I_Count = 125,
       I_PSSetShader = 9, I_VSSetShader = 11, I_DrawIndexed = 12, I_Draw = 13, I_DrawIndexedInstanced = 20, I_DrawInstanced = 21,
       I_OMSetRenderTargets = 33, I_OMSetRTUAV = 34, I_RSSetViewports = 44, I_DrawAuto = 38, I_DrawIndexedInstancedIndirect = 39,
       I_DrawInstancedIndirect = 40, I_Dispatch = 41, I_DispatchIndirect = 42 };
// always-on call counters, reported every 600 frames
enum { C_Draw, C_DrawIndexed, C_DrawIndexedInstanced, C_DrawInstanced, C_DrawAuto, C_DIII, C_DII, C_Dispatch, C_DispatchIndirect,
       C_VSSetShader, C_OMSetRT, C_OMSetRTUAV, C_Count };
static volatile LONG g_calls[C_Count];
static const char* kCallNames[C_Count] = { "Draw", "DrawIndexed", "DrawIndexedInstanced", "DrawInstanced", "DrawAuto",
    "DrawIndexedInstancedIndirect", "DrawInstancedIndirect", "Dispatch", "DispatchIndirect", "VSSetShader", "OMSetRenderTargets", "OMSetRT+UAV" };

struct CtxVt { void** vt = nullptr; void* orig[I_Count] = {}; bool isCtx1 = false; };
static CtxVt g_ctxVt[4];
static std::mutex g_ctxVtMx;

static CtxVt& VtOf(void* obj) {
    void** vt = *(void***)obj;
    for (auto& c : g_ctxVt) if (c.vt == vt) return c;
    return g_ctxVt[0];   // should not happen
}
#define ORIG(ctx, idx, T) ((T)VtOf(ctx).orig[idx])

static bool OutlineIsSource(ID3D11Resource* r);
static void OutlineCapture(const void* data, UINT size);
// Pending maps of candidate constant buffers.
struct PendingMap { ID3D11Resource* res; void* data; UINT size; DWORD thread; };
static PendingMap g_pending[64];
static std::mutex g_pendingMx;

[[maybe_unused]] static UINT CandidateCBSize(ID3D11Resource* res) {
    D3D11_RESOURCE_DIMENSION dim;
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return 0;
    D3D11_BUFFER_DESC d;
    static_cast<ID3D11Buffer*>(res)->GetDesc(&d);
    if (!(d.BindFlags & D3D11_BIND_CONSTANT_BUFFER) || d.ByteWidth < kSceneSize || d.ByteWidth > 4096) return 0;
    return d.ByteWidth;
}

static UINT AnyCBSize(ID3D11Resource* res) {      // any constant buffer up to 64 KB
    D3D11_RESOURCE_DIMENSION dim;
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return 0;
    D3D11_BUFFER_DESC d;
    static_cast<ID3D11Buffer*>(res)->GetDesc(&d);
    if (!(d.BindFlags & D3D11_BIND_CONSTANT_BUFFER) || d.ByteWidth < 64 || d.ByteWidth > 65536) return 0;
    return d.ByteWidth;
}

static HRESULT STDMETHODCALLTYPE Hook_Map(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub,
                                          D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* out) {
    HRESULT hr = ORIG(ctx, I_Map, PFN_Map)(ctx, res, sub, type, flags, out);
    INV({ BufStat* b = Stat(res); if (type == D3D11_MAP_WRITE_DISCARD) b->mapDiscard++; else if (type == D3D11_MAP_WRITE_NO_OVERWRITE) b->mapNoOverwrite++; else b->mapOther++; });
    if (SUCCEEDED(hr) && out && out->pData && (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE)) {
        if (OutlineIsSource(res)) {
            std::lock_guard<std::mutex> lk(g_pendingMx);
            for (auto& p : g_pending) if (!p.res) { p = { res, out->pData, 0xFFFFFFFFu, GetCurrentThreadId() }; break; }
        } else if (UINT sz = AnyCBSize(res)) {
            std::lock_guard<std::mutex> lk(g_pendingMx);
            for (auto& p : g_pending)
                if (!p.res) { p = { res, out->pData, sz, GetCurrentThreadId() }; break; }
        }
    }
    return hr;
}

static void STDMETHODCALLTYPE Hook_Unmap(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub) {
    void* data = nullptr; UINT size = 0;
    {
        std::lock_guard<std::mutex> lk(g_pendingMx);
        for (auto& p : g_pending)
            if (p.res == res && p.thread == GetCurrentThreadId()) { data = p.data; size = p.size; p.res = nullptr; break; }
    }
    if (data && size == 0xFFFFFFFFu) { OutlineCapture(data, 0); data = nullptr; }
    if (data) {
        if (size == 2800 && g_capture && !g_dumped2800) {
            g_dumped2800 = true;
            const float* f = (const float*)data;
            Log("---- 2800-byte constant buffer, full dump (register: x y z w) ----");
            for (int r = 0; r < 175; r++) Log("  c%-3d %12.5f %12.5f %12.5f %12.5f", r, f[r*4], f[r*4+1], f[r*4+2], f[r*4+3]);
        }
        static thread_local uint8_t mono[65536];
        bool rec = g_dualActive && !g_inRender && size <= sizeof mono;
        if (rec) memcpy(mono, data, size);
        bool scene = size >= kSceneSize && size <= 4096 && ProcessSceneBuffer((float*)data, size, res);
        bool patched = scene || PatchMatrixCopies((float*)data, size);
        if (rec && patched) RecordPrep(res, mono, size, true);
    }
    ORIG(ctx, I_Unmap, PFN_Unmap)(ctx, res, sub);
}

// ---- dual render: prepare-phase uploads are replayed per eye ----
// cb_scene and the camera copies are uploaded BEFORE the game's render call. For the second
// eye they are re-uploaded from their unmodified (mono) contents with the right eye applied.
struct PrepUpload { ID3D11Resource* res; std::vector<uint8_t> mono; bool dynamic; };
static std::vector<PrepUpload> g_prep;
static void RecordPrep(ID3D11Resource* res, const void* mono, UINT size, bool dynamic) {
    for (auto& u : g_prep) if (u.res == res) { u.mono.assign((const uint8_t*)mono, (const uint8_t*)mono + size); u.dynamic = dynamic; return; }
    if (g_prep.size() < 64) g_prep.push_back({ res, std::vector<uint8_t>((const uint8_t*)mono, (const uint8_t*)mono + size), dynamic });
}

// Handles both UpdateSubresource and UpdateSubresource1. Returns replacement data or nullptr.
static const void* MaybeProcessUpdate(ID3D11Resource* res, const D3D11_BOX* box, const void* src, float* tmp) {
    if (!src) return nullptr;
    UINT sz = AnyCBSize(res);
    if (!sz) return nullptr;
    UINT len = sz;
    if (box) { if (box->left != 0) return nullptr; len = box->right; }
    if (len > 65536 || len < 64) return nullptr;
    memcpy(tmp, src, len);
    bool done = (len >= kSceneSize && len <= 4096 && ProcessSceneBuffer(tmp, len, res)) || PatchMatrixCopies(tmp, len);
    if (done && g_dualActive && !g_inRender && !box) RecordPrep(res, src, len, false);
    return done ? tmp : nullptr;
}

static void STDMETHODCALLTYPE Hook_UpdateSubresource(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub,
                                                     const D3D11_BOX* box, const void* src, UINT rp, UINT dp) {
    INV({ BufStat* b = Stat(res); if (box) b->updBox++; else b->upd++; });
    if (src && !box && OutlineIsSource(res)) OutlineCapture(src, 0);
    static thread_local float tmp[65536 / 4];
    const void* repl = MaybeProcessUpdate(res, box, src, tmp);
    ORIG(ctx, I_UpdateSubresource, PFN_UpdateSubresource)(ctx, res, sub, box, repl ? repl : src, rp, dp);
}

static void STDMETHODCALLTYPE Hook_UpdateSubresource1(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub,
                                                      const D3D11_BOX* box, const void* src, UINT rp, UINT dp, UINT fl) {
    INV(Stat(res)->upd1++);
    static thread_local float tmp[65536 / 4];
    const void* repl = MaybeProcessUpdate(res, box, src, tmp);
    ORIG(ctx, I_UpdateSubresource1, PFN_UpdateSubresource1)(ctx, res, sub, box, repl ? repl : src, rp, dp, fl);
}

static void RecordBind(int stage, UINT slot, UINT n, ID3D11Buffer* const* bufs, const UINT* first, const UINT* num) {
    if (!g_capture || !bufs) return;
    std::lock_guard<std::mutex> lk(g_invMx);
    for (UINT i = 0; i < n; i++) {
        if (!bufs[i]) continue;
        BufStat* b = Stat(bufs[i]);
        if (stage == 0) { b->vsAny++; if (slot + i == 0) { b->vs0++;
            if (first && num && b->vs0Ranges.size() < 6) b->vs0Ranges.push_back({ first[i], num[i] }); } }
        else if (stage == 1) b->psAny++; else b->csAny++;
    }
}
static void STDMETHODCALLTYPE Hook_VSSetCB(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b) { RecordBind(0, s, n, b, nullptr, nullptr); ORIG(c, I_VSSetCB, PFN_SetCB)(c, s, n, b); }
static void OutlineAfterPSCB(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b);
static void STDMETHODCALLTYPE Hook_PSSetCB(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b) { RecordBind(1, s, n, b, nullptr, nullptr); ORIG(c, I_PSSetCB, PFN_SetCB)(c, s, n, b); OutlineAfterPSCB(c, s, n, b); }
static void STDMETHODCALLTYPE Hook_CSSetCB(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b) { RecordBind(2, s, n, b, nullptr, nullptr); ORIG(c, I_CSSetCB, PFN_SetCB)(c, s, n, b); }
static void STDMETHODCALLTYPE Hook_VSSetCB1(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* m) { RecordBind(0, s, n, b, f, m); ORIG(c, I_VSSetCB1, PFN_SetCB1)(c, s, n, b, f, m); }
static void STDMETHODCALLTYPE Hook_PSSetCB1(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* m) { RecordBind(1, s, n, b, f, m); ORIG(c, I_PSSetCB1, PFN_SetCB1)(c, s, n, b, f, m); OutlineAfterPSCB(c, s, n, b); }
static void STDMETHODCALLTYPE Hook_CSSetCB1(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* m) { RecordBind(2, s, n, b, f, m); ORIG(c, I_CSSetCB1, PFN_SetCB1)(c, s, n, b, f, m); }

static void STDMETHODCALLTYPE Hook_CopyResource(ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src) {
    INV(Stat(dst)->copyDst++);
    ORIG(c, I_CopyResource, PFN_CopyResource)(c, dst, src);
}
static void STDMETHODCALLTYPE Hook_CopySubresourceRegion(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT ds, UINT x, UINT y, UINT z,
                                                         ID3D11Resource* src, UINT ss, const D3D11_BOX* box) {
    INV(Stat(dst)->copyDst++);
    ORIG(c, I_CopySubresourceRegion, PFN_CopySubresourceRegion)(c, dst, ds, x, y, z, src, ss, box);
}
static void STDMETHODCALLTYPE Hook_ExecuteCommandList(ID3D11DeviceContext* c, ID3D11CommandList* cl, BOOL restore) {
    INV(g_capExec++);
    ORIG(c, I_ExecuteCommandList, PFN_ExecuteCommandList)(c, cl, restore);
}

// ---- shader hashes (3DMigoto-compatible) and per-frame render-target sequence ----
// 3DMigoto names shaders by a 64-bit FNV hash of the bytecode. The exact variant is
// verified at runtime against the HUD shaders listed in the geo-11 d3dx.ini.
static const uint64_t kHudVS[] = { 0x4976a4a4235db4a9ull, 0x48a3933aedab203full, 0xa2ae64f05aad23c7ull, 0x6c94f7c741fa3483ull,
                                   0x1a2f8acb40d86b2cull, 0x09758bd909c55e9aull, 0x8723394518bd1a07ull };
static uint64_t Fnv(const void* p, size_t n, int variant) {
    const unsigned char* b = (const unsigned char*)p;
    uint64_t h = variant == 0 ? 0 : 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        if (variant == 2) { h ^= b[i]; h *= 0x100000001b3ull; }        // FNV-1a
        else              { h *= 0x100000001b3ull; h ^= b[i]; }        // FNV-1 (variant 0: zero basis)
    }
    return h;
}
struct ShaderInfo { uint64_t h[3]; bool hud; };
static std::mutex g_shMx;
static std::map<void*, ShaderInfo> g_shaders;
static int g_hashVariant = -1;            // which FNV variant matched the HUD list
static int g_vsCreated = 0, g_hudVsFound = 0;
static std::map<uint64_t, std::vector<uint8_t>> g_vsCode;   // bytecode by hash (for disassembly dumps)

// ---- version-independent shader identity ----
// Game updates recompile the shaders (new hashes) and can change their data layout (1.07 added
// prevWorld to the instance record: 128 -> 192 bytes). The shaders SoraVR replaces or watches are
// recognised by their code instead and then carry their 1.06.1 hash as a canonical name, so every
// hash list (and the ini) keeps working. Vertex shaders: fingerprint of the disassembly with the
// numbers on structured-buffer lines blanked (tools/shaderprint.py computes it from a hunting dump).
// The outline pixel shader: by its declared resources (outlinePrepareTexture, cb_local.offsetsAndWeights).
struct KnownShader { uint64_t print, canon; const char* what; };
static const KnownShader kKnownVS[] = {
    { 0xd3c01e02979150b0ull, 0xf006f8946dc6d3c4ull, "world label shader 1" },
    { 0x889fb9ac5d71285bull, 0x6ab95c20c50948e5ull, "world label shader 2" },
    { 0x0836fae6a6063bb8ull, 0x4976a4a4235db4a9ull, "2D sprite shader" },
};
static const uint64_t kOutlinePSCanon = 0xe49978a78bd4be0eull;
typedef HRESULT (WINAPI* PFN_D3DReflectT)(LPCVOID, SIZE_T, REFIID, void**);
static HMODULE D3DCompilerDll() { static HMODULE m = LoadLibraryA("d3dcompiler_47.dll"); return m; }
static bool HasBytes(const void* code, size_t len, const char* s) {
    size_t n = strlen(s); const char* b = (const char*)code;
    for (size_t i = 0; i + n <= len; i++) if (b[i] == s[0] && !memcmp(b + i, s, n)) return true;
    return false;
}
static uint64_t ShaderPrint(const void* code, size_t len) {
    static pD3DDisassemble dis = D3DCompilerDll() ? (pD3DDisassemble)GetProcAddress(D3DCompilerDll(), "D3DDisassemble") : nullptr;
    ID3DBlob* blob = nullptr;
    if (!dis || FAILED(dis(code, len, 0, nullptr, &blob)) || !blob) return 0;
    const char* t = (const char*)blob->GetBufferPointer(); size_t tn = strnlen(t, blob->GetBufferSize());
    uint64_t h = 0xcbf29ce484222325ull; bool first = true;
    auto feed = [&](char c) { h ^= (uint8_t)c; h *= 0x100000001b3ull; };
    for (size_t i = 0; i < tn; ) {
        size_t e = i; while (e < tn && t[e] != '\n') e++;
        std::string line(t + i, e - i); i = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || !line.compare(a, 2, "//")) continue;
        bool numb = !line.compare(a, 13, "ld_structured") || !line.compare(a, 23, "dcl_resource_structured");
        if (!first) feed('\n');
        first = false;
        for (size_t k = 0; k < line.size(); k++) {
            if (numb && line[k] >= '0' && line[k] <= '9') { while (k + 1 < line.size() && line[k + 1] >= '0' && line[k + 1] <= '9') k++; feed('#'); }
            else feed(line[k]);
        }
    }
    blob->Release();
    return h;
}
static ID3D11ShaderReflection* Reflect(const void* code, size_t len) {
    static PFN_D3DReflectT refl = D3DCompilerDll() ? (PFN_D3DReflectT)GetProcAddress(D3DCompilerDll(), "D3DReflect") : nullptr;
    ID3D11ShaderReflection* r = nullptr;
    static const GUID kIID = { 0x8d536ca1, 0x0cca, 0x4956, { 0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84 } };   // IID_ID3D11ShaderReflection (d3dcompiler_47)
    if (!refl || FAILED(refl(code, len, kIID, (void**)&r))) return nullptr;
    return r;
}
static bool IsOutlinePSCode(const void* code, size_t len) {
    if (!HasBytes(code, len, "outlinePrepareTexture") || !HasBytes(code, len, "offsetsAndWeights")) return false;
    ID3D11ShaderReflection* r = Reflect(code, len);
    if (!r) return false;
    bool ok = false;
    D3D11_SHADER_INPUT_BIND_DESC bd;
    if (SUCCEEDED(r->GetResourceBindingDescByName("cb_local", &bd)) && bd.Type == D3D_SIT_CBUFFER && bd.BindPoint == 2) {
        D3D11_SHADER_VARIABLE_DESC vd;
        ID3D11ShaderReflectionConstantBuffer* cb = r->GetConstantBufferByName("cb_local");
        if (cb && SUCCEEDED(cb->GetVariableByName("offsetsAndWeights")->GetDesc(&vd)) && vd.StartOffset == 64 && vd.Size >= 128) ok = true;
    }
    r->Release();
    return ok;
}
// Bytes between the instance record's world matrix and its colour (0 in 1.06.1, 64 in 1.07), or -1
// if the record isn't laid out as the replacement shaders expect (then they're not used).
static int InstanceExtraBytes(const void* code, size_t len) {
    ID3D11ShaderReflection* r = Reflect(code, len);
    if (!r) return -1;
    int extra = -1;
    ID3D11ShaderReflectionConstantBuffer* cb = r->GetConstantBufferByName("instances_g");
    ID3D11ShaderReflectionType* el = cb ? cb->GetVariableByIndex(0)->GetType() : nullptr;
    D3D11_SHADER_TYPE_DESC w, c, u, p, b, q;
    if (el && SUCCEEDED(el->GetMemberTypeByName("world")->GetDesc(&w)) && SUCCEEDED(el->GetMemberTypeByName("color")->GetDesc(&c)) &&
        SUCCEEDED(el->GetMemberTypeByName("uv")->GetDesc(&u)) && SUCCEEDED(el->GetMemberTypeByName("param")->GetDesc(&p)) &&
        SUCCEEDED(el->GetMemberTypeByName("boneAddress")->GetDesc(&b)) && SUCCEEDED(el->GetMemberTypeByName("param2")->GetDesc(&q)) &&
        w.Offset == 0 && c.Offset >= 64 && (c.Offset - 64) % 16 == 0 &&
        u.Offset == c.Offset + 16 && p.Offset == c.Offset + 32 && b.Offset == c.Offset + 48 && q.Offset == c.Offset + 52)
        extra = (int)c.Offset - 64;
    r->Release();
    return extra;
}

static uint64_t RegisterShader(void* sh, const void* code, SIZE_T len, bool vertex) {
    ShaderInfo si{};
    for (int v = 0; v < 3; v++) si.h[v] = Fnv(code, len, v);
    {   uint64_t canon = 0; const char* what = nullptr;
        if (vertex && HasBytes(code, len, "instances_g")) {
            uint64_t p = ShaderPrint(code, len);
            for (const KnownShader& k : kKnownVS) if (p == k.print) { canon = k.canon; what = k.what; }
        } else if (!vertex && IsOutlinePSCode(code, len)) { canon = kOutlinePSCanon; what = "outline (deferred lighting) shader"; }
        if (canon) {
            if (si.h[0] != canon) Log("shader %016llx recognised by its code as the %s (%016llx in 1.06.1)", (unsigned long long)si.h[0], what, (unsigned long long)canon);
            for (int v = 0; v < 3; v++) si.h[v] = canon;
        }
    }
    si.hud = false;
    if (vertex) {
        for (int v = 0; v < 3; v++)
            for (uint64_t k : kHudVS)
                if (si.h[v] == k) {
                    si.hud = true;
                    if (g_hashVariant != v) { g_hashVariant = v; Log("HUD shader hash matched (FNV variant %d) -> %016llx", v, (unsigned long long)k); }
                }
    }
    std::lock_guard<std::mutex> lk(g_shMx);
    g_shaders[sh] = si;
    if (vertex) { g_vsCreated++; if (si.hud) g_hudVsFound++; g_vsCode[si.h[0]].assign((const uint8_t*)code, (const uint8_t*)code + len); }
    return si.h[0];
}
static uint64_t ShaderHash(void* sh) {
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_shaders.find(sh);
    if (it == g_shaders.end()) return 0;
    return it->second.h[g_hashVariant < 0 ? 0 : g_hashVariant];
}
static bool IsHudVS(void* sh) {
    std::lock_guard<std::mutex> lk(g_shMx);
    auto it = g_shaders.find(sh);
    return it != g_shaders.end() && it->second.hud;
}

struct Segment { void* rt; UINT w, h, fmt; bool bb; void* dsv; int draws, hudDraws; uint64_t vs[6]; int nvs; };
static std::vector<Segment> g_seq;
static void* g_curVS = nullptr;
static void* g_curRT = nullptr; static void* g_curDSV = nullptr; static UINT g_curW = 0, g_curH = 0, g_curFmt = 0;
static void* g_backbuffer = nullptr;      // raw pointer of the swapchain's buffer 0, for comparison only


static void SeqNewSegment() {
    if (!g_capture || g_seq.size() >= 400) return;
    g_seq.push_back(Segment{ g_curRT, g_curW, g_curH, g_curFmt, g_curRT && g_curRT == g_backbuffer, g_curDSV, 0, 0, {}, 0 });
}
static void SeqDraw() {
    if (!g_capture) return;
    if (g_seq.empty()) SeqNewSegment();
    if (g_seq.empty()) return;
    Segment& s = g_seq.back();
    s.draws++;
    if (IsHudVS(g_curVS)) s.hudDraws++;
    uint64_t h = ShaderHash(g_curVS);
    for (int i = 0; i < s.nvs; i++) if (s.vs[i] == h) return;
    if (s.nvs < 6) s.vs[s.nvs++] = h;
}
static void DumpSequence() {
    Log("==== FRAME SEQUENCE: %zu render-target segments (backbuffer=%p, VS created=%d, HUD VS recognised=%d, hash variant=%d) ====",
        g_seq.size(), g_backbuffer, g_vsCreated, g_hudVsFound, g_hashVariant);
    int i = 0;
    for (auto& s : g_seq) {
        char vs[160] = "";
        for (int k = 0; k < s.nvs; k++) { char t[24]; snprintf(t, sizeof t, " %016llx", (unsigned long long)s.vs[k]); strncat(vs, t, sizeof vs - strlen(vs) - 1); }
        Log("  #%-3d RT=%p %ux%u fmt=%u %s dsv=%s draws=%d HUD=%d  VS:%s", i++, s.rt, s.w, s.h, s.fmt,
            s.bb ? "BACKBUFFER" : "offscreen", s.dsv ? "yes" : "no", s.draws, s.hudDraws, vs);
    }
    g_seq.clear();
}

using PFN_SetShader = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, void*, void* const*, UINT);
using PFN_OMSetRT = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using PFN_Draw = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using PFN_DrawIndexed = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using PFN_DrawIndexedInstanced = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using PFN_DrawInstanced = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);

static void UiHuntOnShader(ID3D11DeviceContext* c, void* sh, bool pixel);
static void HuntOnVS(ID3D11DeviceContext* c, void* sh);
static void* BillboardSwap(ID3D11DeviceContext* c, void* sh);
static void STDMETHODCALLTYPE Hook_VSSetShader(ID3D11DeviceContext* c, void* sh, void* const* ci, UINT n) {
    g_calls[C_VSSetShader]++;
    g_curVS = sh;
    ORIG(c, I_VSSetShader, PFN_SetShader)(c, BillboardSwap(c, sh), ci, n);
    UiHuntOnShader(c, sh, false);
    HuntOnVS(c, sh);
}
static void TrackRT(UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv) {
    if (g_capture) {
        void* rt = nullptr; UINT w = 0, h = 0, fmt = 0;
        if (n && rtv && rtv[0]) {
            ID3D11Resource* r = nullptr; rtv[0]->GetResource(&r);
            if (r) {
                rt = r;
                D3D11_RESOURCE_DIMENSION dim; r->GetType(&dim);
                if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)r)->GetDesc(&d); w = d.Width; h = d.Height; fmt = d.Format; }
                r->Release();
            }
        }
        if (rt != g_curRT || (void*)dsv != g_curDSV) { g_curRT = rt; g_curDSV = dsv; g_curW = w; g_curH = h; g_curFmt = fmt; SeqNewSegment(); }
    }
}
// ---- HUD separation ----
// Every frame the game scales the 3D scene into one display-sized buffer ("UI target"),
// binds that same buffer again WITH a depth buffer and draws all UI into it, then copies it
// to the backbuffer. The UI target is learned as "the last render target before the
// backbuffer". While the game binds it with a depth buffer, we bind our own transparent
// texture instead, so the scene stays clean and the UI ends up on its own layer.
static void* g_uiTarget = nullptr;          // learned UI target (raw pointer, comparison only)
static void* g_uiCandidate = nullptr; static int g_uiCandidateFrames = 0;
static void* g_lastNonBBRT = nullptr;
static ID3D11Texture2D* g_uiTex = nullptr;
static ID3D11RenderTargetView* g_uiRTV = nullptr;
// dual render: copy of the panel after the first eye pass (the UI is drawn once per eye)
static ID3D11Texture2D* g_uiTexP0 = nullptr; static ID3D11RenderTargetView* g_uiRTVP0 = nullptr; static bool g_panelP0Valid = false;
static bool g_uiClearedThisFrame = false, g_uiDrawnThisFrame = false;

static void EnsureUiTex(ID3D11DeviceContext* c, ID3D11Resource* like) {
    D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)like)->GetDesc(&d);
    if (g_uiTex) {
        D3D11_TEXTURE2D_DESC e; g_uiTex->GetDesc(&e);
        if (e.Width == d.Width && e.Height == d.Height && e.Format == d.Format) return;
        g_uiRTV->Release(); g_uiTex->Release(); g_uiRTV = nullptr; g_uiTex = nullptr;
    }
    ID3D11Device* dev = nullptr; c->GetDevice(&dev);
    D3D11_TEXTURE2D_DESC t = {};
    t.Width = d.Width; t.Height = d.Height; t.MipLevels = 1; t.ArraySize = 1; t.Format = d.Format;
    t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_DEFAULT; t.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(dev->CreateTexture2D(&t, nullptr, &g_uiTex)) && SUCCEEDED(dev->CreateRenderTargetView(g_uiTex, nullptr, &g_uiRTV)))
        Log("HUD layer texture %ux%u fmt %d created", d.Width, d.Height, (int)d.Format);
    else { Log("HUD layer texture create FAILED"); if (g_uiTex) { g_uiTex->Release(); g_uiTex = nullptr; } }
    dev->Release();
}

// Returns true if it bound the substitute itself (caller must not call the original).
// ---- UI routing + hunting ----
// During the UI pass (the game binds the UI target with a depth buffer) every draw is routed
// by the shaders bound for it:
//   - normal UI                      -> our HUD panel texture (floating panel in VR)
//   - "world UI" vertex shaders      -> the game's own target, i.e. the 3D image. These are
//     billboards (item markers, speech indicators, nearby waypoints) projected with cb_scene,
//     which we already head-track, so in the 3D image they sit correctly in the world.
//   - the shader selected for hunting -> a trash texture (hidden)
// Hunting keys (NumLock on): Numpad 1/2 prev/next UI vertex shader, 4/5 prev/next UI pixel
// shader, 0 stop, 3 list to log, 6 toggle the selected vertex shader as "world UI" (saved).
static bool g_uiPass = false;
static ID3D11RenderTargetView *g_uiGameRTV = nullptr, *g_uiPanelRTV = nullptr, *g_uiCurRTV = nullptr;
static ID3D11DepthStencilView* g_uiDSV = nullptr;
static ID3D11Texture2D* g_trashTex = nullptr; static ID3D11RenderTargetView* g_trashRTV = nullptr;
static std::vector<uint64_t> g_uiVs, g_uiPs, g_worldVs;

static bool g_curVsHidden = false, g_curPsHidden = false, g_curVsWorld = false;
static bool g_onUiTarget = false;     // the UI target is bound (with or without a depth buffer)
static bool g_curVsPanel = false;     // current VS is a HUD shader -> panel even outside the UI pass

static bool IsListedHud(uint64_t h) { for (uint64_t k : kHudVS) if (k == h) return true; return false; }
static bool IsWorldVs(uint64_t h) { return std::find(g_worldVs.begin(), g_worldVs.end(), h) != g_worldVs.end(); }

static std::vector<uint64_t> g_panelVsExtra;
static bool IsPanelVs(uint64_t h) {
    if (IsListedHud(h)) return true;
    return std::find(g_panelVsExtra.begin(), g_panelVsExtra.end(), h) != g_panelVsExtra.end();
}

static void LoadWorldVs() {
    char buf[1024];
    GetPrivateProfileStringA("HUD", "PanelVS", "", buf, sizeof buf, g_ini);
    g_panelVsExtra.clear();
    for (char* t = strtok(buf, ", "); t; t = strtok(nullptr, ", ")) { uint64_t h = strtoull(t, nullptr, 16); if (h) g_panelVsExtra.push_back(h); }
    GetPrivateProfileStringA("HUD", "WorldVS", "", buf, sizeof buf, g_ini);
    g_worldVs.clear();
    for (char* t = strtok(buf, ", "); t; t = strtok(nullptr, ", ")) { uint64_t h = strtoull(t, nullptr, 16); if (h) g_worldVs.push_back(h); }
}
static void SaveWorldVs() {
    std::string v;
    for (uint64_t h : g_worldVs) { char t[24]; snprintf(t, sizeof t, "%s%016llx", v.empty() ? "" : ",", (unsigned long long)h); v += t; }
    WritePrivateProfileStringA("HUD", "WorldVS", v.c_str(), g_ini);
}

static void EnsureTrash(ID3D11DeviceContext* c, ID3D11Resource* like) {
    D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)like)->GetDesc(&d);
    if (g_trashTex) {
        D3D11_TEXTURE2D_DESC e; g_trashTex->GetDesc(&e);
        if (e.Width == d.Width && e.Height == d.Height && e.Format == d.Format) return;
        g_trashRTV->Release(); g_trashTex->Release(); g_trashRTV = nullptr; g_trashTex = nullptr;
    }
    ID3D11Device* dev = nullptr; c->GetDevice(&dev);
    D3D11_TEXTURE2D_DESC t = {};
    t.Width = d.Width; t.Height = d.Height; t.MipLevels = 1; t.ArraySize = 1; t.Format = d.Format;
    t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_DEFAULT; t.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&t, nullptr, &g_trashTex)) || FAILED(dev->CreateRenderTargetView(g_trashTex, nullptr, &g_trashRTV))) {
        if (g_trashTex) { g_trashTex->Release(); g_trashTex = nullptr; }
        g_trashRTV = nullptr;
    }
    dev->Release();
}

static void PanelClearOnce(ID3D11DeviceContext* c) {
    if (g_uiRTV && !g_uiClearedThisFrame) { const float z[4] = { 0, 0, 0, 0 }; c->ClearRenderTargetView(g_uiRTV, z); g_uiClearedThisFrame = true; }
}

static void UiRoute(ID3D11DeviceContext* c) {
    if (!g_onUiTarget) return;
    ID3D11RenderTargetView* want;
    if (g_uiPass) want = g_curVsWorld ? g_uiGameRTV : g_uiPanelRTV;
    else {
        // before the UI pass (no depth buffer): normally the scene composite -> leave it alone,
        // but HUD shaders (e.g. the menu panel) belong on the floating panel
        want = (g_curVsPanel && g_cfg.hud && g_uiRTV) ? g_uiRTV : g_uiGameRTV;
        if (want == g_uiRTV) { PanelClearOnce(c); g_uiDrawnThisFrame = true; }
    }
    if ((g_curVsHidden || g_curPsHidden) && g_trashRTV) want = g_trashRTV;
    if (want == g_uiCurRTV || !want) return;
    ORIG(c, I_OMSetRenderTargets, PFN_OMSetRT)(c, 1, &want, g_uiDSV);
    g_uiCurRTV = want;
}
static bool IsPanelVs(uint64_t h);

static void UiHuntOnShader(ID3D11DeviceContext* c, void* sh, bool pixel) {
    if (!g_onUiTarget) return;
    uint64_t h = sh ? ShaderHash(sh) : 0;
    if (!pixel) g_curVsPanel = IsPanelVs(h);
    if (!g_uiPass) { if (!pixel) UiRoute(c); return; }
    auto& list = pixel ? g_uiPs : g_uiVs;
    if (h && std::find(list.begin(), list.end(), h) == list.end() && list.size() < 256) list.push_back(h);
    if (!pixel) g_curVsWorld = g_cfg.hud && IsWorldVs(h);
    UiRoute(c);
}

// ---- global shader hunting (any pass) ----
// Every vertex shader bound anywhere in the frame is listed in first-seen order. The selected
// one is hidden by squashing the viewport to 1x1 pixel while it is bound (safe in any pass).
// NumLock on: Numpad 2/1 next/previous, 8/7 +10/-10, 0 stop, 3 list to log,
//             9 save the selected shader's disassembly next to the game exe,
//             6 toggle the selected shader as "world UI" (UI pass draws it into the 3D image).
struct VsSeen { uint64_t h; UINT w, h2; bool ui; };
static std::vector<VsSeen> g_allVs;
static std::map<uint64_t, int> g_allVsIdx;
static int  g_hunt = -1;
static bool g_vpHidden = false;
static D3D11_VIEWPORT g_vp[16]; static UINT g_vpN = 0;
static UINT g_curRTw = 0, g_curRTh = 0;
using PFN_RSSetViewports = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);

static void HuntHide(ID3D11DeviceContext* c, bool hide) {
    if (hide == g_vpHidden) return;
    if (hide) { D3D11_VIEWPORT one = { 0, 0, 1, 1, 0, 1 }; ORIG(c, I_RSSetViewports, PFN_RSSetViewports)(c, 1, &one); }
    else if (g_vpN) ORIG(c, I_RSSetViewports, PFN_RSSetViewports)(c, g_vpN, g_vp);
    g_vpHidden = hide;
}
static void STDMETHODCALLTYPE Hook_RSSetViewports(ID3D11DeviceContext* c, UINT n, const D3D11_VIEWPORT* vp) {
    g_vpN = n > 16 ? 16 : n;
    if (vp) memcpy(g_vp, vp, g_vpN * sizeof(D3D11_VIEWPORT));
    if (g_vpHidden) return;                         // keep hiding until the shader changes
    ORIG(c, I_RSSetViewports, PFN_RSSetViewports)(c, n, vp);
}
static void HuntOnVS(ID3D11DeviceContext* c, void* sh) {
    uint64_t h = sh ? ShaderHash(sh) : 0;
    if (h && !g_allVsIdx.count(h) && g_allVs.size() < 2000) {
        g_allVsIdx[h] = (int)g_allVs.size();
        g_allVs.push_back({ h, g_curRTw, g_curRTh, g_uiPass });
    }
    HuntHide(c, g_hunt >= 0 && g_hunt < (int)g_allVs.size() && g_allVs[g_hunt].h == h);
}

static void DumpVsDisassembly(uint64_t h) {
    auto it = g_vsCode.find(h);
    if (it == g_vsCode.end()) { Log("hunting: no bytecode stored for %016llx", (unsigned long long)h); return; }
    static pD3DDisassemble dis = nullptr;
    if (!dis) { HMODULE m = LoadLibraryA("d3dcompiler_47.dll"); if (m) dis = (pD3DDisassemble)GetProcAddress(m, "D3DDisassemble"); }
    char path[MAX_PATH]; snprintf(path, sizeof path, "%sSoraVR_VS_%016llx.txt", g_dir, (unsigned long long)h);
    ID3DBlob* blob = nullptr;
    if (!dis || FAILED(dis(it->second.data(), it->second.size(), 0, nullptr, &blob)) || !blob) { Log("hunting: disassembly failed (d3dcompiler_47.dll missing?)"); return; }
    FILE* f = fopen(path, "wb");
    if (f) { fwrite(blob->GetBufferPointer(), 1, blob->GetBufferSize(), f); fclose(f); Log("hunting: disassembly saved to %s", path); }
    blob->Release();
}

static void UiHuntKeys(bool (*pressed)(int)) {
    auto step = [](int d) {
        if (g_allVs.empty()) { Log("hunting: no shaders seen yet"); return; }
        int n = (int)g_allVs.size();
        g_hunt = (g_hunt < 0) ? (d > 0 ? 0 : n - 1) : ((g_hunt + d) % n + n) % n;
        const VsSeen& v = g_allVs[g_hunt];
        Log("hunting: hiding VS %d/%d = %016llx  (first seen in %s, render target %ux%u)%s%s", g_hunt + 1, n, (unsigned long long)v.h,
            v.ui ? "UI pass" : "scene", v.w, v.h2, IsListedHud(v.h) ? "  geo-11 HUD" : "", IsWorldVs(v.h) ? "  [world UI]" : "");
    };
    if (pressed(VK_NUMPAD2)) step(+1);
    if (pressed(VK_NUMPAD1)) step(-1);
    if (pressed(VK_NUMPAD8)) step(+10);
    if (pressed(VK_NUMPAD7)) step(-10);
    if (pressed(VK_NUMPAD0)) { g_hunt = -1; Log("hunting: off (nothing hidden)"); }
    if (pressed(VK_NUMPAD5)) { g_billboardFix = !g_billboardFix; Log("billboard fix %s", g_billboardFix ? "ON" : "OFF");
                               WritePrivateProfileStringA("HUD", "BillboardFix", g_billboardFix ? "1" : "0", g_ini); }
    if (pressed(VK_NUMPAD9)) { if (g_hunt >= 0) DumpVsDisassembly(g_allVs[g_hunt].h); else Log("hunting: select a shader first"); }
    if (pressed(VK_NUMPAD6)) {
        if (g_hunt < 0) Log("hunting: select a vertex shader first");
        else {
            uint64_t h = g_allVs[g_hunt].h;
            auto it = std::find(g_worldVs.begin(), g_worldVs.end(), h);
            if (it == g_worldVs.end()) { g_worldVs.push_back(h); Log("world UI: %016llx now drawn into the 3D image (UI pass only)", (unsigned long long)h); }
            else { g_worldVs.erase(it); Log("world UI: %016llx back on the HUD panel", (unsigned long long)h); }
            SaveWorldVs();
        }
    }
    if (pressed(VK_NUMPAD3)) {
        Log("hunting: %zu vertex shaders seen:", g_allVs.size());
        for (size_t i = 0; i < g_allVs.size(); i++)
            Log("   VS %3zu %016llx %s %ux%u%s", i + 1, (unsigned long long)g_allVs[i].h, g_allVs[i].ui ? "UI   " : "scene", g_allVs[i].w, g_allVs[i].h2,
                IsListedHud(g_allVs[i].h) ? "  geo-11 HUD" : "");
    }
}

static bool HudRedirect(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv,
                        ID3D11RenderTargetView** subst) {
    ID3D11Resource* r = nullptr;
    if (n && rtv && rtv[0]) { rtv[0]->GetResource(&r); if (r) r->Release(); }   // raw pointer only
    g_curRTw = g_curRTh = 0;
    if (r) { D3D11_RESOURCE_DIMENSION dim; r->GetType(&dim);
             if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)r)->GetDesc(&d); g_curRTw = d.Width; g_curRTh = d.Height; } }
    // UI pass = the game binds the UI target together with a depth buffer
    g_onUiTarget = r && r == g_uiTarget && n >= 1 && n <= 8;
    g_uiPass = g_onUiTarget && dsv;
    if (r && r == g_backbuffer) {
        if (g_lastNonBBRT) {
            if (g_lastNonBBRT == g_uiCandidate) g_uiCandidateFrames++; else { g_uiCandidate = g_lastNonBBRT; g_uiCandidateFrames = 1; }
            if (g_uiCandidateFrames == 5 && g_uiTarget != g_uiCandidate) {
                // keep a reference so the dual-render capture never copies from a freed texture
                ((ID3D11Resource*)g_uiCandidate)->AddRef();
                if (g_uiTarget) ((ID3D11Resource*)g_uiTarget)->Release();
                g_uiTarget = g_uiCandidate; Log("HUD: UI target learned (%p)", g_uiTarget);
            }
        }
        return false;
    }
    if (r) g_lastNonBBRT = r;
    if (g_onUiTarget && !g_uiPass) {          // UI target without depth: scene composite (and maybe HUD shaders)
        g_uiGameRTV = rtv[0]; g_uiDSV = dsv; g_uiCurRTV = rtv[0];
        if (g_cfg.hud) EnsureUiTex(c, r);
        return false;                          // UiRoute (called after the bind) moves HUD shaders to the panel
    }
    if (!g_uiPass) return false;
    g_uiGameRTV = rtv[0]; g_uiDSV = dsv; g_uiPanelRTV = rtv[0]; g_uiCurRTV = rtv[0];
    EnsureTrash(c, r);
    if (!g_cfg.hud) return false;
    EnsureUiTex(c, r);
    if (!g_uiRTV) return false;
    PanelClearOnce(c);
    for (UINT i = 0; i < n; i++) subst[i] = rtv[i];
    subst[0] = g_uiRTV;
    g_uiPanelRTV = g_uiRTV; g_uiCurRTV = g_uiRTV;
    g_uiDrawnThisFrame = true;
    return true;
}

static LONG g_firstRtFrame = -1;
static void STDMETHODCALLTYPE Hook_OMSetRenderTargets(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv) {
    g_calls[C_OMSetRT]++;
    if (g_firstRtFrame != g_frame && g_frame > 600) { g_firstRtFrame = g_frame; LogStack("frame_first_rt_bind"); }
    TrackRT(n, rtv, dsv);
    ID3D11RenderTargetView* sub[8];
    if (HudRedirect(c, n, rtv, dsv, sub)) ORIG(c, I_OMSetRenderTargets, PFN_OMSetRT)(c, n, sub, dsv);
    else ORIG(c, I_OMSetRenderTargets, PFN_OMSetRT)(c, n, rtv, dsv);
    if (g_onUiTarget) {   // the VS may have been set before this bind
        uint64_t h = g_curVS ? ShaderHash(g_curVS) : 0;
        g_curVsPanel = IsPanelVs(h);
        g_curVsWorld = g_cfg.hud && IsWorldVs(h);
    }
    UiRoute(c);   // the currently bound shaders may need the 3D image, the panel or the trash target
}
using PFN_OMSetRTUAV = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
static void STDMETHODCALLTYPE Hook_OMSetRTUAV(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv,
                                              UINT us, UINT un, ID3D11UnorderedAccessView* const* uav, const UINT* ic) {
    g_calls[C_OMSetRTUAV]++;
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
        TrackRT(n, rtv, dsv);
        ID3D11RenderTargetView* sub[8];
        if (HudRedirect(c, n, rtv, dsv, sub)) { ORIG(c, I_OMSetRTUAV, PFN_OMSetRTUAV)(c, n, sub, dsv, us, un, uav, ic); return; }
    }
    ORIG(c, I_OMSetRTUAV, PFN_OMSetRTUAV)(c, n, rtv, dsv, us, un, uav, ic);
}
using PFN_DrawAuto = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using PFN_Indirect = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
using PFN_Dispatch = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
static void STDMETHODCALLTYPE Hook_DrawAuto(ID3D11DeviceContext* c) { g_calls[C_DrawAuto]++; SeqDraw(); ORIG(c, I_DrawAuto, PFN_DrawAuto)(c); }
static void STDMETHODCALLTYPE Hook_DIII(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o) { g_calls[C_DIII]++; SeqDraw(); ORIG(c, I_DrawIndexedInstancedIndirect, PFN_Indirect)(c, b, o); }
static void STDMETHODCALLTYPE Hook_DII(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o) { g_calls[C_DII]++; SeqDraw(); ORIG(c, I_DrawInstancedIndirect, PFN_Indirect)(c, b, o); }
static void STDMETHODCALLTYPE Hook_Dispatch(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) { g_calls[C_Dispatch]++; ORIG(c, I_Dispatch, PFN_Dispatch)(c, x, y, z); }
static void STDMETHODCALLTYPE Hook_DispatchIndirect(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o) { g_calls[C_DispatchIndirect]++; ORIG(c, I_DispatchIndirect, PFN_Indirect)(c, b, o); }
static void STDMETHODCALLTYPE Hook_Draw(ID3D11DeviceContext* c, UINT a, UINT b) { g_calls[C_Draw]++; SeqDraw(); ORIG(c, I_Draw, PFN_Draw)(c, a, b); }
static void STDMETHODCALLTYPE Hook_DrawIndexed(ID3D11DeviceContext* c, UINT a, UINT b, INT d) { g_calls[C_DrawIndexed]++; SeqDraw(); ORIG(c, I_DrawIndexed, PFN_DrawIndexed)(c, a, b, d); }
static void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f) { g_calls[C_DrawIndexedInstanced]++; SeqDraw(); ORIG(c, I_DrawIndexedInstanced, PFN_DrawIndexedInstanced)(c, a, b, d, e, f); }
static void STDMETHODCALLTYPE Hook_DrawInstanced(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e) { g_calls[C_DrawInstanced]++; SeqDraw(); ORIG(c, I_DrawInstanced, PFN_DrawInstanced)(c, a, b, d, e); }

// ---- outline width compensation ----
// The toon outlines are a screen-space effect inside the deferred lighting pixel shader
// (3DMigoto hash e49978a78bd4be0e): it samples an "outline prepare" texture at
// offsetsAndWeights[i].xy (cb_local, b2, c4..c11) times a thickness. Those offsets are in
// screen units, so when the FOV override widens the view the outlines look relatively
// thicker. While that shader is bound we bind a copy of its b2 buffer with the offsets
// scaled by tan(baseFov/2) / tan(newFov/2).
static const uint64_t kOutlinePS = 0xe49978a78bd4be0eull;
static float OutlineScale();                       // defined with the FOV override
static bool  g_psIsOutline = false, g_outlineSeen = false;
static bool  g_sceneLit = false;                    // the deferred lighting shader was bound during this render pass
static ID3D11Buffer* g_psB2 = nullptr;              // the game's current PS b2 buffer (not ref-counted)
static ID3D11Buffer* g_olCopy = nullptr;            // our scaled copy
static ID3D11Buffer* g_olSrc = nullptr;  static UINT g_olSize = 0; static LONG g_olReadFrame = -1000;
static float g_olData[1024];
static float g_olAppliedK = 0;
static bool  g_olBound = false;

static bool OutlineReadSource(ID3D11DeviceContext* c, ID3D11Buffer* src) {
    D3D11_BUFFER_DESC d; src->GetDesc(&d);
    if (d.ByteWidth < 192 || d.ByteWidth > sizeof g_olData) return false;
    ID3D11Device* dev = nullptr; c->GetDevice(&dev);
    D3D11_BUFFER_DESC sd = {}; sd.ByteWidth = d.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* st = nullptr;
    bool ok = SUCCEEDED(dev->CreateBuffer(&sd, nullptr, &st));
    if (ok) {
        c->CopyResource(st, src);
        D3D11_MAPPED_SUBRESOURCE m;
        ok = SUCCEEDED(ORIG(c, I_Map, PFN_Map)(c, st, 0, D3D11_MAP_READ, 0, &m));
        if (ok) { memcpy(g_olData, m.pData, d.ByteWidth); ORIG(c, I_Unmap, PFN_Unmap)(c, st, 0); }
        st->Release();
    }
    if (ok && (!g_olCopy || g_olSize != d.ByteWidth)) {
        if (g_olCopy) { g_olCopy->Release(); g_olCopy = nullptr; }
        D3D11_BUFFER_DESC cd = {}; cd.ByteWidth = d.ByteWidth; cd.Usage = D3D11_USAGE_DEFAULT; cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ok = SUCCEEDED(dev->CreateBuffer(&cd, nullptr, &g_olCopy));
    }
    dev->Release();
    if (ok) {
        g_olSrc = src; g_olSize = d.ByteWidth; g_olReadFrame = g_frame; g_olAppliedK = 0;
        static int logged = 0;
        if (logged++ < 3) Log("outline params read (%u bytes): maxThickness=%.4f offsets[0]=(%.5f %.5f) offsets[1]=(%.5f %.5f)",
                              d.ByteWidth, g_olData[9], g_olData[16], g_olData[17], g_olData[20], g_olData[21]);
    }
    return ok;
}

static void OutlineUpdateBinding(ID3D11DeviceContext* c) {
    float k = OutlineScale();
    bool want = g_psIsOutline && g_psB2 && k < 0.995f;
    if (want) {
        if (g_psB2 != g_olSrc) { if (!OutlineReadSource(c, g_psB2)) want = false; }
    }
    if (want) {
        if (fabsf(k - g_olAppliedK) > 1e-4f) {
            static float tmp[1024];
            memcpy(tmp, g_olData, g_olSize);
            for (int i = 0; i < 8; i++) { tmp[16 + i*4] *= k; tmp[17 + i*4] *= k; }   // offsetsAndWeights[i].xy
            c->UpdateSubresource(g_olCopy, 0, nullptr, tmp, 0, 0);
            g_olAppliedK = k;
        }
        ORIG(c, I_PSSetCB, PFN_SetCB)(c, 2, 1, &g_olCopy);
        g_olBound = true;
    } else if (g_olBound) {
        ORIG(c, I_PSSetCB, PFN_SetCB)(c, 2, 1, &g_psB2);
        g_olBound = false;
    }
}

static bool OutlineIsSource(ID3D11Resource* r) { return r && r == (ID3D11Resource*)g_olSrc; }
static void OutlineCapture(const void* data, UINT) {     // the game updated the buffer we mirror
    if (!g_olSize) return;
    memcpy(g_olData, data, g_olSize);
    g_olAppliedK = 0;                                     // re-upload the scaled copy at next bind
}

static void OutlineAfterPSCB(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b) {
    if (!b || s > 2 || s + n <= 2) return;
    g_psB2 = b[2 - s];
    g_olBound = false;                          // the game just replaced b2 itself
    OutlineUpdateBinding(c);
}

static void STDMETHODCALLTYPE Hook_PSSetShader(ID3D11DeviceContext* c, void* sh, void* const* ci, UINT n) {
    ORIG(c, I_PSSetShader, PFN_SetShader)(c, sh, ci, n);
    bool was = g_psIsOutline;
    g_psIsOutline = sh && ShaderHash(sh) == kOutlinePS;
    if (g_psIsOutline) g_sceneLit = true;          // deferred lighting ran: the 3D world is being drawn
    if (g_psIsOutline && !g_outlineSeen) { g_outlineSeen = true; Log("outline shader (e49978a78bd4be0e) is in use"); }
    if (was != g_psIsOutline) OutlineUpdateBinding(c);
    UiHuntOnShader(c, sh, true);
}

static void HookContext(ID3D11DeviceContext* ctx, const char* what) {
    std::lock_guard<std::mutex> lk(g_ctxVtMx);
    void** vt = *(void***)ctx;
    for (auto& c : g_ctxVt) if (c.vt == vt) return;           // this class is already hooked
    CtxVt* slot = nullptr;
    for (auto& c : g_ctxVt) if (!c.vt) { slot = &c; break; }
    if (!slot) { Log("too many context vtables, not hooking %s", what); return; }
    ID3D11DeviceContext1* c1 = nullptr;
    slot->isCtx1 = SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1 && *(void***)c1 == vt;
    if (c1) c1->Release();
    for (int i = 0; i < I_Count; i++) slot->orig[i] = vt[i];
    slot->vt = vt;
    auto hook = [&](int idx, void* fn) { slot->orig[idx] = PatchVtable(ctx, idx, fn); };
    hook(I_VSSetCB, (void*)Hook_VSSetCB);   hook(I_PSSetCB, (void*)Hook_PSSetCB);   hook(I_CSSetCB, (void*)Hook_CSSetCB);
    hook(I_Map, (void*)Hook_Map);           hook(I_Unmap, (void*)Hook_Unmap);
    hook(I_UpdateSubresource, (void*)Hook_UpdateSubresource);
    hook(I_CopyResource, (void*)Hook_CopyResource);
    hook(I_CopySubresourceRegion, (void*)Hook_CopySubresourceRegion);
    hook(I_ExecuteCommandList, (void*)Hook_ExecuteCommandList);
    hook(I_VSSetShader, (void*)Hook_VSSetShader);
    hook(I_RSSetViewports, (void*)Hook_RSSetViewports);
    hook(I_PSSetShader, (void*)Hook_PSSetShader);
    hook(I_OMSetRenderTargets, (void*)Hook_OMSetRenderTargets);
    hook(I_Draw, (void*)Hook_Draw); hook(I_DrawIndexed, (void*)Hook_DrawIndexed);
    hook(I_DrawIndexedInstanced, (void*)Hook_DrawIndexedInstanced); hook(I_DrawInstanced, (void*)Hook_DrawInstanced);
    hook(I_OMSetRTUAV, (void*)Hook_OMSetRTUAV); hook(I_DrawAuto, (void*)Hook_DrawAuto);
    hook(I_DrawIndexedInstancedIndirect, (void*)Hook_DIII); hook(I_DrawInstancedIndirect, (void*)Hook_DII);
    hook(I_Dispatch, (void*)Hook_Dispatch); hook(I_DispatchIndirect, (void*)Hook_DispatchIndirect);
    {   // verify the patches took
        int bad = 0; int idx[] = { I_Draw, I_DrawIndexed, I_DrawIndexedInstanced, I_DrawInstanced, I_VSSetShader, I_OMSetRenderTargets };
        for (int i : idx) if (!slot->orig[i]) bad++;
        Log("draw hooks: %s", bad ? "SOME NOT INSTALLED" : "installed");
    }
    if (slot->isCtx1) {
        hook(I_UpdateSubresource1, (void*)Hook_UpdateSubresource1);
        hook(I_VSSetCB1, (void*)Hook_VSSetCB1); hook(I_PSSetCB1, (void*)Hook_PSSetCB1); hook(I_CSSetCB1, (void*)Hook_CSSetCB1);
    }
    Log("hooked %s context class (vtable %p, DeviceContext1 methods %s)", what, (void*)vt, slot->isCtx1 ? "hooked" : "not available");
}

static PFN_CreateDeferredContext o_CreateDeferredContext = nullptr;
static PFN_CreateBuffer          o_CreateBuffer = nullptr;
using PFN_CreateVS = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
using PFN_CreatePS = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
static PFN_CreateVS o_CreateVS = nullptr;
static PFN_CreatePS o_CreatePS = nullptr;
// ---- world-anchored UI billboards (items, speech indicators, nearby waypoints) ----
// VS f006f8946dc6d3c4 takes each element's per-instance matrix from instances_g (t15). That
// matrix already includes the game's MONO camera and projection (clip = world * pos), so the
// elements stay glued to the game camera instead of the head. Replacement = the same shader
// (rebuilt from its disassembly) plus clip' = K * clip, K = P_eye * Minv * P_game^-1, which is
// this frame's head/eye correction expressed in clip space. K comes from cb b13.
static const char* kBillboardHlsl = R"HLSL(
cbuffer cb_instance : register(b1) { int instanceOffset_g; };
cbuffer SoraVRFix : register(b13) { float4 K0; float4 K1; float4 K2; float4 K3; };
struct UICameraParam { float4 pi0; float4 pi1; float4 pi2; float4 pi3; float2 screenScale; };
struct InstanceParam { float4 w0; float4 w1; float4 w2; float4 w3; INSTANCE_EXTRA float4 color; float4 uv; float4 param; uint boneAddress; float3 param2; };
StructuredBuffer<UICameraParam> uiCameras_g : register(t2);
StructuredBuffer<InstanceParam> instances_g : register(t15);
struct VSOut {
    float4 pos : SV_Position;
    float3 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
    float3 t2 : TEXCOORD2;
    float4 t3 : TEXCOORD3;
    nointerpolation int t4 : TEXCOORD4;
    float4 t5 : TEXCOORD5;
    float4 t6 : TEXCOORD6;
    float4 t7 : TEXCOORD7;
};
VSOut main(float3 p : POSITION, float2 uv : TEXCOORD0, uint iid : SV_InstanceID) {
    VSOut o;
    uint i = iid + (uint)instanceOffset_g;
    InstanceParam ip = instances_g[i];
    float4 v = float4(p, 1);
    float4 c = float4(dot(v, ip.w0), dot(v, ip.w1), dot(v, ip.w2), dot(v, ip.w3));
    c = float4(dot(K0, c), dot(K1, c), dot(K2, c), dot(K3, c));
    o.pos = c;
    o.t3 = c;
    float2 t = uv * float2(1, -1) + float2(0, 1);
    o.t0.xy = t * ip.uv.zw + ip.uv.xy;
    o.t0.z = (ip.boneAddress & 256) ? 1.0 : 0.0;
    o.t1 = ip.color;
    o.t2 = ip.param2;
    o.t4 = (int)ip.param.x;
    UICameraParam uc = uiCameras_g[ip.boneAddress & 255];
    o.t5 = uc.pi2;
    o.t6 = uc.pi3;
    o.t7 = float4(ip.param.y, ip.param.z, uc.screenScale.x, uc.screenScale.y);
    return o;
}
)HLSL";
// Second label shader (6ab95c20c50948e5): same inputs and per-instance clip matrix, different outputs.
static const char* kBillboardHlsl2 = R"HLSL(
cbuffer cb_instance : register(b1) { int instanceOffset_g; };
cbuffer SoraVRFix : register(b13) { float4 K0; float4 K1; float4 K2; float4 K3; };
struct UICameraParam { float4 pi0; float4 pi1; float4 pi2; float4 pi3; float2 screenScale; };
struct InstanceParam { float4 w0; float4 w1; float4 w2; float4 w3; INSTANCE_EXTRA float4 color; float4 uv; float4 param; uint boneAddress; float3 param2; };
StructuredBuffer<UICameraParam> uiCameras_g : register(t2);
StructuredBuffer<InstanceParam> instances_g : register(t15);
struct VSOut {
    float4 pos : SV_Position;
    float2 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
    float3 t2 : TEXCOORD2;
    float4 t3 : TEXCOORD3;
    nointerpolation float2 t4 : TEXCOORD4;   // shares a register with t5 in the original (keeps the packing identical)
    nointerpolation int t5 : TEXCOORD5;
    float4 t6 : TEXCOORD6;
    float4 t7 : TEXCOORD7;
    float4 t8 : TEXCOORD8;
    float4 t9 : TEXCOORD9;
};
VSOut main(float3 p : POSITION, float2 uv : TEXCOORD0, uint iid : SV_InstanceID) {
    VSOut o;
    uint i = iid + (uint)instanceOffset_g;
    InstanceParam ip = instances_g[i];
    float4 v = float4(p, 1);
    float4 c = float4(dot(v, ip.w0), dot(v, ip.w1), dot(v, ip.w2), dot(v, ip.w3));
    c = float4(dot(K0, c), dot(K1, c), dot(K2, c), dot(K3, c));
    o.pos = c;
    o.t6 = c;
    float2 t = uv * float2(1, -1) + float2(0, 1);
    o.t0 = t * ip.uv.zw + ip.uv.xy;
    o.t1 = ip.color;
    o.t2 = ip.param2;
    o.t3 = float4(ip.uv.xy, ip.uv.zw + ip.uv.xy);
    o.t4 = float2(ip.param.w * 10.0, (ip.boneAddress & 256) ? 1.0 : 0.0);
    o.t5 = (int)ip.param.x;
    UICameraParam uc = uiCameras_g[ip.boneAddress & 255];
    o.t7 = uc.pi2;
    o.t8 = uc.pi3;
    o.t9 = float4(ip.param.y, ip.param.z, uc.screenScale.x, uc.screenScale.y);
    return o;
}
)HLSL";
// ---- cutscene letterbox bars ----
// The bars are drawn by the game's general 2D sprite shader (4976a4a4235db4a9), which also draws
// dialogue text and most of the HUD, so the shader itself can't be hidden. This replacement is the
// same shader, except a sprite that is opaque black, much wider than tall and reaching the left/right
// screen edge gets its colour (and alpha) set to zero. [HUD] HideLetterbox=0 turns it off.
static const uint64_t kSpriteVS = 0x4976a4a4235db4a9ull;
static const char* kSpriteHlsl = R"HLSL(
cbuffer cb_instance : register(b1) { int instanceOffset_g; };
struct InstanceParam { float4 w0; float4 w1; float4 w2; float4 w3; INSTANCE_EXTRA float4 color; float4 uv; float4 param; uint boneAddress; float3 param2; };
StructuredBuffer<InstanceParam> instances_g : register(t15);
struct VSOut {
    float4 pos : SV_Position;
    float3 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
    float3 t2 : TEXCOORD2;
    nointerpolation int t3 : TEXCOORD3;
};
VSOut main(float3 p : POSITION, float2 uv : TEXCOORD0, uint iid : SV_InstanceID) {
    VSOut o;
    uint i = iid + (uint)instanceOffset_g;
    InstanceParam ip = instances_g[i];
    float4 v = float4(p, 1);
    o.pos = float4(dot(v, ip.w0), dot(v, ip.w1), dot(v, ip.w2), dot(v, ip.w3));
    float2 t = uv * float2(1, -1) + float2(0, 1);
    o.t0 = float3(t * ip.uv.zw + ip.uv.xy, (ip.boneAddress & 256) ? 1.0 : 0.0);
    o.t1 = ip.color;
    o.t2 = ip.param2;
    o.t3 = (int)ip.param.x;
    bool black = ip.color.r < 0.02 && ip.color.g < 0.02 && ip.color.b < 0.02 && ip.color.a > 0.5;
    bool wide  = abs(ip.w1.y) < 0.35 * abs(ip.w0.x);
    bool edge  = abs(o.pos.x) > 0.97 * abs(o.pos.w);
    if (black && wide && edge) o.t1 = float4(0, 0, 0, 0);
    return o;
}
)HLSL";
static std::map<void*, ID3D11VertexShader*> g_lbReplace;

static const char* BillboardHlslFor(uint64_t h) {
    if (h == 0xf006f8946dc6d3c4ull) return kBillboardHlsl;
    if (h == 0x6ab95c20c50948e5ull) return kBillboardHlsl2;
    return nullptr;
}

// Output-signature check: the replacement must hand the pixel shader exactly the same
// registers/components/types as the original, or those draws would break.
struct SigEl { std::string name; uint32_t idx, comp, reg; uint8_t mask; };
static bool ReadSignature(const void* code, size_t len, const char* fourcc, std::vector<SigEl>& out) {
    const uint8_t* b = (const uint8_t*)code;
    if (len < 32 || memcmp(b, "DXBC", 4)) return false;
    uint32_t n = *(const uint32_t*)(b + 28);
    for (uint32_t i = 0; i < n && 32 + i * 4 + 4 <= len; i++) {
        uint32_t off = *(const uint32_t*)(b + 32 + i * 4);
        if (off + 8 > len || memcmp(b + off, fourcc, 4)) continue;
        const uint8_t* d = b + off + 8;
        uint32_t cnt = *(const uint32_t*)d;
        for (uint32_t e = 0; e < cnt; e++) {
            const uint8_t* el = d + 8 + e * 24;
            SigEl s;
            s.name = (const char*)(d + *(const uint32_t*)el);
            s.idx = *(const uint32_t*)(el + 4); s.comp = *(const uint32_t*)(el + 12); s.reg = *(const uint32_t*)(el + 16); s.mask = el[20];
            out.push_back(s);
        }
        return true;
    }
    return false;
}
static bool SameOutputs(const void* a, size_t al, const void* b, size_t bl) {
    std::vector<SigEl> x, y;
    if (!ReadSignature(a, al, "OSGN", x) || !ReadSignature(b, bl, "OSGN", y) || x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++) {
        bool same = _stricmp(x[i].name.c_str(), y[i].name.c_str()) == 0 && x[i].idx == y[i].idx && x[i].comp == y[i].comp &&
                    x[i].reg == y[i].reg && x[i].mask == y[i].mask;
        if (!same) {
            Log("billboard fix: output %s%u differs (original reg %u mask %x, replacement reg %u mask %x)",
                x[i].name.c_str(), x[i].idx, x[i].reg, x[i].mask, y[i].reg, y[i].mask);
            return false;
        }
    }
    return true;
}

static std::vector<uint64_t> g_billboardVs;            // [HUD] BillboardVS
static std::map<void*, ID3D11VertexShader*> g_vsReplace;   // game shader -> our replacement
static ID3D11Buffer* g_fixCB = nullptr;
static LONG g_fixCBFrame = -1;

static void LoadBillboardCfg() {
    char buf[512];
    GetPrivateProfileStringA("HUD", "BillboardVS", "f006f8946dc6d3c4,6ab95c20c50948e5", buf, sizeof buf, g_ini);
    g_billboardVs.clear();
    for (char* t = strtok(buf, ", "); t; t = strtok(nullptr, ", ")) { uint64_t h = strtoull(t, nullptr, 16); if (h) g_billboardVs.push_back(h); }
    g_billboardFix = GetPrivateProfileIntA("HUD", "BillboardFix", 1, g_ini) != 0;
}

static ID3D11VertexShader* CompileBillboard(ID3D11Device* d, const char* hlsl, const void* origCode, SIZE_T origLen) {
    static pD3DCompile compile = nullptr;
    if (!compile) { HMODULE m = LoadLibraryA("d3dcompiler_47.dll"); if (m) compile = (pD3DCompile)GetProcAddress(m, "D3DCompile"); }
    if (!compile) { Log("billboard fix: d3dcompiler_47.dll not available"); return nullptr; }
    int extra = InstanceExtraBytes(origCode, origLen);
    if (extra < 0) { Log("billboard fix: the game's instance data layout is not the expected one - keeping the original shader"); return nullptr; }
    char extraDecl[64] = " ";
    if (extra) snprintf(extraDecl, sizeof extraDecl, "float4 extra_[%d];", extra / 16);
    D3D_SHADER_MACRO macros[] = { { "INSTANCE_EXTRA", extraDecl }, { nullptr, nullptr } };
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT hr = compile(hlsl, strlen(hlsl), "soravr_billboard", macros, nullptr, "main", "vs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr) || !code) {
        Log("billboard fix: compile FAILED: %s", err ? (const char*)err->GetBufferPointer() : "(no message)");
        if (err) err->Release();
        return nullptr;
    }
    if (err) err->Release();
    if (!SameOutputs(origCode, origLen, code->GetBufferPointer(), code->GetBufferSize())) {
        Log("billboard fix: replacement outputs don't match the original - keeping the original shader");
        code->Release();
        return nullptr;
    }
    ID3D11VertexShader* vs = nullptr;
    hr = o_CreateVS(d, code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs);
    code->Release();
    if (FAILED(hr)) { Log("billboard fix: CreateVertexShader failed 0x%08lX", hr); return nullptr; }
    return vs;
}

// Called for every VSSetShader: returns the shader to actually bind.
static void* BillboardSwap(ID3D11DeviceContext* c, void* sh) {
    if (g_cfg.hideLetterbox && sh && !g_lbReplace.empty()) {
        auto lb = g_lbReplace.find(sh);
        if (lb != g_lbReplace.end() && lb->second) return lb->second;
    }
    if (!g_billboardFix || !sh || g_vsReplace.empty()) return sh;
    auto it = g_vsReplace.find(sh);
    if (it == g_vsReplace.end() || !it->second) return sh;
    if (!g_fixCB) {
        ID3D11Device* dev = nullptr; c->GetDevice(&dev);
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        dev->CreateBuffer(&bd, nullptr, &g_fixCB);
        dev->Release();
        if (!g_fixCB) return sh;
    }
    LONG stamp = g_frame * 4 + (g_dualEye + 1);
    if (g_fixCBFrame != stamp) {             // K for this frame / eye (identity if there is nothing to correct)
        float K[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }, Minv[16];
        if (g_haveProj && !g_cfg.levelHorizon && FrameEyeMinv(Minv)) { float t[16]; Mul44(g_Peye, Minv, t); Mul44(t, g_PgameInv, K); }
        c->UpdateSubresource(g_fixCB, 0, nullptr, K, 0, 0);
        g_fixCBFrame = stamp;
    }
    ORIG(c, I_VSSetCB, PFN_SetCB)(c, 13, 1, &g_fixCB);
    return it->second;
}

static HRESULT STDMETHODCALLTYPE Hook_CreateVS(ID3D11Device* d, const void* code, SIZE_T len, ID3D11ClassLinkage* cl, ID3D11VertexShader** out) {
    HRESULT hr = o_CreateVS(d, code, len, cl, out);
    if (SUCCEEDED(hr) && out && *out && code) {
        uint64_t h = RegisterShader(*out, code, len, true);    // canonical (1.06.1) hash for the shaders SoraVR knows
        const char* hlsl = BillboardHlslFor(h);
        if (std::find(g_billboardVs.begin(), g_billboardVs.end(), h) != g_billboardVs.end() && !hlsl)
            Log("billboard fix: %016llx is listed but there is no replacement for it yet", (unsigned long long)h);
        if (h == kSpriteVS) {
            ID3D11VertexShader* r = CompileBillboard(d, kSpriteHlsl, code, len);
            g_lbReplace[*out] = r;
            Log("letterbox fix: sprite shader %s", r ? "replaced (black cutscene bars hidden on the panel)" : "NOT replaced (see error above)");
        }
        if (hlsl && std::find(g_billboardVs.begin(), g_billboardVs.end(), h) != g_billboardVs.end()) {
            ID3D11VertexShader* r = CompileBillboard(d, hlsl, code, len);
            g_vsReplace[*out] = r;
            Log("billboard fix: %016llx %s", (unsigned long long)h, r ? "replaced with the head-tracked version" : "NOT replaced (see error above)");
        }
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE Hook_CreatePS(ID3D11Device* d, const void* code, SIZE_T len, ID3D11ClassLinkage* cl, ID3D11PixelShader** out) {
    HRESULT hr = o_CreatePS(d, code, len, cl, out);
    if (SUCCEEDED(hr) && out && *out && code) RegisterShader(*out, code, len, false);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDeferredContext(ID3D11Device* dev, UINT flags, ID3D11DeviceContext** out) {
    HRESULT hr = o_CreateDeferredContext(dev, flags, out);
    if (SUCCEEDED(hr) && out && *out) {
        g_capDeferred++;
        if (g_capDeferred <= 4) Log("game created a deferred context (#%d)", g_capDeferred);
        HookContext(*out, "deferred");
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_CreateBuffer(ID3D11Device* dev, const D3D11_BUFFER_DESC* d,
                                                   const D3D11_SUBRESOURCE_DATA* init, ID3D11Buffer** out) {
    if (d && (d->BindFlags & D3D11_BIND_CONSTANT_BUFFER)) {
        INV(g_capCreateCB++);
        static int logged = 0;
        if (d->ByteWidth >= kSceneSize && logged < 40) {
            logged++;
            Log("CreateBuffer CB w=%u usage=%u cpu=0x%x initData=%s", d->ByteWidth, d->Usage, d->CPUAccessFlags, init ? "yes" : "no");
        }
        if (init && init->pSysMem && d->ByteWidth >= kSceneSize && d->ByteWidth <= 4096) {
            alignas(16) float tmp[4096 / 4];
            memcpy(tmp, init->pSysMem, d->ByteWidth);
            if (ProcessSceneBuffer(tmp, d->ByteWidth, nullptr)) {
                D3D11_SUBRESOURCE_DATA i2 = *init; i2.pSysMem = tmp;
                return o_CreateBuffer(dev, d, &i2, out);
            }
        }
    }
    return o_CreateBuffer(dev, d, init, out);
}

// ----------------------------------------------------------------------------
// FOV hunt (read-only): find where the game keeps its camera field of view, by scanning
// memory for the current FOV in several encodings and keeping only addresses that follow
// the game's FOV as it changes (gameplay 45 deg vertical, dialogue ~33 deg, ...).
// ----------------------------------------------------------------------------
struct FovCand { uintptr_t addr; uint8_t enc; };
static std::vector<FovCand> g_fovCands;
static bool  g_fovScanned = false;
static float g_fovLastV = 0;
static const char* kEncNames[12] = { "vdeg", "vrad", "vhalfdeg", "vhalfrad", "vtanhalf", "vcothalf",
                                     "hdeg", "hrad", "hhalfdeg", "hhalfrad", "htanhalf", "hcothalf" };
static void FovEncodings(float p00, float p11, float out[12]) {
    float hv = atanf(1.0f / p11), hh = atanf(1.0f / p00);    // half angles (radians)
    float hs[2] = { hv, hh };
    for (int k = 0; k < 2; k++) {
        float a = hs[k];
        out[k*6 + 0] = 2 * a * 57.29578f; out[k*6 + 1] = 2 * a; out[k*6 + 2] = a * 57.29578f;
        out[k*6 + 3] = a; out[k*6 + 4] = tanf(a); out[k*6 + 5] = 1.0f / tanf(a);
    }
}
static inline bool Near(float v, float t) { return fabsf(v - t) <= 2e-5f * fabsf(t) + 1e-7f; }

template <class F> static void ForEachRWChunk(F fn) {
    std::vector<char> buf(1 << 20);
    uintptr_t a = 0x10000;
    MEMORY_BASIC_INFORMATION mi;
    while (VirtualQuery((void*)a, &mi, sizeof mi) == sizeof mi) {
        uintptr_t base = (uintptr_t)mi.BaseAddress, end = base + mi.RegionSize;
        bool ok = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                  (mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE));
        for (uintptr_t p = base; ok && p < end; p += buf.size()) {
            SIZE_T n = (SIZE_T)std::min<uintptr_t>(buf.size(), end - p), got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), (void*)p, buf.data(), n, &got) && got) fn(p, buf.data(), got);
        }
        if (end <= a) break;
        a = end;
    }
}

[[maybe_unused]] static void FovHunt() {
    if (g_gameP11 <= 0 || g_gameP00 <= 0) { Log("FOV hunt: no camera seen yet"); return; }
    float enc[12]; FovEncodings(g_gameP00, g_gameP11, enc);
    float vdeg = enc[0];
    DWORD t0 = GetTickCount();
    if (!g_fovScanned) {
        g_fovCands.clear();
        ForEachRWChunk([&](uintptr_t base, const char* data, SIZE_T n) {
            const float* f = (const float*)data;
            for (SIZE_T i = 0; i < n / 4 && g_fovCands.size() < 4000000; i++) {
                float v = f[i];
                if (!(v > 0.05f && v < 200.0f)) continue;
                for (int e = 0; e < 12; e++) if (Near(v, enc[e])) { g_fovCands.push_back({ base + i * 4, (uint8_t)e }); break; }
            }
        });
        g_fovScanned = true;
        Log("FOV hunt: first scan at %.2f deg vertical -> %zu candidates (%lu ms)", vdeg, g_fovCands.size(), GetTickCount() - t0);
    } else {
        size_t before = g_fovCands.size(), w = 0;
        for (auto& c : g_fovCands) {
            float v; SIZE_T got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), (void*)c.addr, &v, 4, &got) && got == 4 && Near(v, enc[c.enc])) g_fovCands[w++] = c;
        }
        g_fovCands.resize(w);
        Log("FOV hunt: filter at %.2f deg vertical (%s) -> %zu of %zu candidates left (%lu ms)", vdeg,
            fabsf(vdeg - g_fovLastV) > 0.5f ? "FOV CHANGED since last F9 - good" : "same FOV as last F9", w, before, GetTickCount() - t0);
    }
    g_fovLastV = vdeg;
    HMODULE exe = GetModuleHandleA(nullptr);
    MEMORY_BASIC_INFORMATION mi;
    size_t shown = 0, inExe = 0;
    for (auto& c : g_fovCands) {
        bool ie = VirtualQuery((void*)c.addr, &mi, sizeof mi) && mi.AllocationBase == (void*)exe;
        if (ie) inExe++;
        if (g_fovCands.size() <= 40 && shown++ < 40) {
            float v = 0; SIZE_T got; ReadProcessMemory(GetCurrentProcess(), (void*)c.addr, &v, 4, &got);
            Log("   %p %s%-9s = %.5f", (void*)c.addr, ie ? "[exe] " : "", kEncNames[c.enc], v);
        }
    }
    if (g_fovCands.size() > 40) Log("   (%zu candidates, %zu inside the game exe - keep pressing F9 in places with different FOV)", g_fovCands.size(), inExe);
}

// ----------------------------------------------------------------------------
// FOV probe + override. F12: scan heap memory for the camera's current vertical/horizontal
// FOV in radians, then test each candidate (write a wider value for a few frames, see whether
// the game's projection follows, restore). The one that controls the camera is then set to
// GameFov every frame, so the game itself renders AND culls a wider view (no pop-in).
// ----------------------------------------------------------------------------
struct ProbeCand { float* p; bool horiz; float orig; };
static std::vector<ProbeCand> g_probe;
static int   g_probeIdx = -1, g_probeFrame = 0;
static float g_probeBaseP11 = 0, g_probeBaseP00 = 0;
static float* g_fovAddr = nullptr; static bool g_fovAddrHoriz = false;
static float  g_fovBase = 0;   // the game's own vertical FOV (radians) when the probe found it
// Remember the game's natural FOV for the outline correction - but never our own override value:
// after a battle/cutscene the camera can still hold the override when it is re-attached (7c bug:
// base became 98 deg -> no correction -> thick outlines).
static void SetFovBase(float vrad, const char* where) {
    float deg = vrad * 57.29578f;
    if (fabsf(deg - g_cfg.gameFov) < 2.5f || deg < 5.0f || deg > 85.0f) {
        Log("FOV base: %s read %.1f deg (that is the override itself) - keeping %.1f deg for the outline correction",
            where, deg, (g_fovBase > 0.05f ? g_fovBase : 40.0f / 57.29578f) * 57.29578f);
        return;
    }
    g_fovBase = vrad;
}
static bool   g_fovRun = false;  // the FOV sits in a run of three (current/target/base): hold all three
static int   g_fovMismatchFrames = 0;

static bool FovOverrideActive() { return g_fovAddr != nullptr; }
static float OutlineScale() {
    // The outline offsets are fixed screen units tuned for the game's own FOV. Whenever we render
    // wider (override active OR the FOV lock forcing the projection - e.g. a cutscene camera the
    // override doesn't control), shrink them by the ratio. 7b only did this while the override
    // was active, so outlines went thick when it paused in cutscenes.
    if (!g_cfg.outlineFix || (!g_fovAddr && !g_cfg.fovLock)) return 1.0f;
    float base = g_fovBase > 0.05f ? g_fovBase : 40.0f / 57.29578f;
    return tanf(base * 0.5f) / tanf(g_cfg.gameFov / 57.29578f * 0.5f);
}

static bool ReadF(float* p, float* v) { SIZE_T got = 0; return ReadProcessMemory(GetCurrentProcess(), p, v, 4, &got) && got == 4; }
static bool WriteF(float* p, float v) { SIZE_T put = 0; return WriteProcessMemory(GetCurrentProcess(), p, &v, 4, &put) && put == 4; }

static bool g_probeAuto = false;
static void FovSavePaths();

// Run fn on a background thread (memory scans take seconds; keep the game rendering).
#include <functional>
static void RunInBackground(std::function<void()> fn) {
    auto* p = new std::function<void()>(std::move(fn));
    HANDLE h = CreateThread(nullptr, 0, [](LPVOID a) -> DWORD {
        auto* f = (std::function<void()>*)a; (*f)(); delete f; return 0; }, p, 0, nullptr);
    if (h) CloseHandle(h); else { (*p)(); delete p; }
}

static volatile LONG g_probeScan = 0;            // 0 idle, 1 scanning (background), 2 candidates ready
static std::vector<ProbeCand> g_probeNext;
static float g_probeScanV = 0, g_probeScanH = 0;

static void FovProbeStart(bool autoMode = false) {
    if (g_probeIdx >= 0 || g_probeScan) { if (!autoMode) Log("FOV probe already running"); return; }
    if (g_gameP11 <= 0 || g_gameP00 <= 0) { Log("FOV probe: no 3D camera seen yet - try in normal gameplay"); return; }
    if (g_fovAddr) { Log("FOV probe: override already active (address %p) - restart the game to probe again", (void*)g_fovAddr); return; }
    float vrad = 2 * atanf(1.0f / g_gameP11), hrad = 2 * atanf(1.0f / g_gameP00);
    ULONG_PTR stLo = 0, stHi = 0; GetCurrentThreadStackLimits(&stLo, &stHi);   // the render thread's stack
    g_probeScanV = vrad; g_probeScanH = hrad; g_probeAuto = autoMode;
    InterlockedExchange(&g_probeScan, 1);
    RunInBackground([vrad, hrad, stLo, stHi, autoMode] {
        std::vector<ProbeCand> found;
        DWORD t0 = GetTickCount();
        ForEachRWChunk([&](uintptr_t base, const char* data, SIZE_T n) {
            const float* f = (const float*)data;
            for (SIZE_T i = 0; i < n / 4 && found.size() < 5000; i++) {
                uintptr_t a = base + i * 4;
                if (a >= stLo && a < stHi) continue;
                float v = f[i];
                if (Near(v, vrad)) found.push_back({ (float*)a, false, v });
                else if (Near(v, hrad)) found.push_back({ (float*)a, true, v });
            }
        });
        HMODULE exe = GetModuleHandleA(nullptr);
        std::vector<ProbeCand> keep;
        for (auto& c : found) {
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(c.p, &mi, sizeof mi)) continue;
            if (mi.Type == MEM_IMAGE && mi.AllocationBase != (void*)exe) continue;   // other DLLs
            keep.push_back(c);
        }
        // The camera keeps current/target/base FOV as three equal floats in a row: test those
        // first, so normally only 1-3 values are ever touched.
        auto isRun = [&](const ProbeCand& c) {
            float a, b; return ReadF(c.p - 1, &a) && ReadF(c.p + 1, &b) && Near(a, c.orig) && Near(b, c.orig);
        };
        std::stable_partition(keep.begin(), keep.end(), isRun);
        size_t runs = 0; for (auto& c : keep) if (isRun(c)) runs++;
        if (autoMode) keep.resize(runs);
        else if (keep.size() > 60) keep.resize(std::max<size_t>(runs, 60));
        Log("FOV probe: camera %.2f deg vertical; %zu candidates (%zu in a run of three), scanned in the background in %lu ms",
            vrad * 57.29578f, keep.size(), runs, GetTickCount() - t0);
        g_probeNext.swap(keep);
        InterlockedExchange(&g_probeScan, 2);
    });
}

static void FovProbeStep() {
    if (g_probeScan == 2) {                       // background scan finished: start testing
        g_probe.swap(g_probeNext); g_probeNext.clear();
        InterlockedExchange(&g_probeScan, 0);
        if (g_probe.empty()) { if (!g_probeAuto) Log("FOV probe: nothing to test"); return; }
        // the camera FOV must not have changed while we were scanning
        if (fabsf(2 * atanf(1.0f / g_gameP11) - g_probeScanV) > 1e-3f) { Log("FOV probe: camera changed during the scan - will retry"); g_probe.clear(); return; }
        Log("FOV probe: testing %zu candidates - the view may flicker briefly", g_probe.size());
        g_probeIdx = 0; g_probeFrame = 0;
    }
    if (g_probeIdx < 0) return;
    ProbeCand& c = g_probe[g_probeIdx];
    const int kHold = 8;
    if (g_probeFrame == 0) {
        g_probeBaseP11 = g_gameP11; g_probeBaseP00 = g_gameP00;
        float cur;
        if (!ReadF(c.p, &cur) || !Near(cur, c.orig)) { g_probeFrame = kHold; }   // value moved on its own: skip
    }
    if (g_probeFrame < kHold) {
        WriteF(c.p, c.orig * 1.15f);
        g_probeFrame++;
        return;
    }
    // evaluate, restore, next
    WriteF(c.p, c.orig);
    float base = c.horiz ? g_probeBaseP00 : g_probeBaseP11, now = c.horiz ? g_gameP00 : g_gameP11;
    bool follows = base > 0 && now > 0 && now < base * 0.98f;   // wider FOV -> smaller projection scale
    Log("  candidate %d/%zu %p (%s rad %.4f): projection %s (%.4f -> %.4f)", g_probeIdx + 1, g_probe.size(), (void*)c.p,
        c.horiz ? "horizontal" : "vertical", c.orig, follows ? "FOLLOWED" : "unchanged", base, now);
    if (follows) {
        g_fovAddr = c.p; g_fovAddrHoriz = c.horiz; g_fovMismatchFrames = 0;
        { float x, y; g_fovRun = ReadF(c.p - 1, &x) && ReadF(c.p + 1, &y) && Near(x, c.orig) && Near(y, c.orig); }
        SetFovBase(2 * atanf(1.0f / g_probeBaseP11), "probe");
        Log("FOV probe: found the camera FOV at %p (%s). Override ON: game camera set to %.0f deg vertical. F2/F3 adjust it.",
            (void*)g_fovAddr, c.horiz ? "horizontal" : "vertical", g_cfg.gameFov);
        g_probeIdx = -1;
        FovSavePaths();
        return;
    }
    g_probeFrame = 0;
    if (++g_probeIdx >= (int)g_probe.size()) { g_probeIdx = -1; Log("FOV probe: no candidate controls the camera (try again in normal gameplay, camera still)"); }
}

static void FovOverrideApply() {
    if (!g_fovAddr || g_probeIdx >= 0) return;
    float cur;
    if (!ReadF(g_fovAddr, &cur) || !(cur > 0.05f && cur < 3.0f)) {
        Log("FOV override: address no longer holds a FOV (%p) - paused, will re-find it automatically", (void*)g_fovAddr);
        g_fovAddr = nullptr; return;
    }
    float aspect = (g_gameP11 > 0 && g_gameP00 > 0) ? g_gameP11 / g_gameP00 : 16.0f / 9.0f;
    float v = g_cfg.gameFov / 57.29578f;
    float want = g_fovAddrHoriz ? 2 * atanf(tanf(v * 0.5f) * aspect) : v;
    WriteF(g_fovAddr, want);
    if (g_fovRun && !g_fovAddrHoriz) { WriteF(g_fovAddr - 1, want); WriteF(g_fovAddr + 1, want); }   // current/target/base
    // watchdog: the game's projection must follow; if not for 2 s, stop writing
    float expectP11 = 1.0f / tanf(v * 0.5f);
    static int episode = 0;
    bool off = g_gameP11 > 0 && fabsf(g_gameP11 - expectP11) > 0.02f * expectP11;
    if (off && !episode) Log("FOV override: game projection deviates (game %.1f deg, override %.1f deg)", 2 * atanf(1.0f / g_gameP11) * 57.29578f, g_cfg.gameFov);
    if (off) episode++; else if (episode) { Log("FOV override: back on target after %d frames", episode); episode = 0; }
    if (g_gameP11 > 0 && fabsf(g_gameP11 - expectP11) > 0.05f * expectP11) {
        if (++g_fovMismatchFrames == 180) { Log("FOV override: game camera no longer follows - paused, will re-find it automatically"); g_fovAddr = nullptr; }
    } else g_fovMismatchFrames = 0;
}

// ---- automatic FOV: pointer paths from the game exe's static data to the camera FOV ----
// Saved to SoraVR.ini [FovPaths] after a successful probe; on later launches (and after
// map changes) the address is re-found by following them, without any probing.
struct FovPath { int level; uint32_t rva; uint32_t off1, off2; };
static bool IsRunOf3(float* a, float v) { float x, y; return ReadF(a - 1, &x) && ReadF(a + 1, &y) && Near(x, v) && Near(y, v); }
static uintptr_t g_exeBase = 0, g_exeEnd = 0;
static int g_autoAttempts = 0, g_stableFrames = 0; static float g_lastP11 = 0;

static void ExeRange() {
    if (g_exeBase) return;
    g_exeBase = (uintptr_t)GetModuleHandleA(nullptr);
    auto* dos = (IMAGE_DOS_HEADER*)g_exeBase;
    auto* nt = (IMAGE_NT_HEADERS64*)(g_exeBase + dos->e_lfanew);
    g_exeEnd = g_exeBase + nt->OptionalHeader.SizeOfImage;
}
static bool ReadP(uintptr_t a, uintptr_t* v) { SIZE_T got = 0; return ReadProcessMemory(GetCurrentProcess(), (void*)a, v, 8, &got) && got == 8; }

static float* ResolvePath(const FovPath& p) {
    uintptr_t v;
    if (!ReadP(g_exeBase + p.rva, &v) || !v) return nullptr;
    if (p.level == 2) { if (!ReadP(v + p.off1, &v) || !v) return nullptr; return (float*)(v + p.off2); }
    return (float*)(v + p.off1);
}

static void FovSavePaths() {
    ExeRange();
    uintptr_t target = (uintptr_t)g_fovAddr;
    float cur = 0; ReadF(g_fovAddr, &cur);
    int run = IsRunOf3(g_fovAddr, cur) ? 1 : 0;
    RunInBackground([target, run] {
    const uintptr_t kMax = 0x4000;
    std::vector<FovPath> paths;
    // level 1: a pointer inside the exe that points at (or shortly before) the FOV
    std::vector<std::pair<uintptr_t, uint32_t>> holders;   // (address holding a pointer near target, target - pointer)
    DWORD t0 = GetTickCount();
    ForEachRWChunk([&](uintptr_t base, const char* data, SIZE_T n) {
        bool inExe = base >= g_exeBase && base < g_exeEnd;
        MEMORY_BASIC_INFORMATION mi;
        if (!inExe && VirtualQuery((void*)base, &mi, sizeof mi) && mi.Type == MEM_IMAGE) return;   // other DLLs
        const uintptr_t* q = (const uintptr_t*)data;
        for (SIZE_T i = 0; i < n / 8; i++) {
            uintptr_t v = q[i];
            if (v > target || target - v >= kMax) continue;
            uintptr_t at = base + i * 8;
            if (inExe) { if (paths.size() < 8) paths.push_back({ 1, (uint32_t)(at - g_exeBase), (uint32_t)(target - v), 0 }); }
            else if (holders.size() < 20000) holders.push_back({ at, (uint32_t)(target - v) });
        }
    });
    // level 2: a pointer inside the exe that points at (or shortly before) one of those holders
    if (paths.empty() && !holders.empty()) {
        std::sort(holders.begin(), holders.end());
        ForEachRWChunk([&](uintptr_t base, const char* data, SIZE_T n) {
            if (base < g_exeBase || base >= g_exeEnd) return;
            const uintptr_t* q = (const uintptr_t*)data;
            for (SIZE_T i = 0; i < n / 8 && paths.size() < 8; i++) {
                uintptr_t w = q[i];
                auto it = std::lower_bound(holders.begin(), holders.end(), std::make_pair(w, (uint32_t)0));
                if (it != holders.end() && it->first - w < 0x1000)
                    paths.push_back({ 2, (uint32_t)(base + i * 8 - g_exeBase), (uint32_t)(it->first - w), it->second });
            }
        });
    }
    Log("FOV paths: %zu found (%zu level-2 holders scanned, %lu ms)", paths.size(), holders.size(), GetTickCount() - t0);
    WritePrivateProfileStringA("FovPaths", nullptr, nullptr, g_ini);       // clear the old ones
    int k = 0;
    for (auto& p : paths) {
        if ((uintptr_t)ResolvePath(p) != target) continue;                 // must resolve right now
        char key[16], val[96];
        snprintf(key, sizeof key, "Path%d", k++);
        snprintf(val, sizeof val, "%d,%x,%x,%x,%d", p.level, p.rva, p.off1, p.off2, run);
        WritePrivateProfileStringA("FovPaths", key, val, g_ini);
        Log("  saved %s = level %d, exe+0x%x, +0x%x, +0x%x", key, p.level, p.rva, p.off1, p.off2);
    }
    if (!k) Log("  no stable pointer path - the automatic probe will be used on each launch instead");
    });
}

static bool FovTryPaths() {
    ExeRange();
    float vrad = 2 * atanf(1.0f / g_gameP11);
    for (int k = 0; k < 8; k++) {
        char key[16], val[96];
        snprintf(key, sizeof key, "Path%d", k);
        GetPrivateProfileStringA("FovPaths", key, "", val, sizeof val, g_ini);
        if (!val[0]) break;
        FovPath p{}; int run = 0;
        if (sscanf(val, "%d,%x,%x,%x,%d", &p.level, &p.rva, &p.off1, &p.off2, &run) < 4) continue;
        float* a = ResolvePath(p);
        float v;
        if (a && ReadF(a, &v) && Near(v, vrad) && (!run || IsRunOf3(a, v))) {
            g_fovAddr = a; g_fovAddrHoriz = false; g_fovMismatchFrames = 0; SetFovBase(vrad, "saved path"); g_fovRun = run != 0;
            Log("FOV override ON via saved %s (%p, game camera %.1f deg -> %.0f deg)", key, (void*)a, vrad * 57.29578f, g_cfg.gameFov);
            return true;
        }
    }
    return false;
}

static void FovAutoTick() {
    if (!g_cfg.autoFov || g_fovAddr || g_probeIdx >= 0 || g_probeScan || g_gameP11 <= 0 ||
        g_frame - g_lastSecondaryFrame < 120) { g_stableFrames = 0; return; }     // not while a menu camera is active
    // wait until the game's own camera FOV has been steady for 2 s (normal gameplay, not a transition)
    if (fabsf(g_gameP11 - g_lastP11) < 1e-4f) g_stableFrames++; else g_stableFrames = 0;
    g_lastP11 = g_gameP11;
    if (g_stableFrames != 180) return;
    if (FovTryPaths()) return;
    // a saved path usually just needs normal gameplay (e.g. not the title screen): keep retrying
    // it for a while before falling back to probing memory
    static int pathTries = 0;
    char p0[8]; GetPrivateProfileStringA("FovPaths", "Path0", "", p0, sizeof p0, g_ini);
    if (p0[0] && pathTries++ < 8) { g_stableFrames = -300; return; }
    if (g_autoAttempts < 6) { g_autoAttempts++; FovProbeStart(true); }
    g_stableFrames = -1200;   // if nothing worked, wait ~15 s before the next attempt
}

// ----------------------------------------------------------------------------
// Paired frames: a virtual clock in front of the game's timers. After the first frame of
// a pair is presented, the clock is turned back to the moment that frame started, so the
// game simulates the second frame (other eye) at the same moment; after the second frame
// the clock is real again, so overall the game runs at normal speed.
// Hooked through the game exe's import table: QueryPerformanceCounter, GetTickCount(64),
// timeGetTime.
// ----------------------------------------------------------------------------
using PFN_QPC = BOOL(WINAPI*)(LARGE_INTEGER*);
using PFN_DW = DWORD(WINAPI*)();
using PFN_ULL = ULONGLONG(WINAPI*)();
static PFN_QPC r_QPC = nullptr; static PFN_DW r_GetTickCount = nullptr, r_timeGetTime = nullptr; static PFN_ULL r_GetTickCount64 = nullptr;
static volatile LONGLONG g_clockBack = 0;      // QPC ticks to subtract from the game's clock
static LONGLONG g_qpcFreq = 1, g_lastPresentQpc = 0;
static LONG g_clockCalls = 0;
static bool g_freezeTest = false; static LONGLONG g_freezeAt = 0;

static BOOL WINAPI Hook_QPC(LARGE_INTEGER* p) {
    BOOL r = r_QPC(p);
    if (r && p) p->QuadPart -= g_clockBack;
    g_clockCalls++;
    return r;
}
static DWORD WINAPI Hook_GetTickCount() { return r_GetTickCount() - (DWORD)(g_clockBack * 1000 / g_qpcFreq); }
static ULONGLONG WINAPI Hook_GetTickCount64() { return r_GetTickCount64() - (ULONGLONG)(g_clockBack * 1000 / g_qpcFreq); }
static DWORD WINAPI Hook_timeGetTime() { return r_timeGetTime() - (DWORD)(g_clockBack * 1000 / g_qpcFreq); }

static int PatchImport(HMODULE mod, const char* fn, void* hook) {
    auto* base = (uint8_t*)mod;
    auto* nt = (IMAGE_NT_HEADERS64*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int n = 0;
    for (auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
        if (!d->OriginalFirstThunk) continue;
        auto* names = (IMAGE_THUNK_DATA64*)(base + d->OriginalFirstThunk);
        auto* iat = (IMAGE_THUNK_DATA64*)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto* ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, fn) != 0) continue;
            DWORD old; VirtualProtect(&iat->u1.Function, 8, PAGE_READWRITE, &old);
            iat->u1.Function = (ULONGLONG)hook;
            VirtualProtect(&iat->u1.Function, 8, old, &old);
            n++;
        }
    }
    return n;
}

static void InstallClockHooks() {
    static bool done = false;
    if (done) return;
    done = true;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    r_QPC = (PFN_QPC)GetProcAddress(k32, "QueryPerformanceCounter");
    r_GetTickCount = (PFN_DW)GetProcAddress(k32, "GetTickCount");
    r_GetTickCount64 = (PFN_ULL)GetProcAddress(k32, "GetTickCount64");
    HMODULE wm = LoadLibraryA("winmm.dll");
    r_timeGetTime = wm ? (PFN_DW)GetProcAddress(wm, "timeGetTime") : nullptr;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcFreq = f.QuadPart ? f.QuadPart : 1;
    HMODULE exe = GetModuleHandleA(nullptr);
    int a = PatchImport(exe, "QueryPerformanceCounter", (void*)Hook_QPC);
    int b = PatchImport(exe, "GetTickCount", (void*)Hook_GetTickCount);
    int c = PatchImport(exe, "GetTickCount64", (void*)Hook_GetTickCount64);
    int d = r_timeGetTime ? PatchImport(exe, "timeGetTime", (void*)Hook_timeGetTime) : 0;
    Log("paired frames: game clock hooks - QueryPerformanceCounter %d, GetTickCount %d, GetTickCount64 %d, timeGetTime %d", a, b, c, d);
}

static void PairClockAtPresent() {
    if (!r_QPC) return;
    LARGE_INTEGER now; r_QPC(&now);
    bool on = g_cfg.paired && g_cfg.enabled && g_cfg.mode == 0 && !g_dualActive;
    if (g_freezeTest) {                                   // test: game clock stands still
        if (!g_freezeAt) g_freezeAt = now.QuadPart;
        g_clockBack = (LONGLONG)((now.QuadPart - g_freezeAt) * 0.99);   // crawls at 1% so waits can't hang
        g_lastPresentQpc = now.QuadPart;
        return;
    }
    g_freezeAt = 0;
    if (on && CurrentEye() < 0) g_clockBack = now.QuadPart - g_lastPresentQpc;   // first of the pair done: rewind
    else g_clockBack = 0;                                                       // pair done: real time again
    if (g_clockBack < 0 || g_clockBack > g_qpcFreq / 5) g_clockBack = 0;          // ignore stalls > 200 ms
    g_lastPresentQpc = now.QuadPart;
    static LONG last = 0;
    if (g_frame % 600 == 0) { Log("paired frames: game read the clock %ld times in the last 600 frames", g_clockCalls - last); last = g_clockCalls; }
}

// ----------------------------------------------------------------------------
// Present hook (IDXGISwapChain::Present = 8, IDXGISwapChain1::Present1 = 22)
// ----------------------------------------------------------------------------
using PFN_Present  = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using PFN_Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static PFN_Present  o_Present = nullptr;
static PFN_Present1 o_Present1 = nullptr;

static bool g_liveToggleReq = false;
static bool KeyPressed(int vk) {
    static bool down[256];
    bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool fire = now && !down[vk & 255];
    down[vk & 255] = now;
    return fire;
}

static void OnFrameEnd() {
    if (KeyPressed(g_cfg.keyPaired) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
        g_cfg.dual = !g_cfg.dual;
        Log("dual render %s", g_cfg.dual ? "ON (both eyes rendered every frame)" : "OFF (alternate-eye)");
        SaveConfig();
    }
    {   bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        static bool f10 = false, f11 = false;
        bool d10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0, d11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (ctrl && d10 && !f10) g_liveToggleReq = true;     // Ctrl+F10: live world behind menus (step 9a; was the 5b clock log)
        {   // Ctrl+PgUp/PgDn: controller physics sphere bigger/smaller; Ctrl+Home/End: sphere forward/back
            static bool pu, pd, ph, pe;
            bool du = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0, dd = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
            bool dh = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0, de = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
            bool ch = false;
            static bool pi;
            bool di = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
            if (ctrl && di && !pi) { g_cfg.physDebug = !g_cfg.physDebug; Log("controller physics: debug sphere %s", g_cfg.physDebug ? "ON" : "OFF"); }
            pi = di;
            if (ctrl && du && !pu) { g_cfg.physRadius += 0.005f; ch = true; }
            if (ctrl && dd && !pd) { g_cfg.physRadius -= 0.005f; ch = true; }
            if (ctrl && dh && !ph) { g_cfg.physOffset += 0.005f; ch = true; }
            if (ctrl && de && !pe) { g_cfg.physOffset -= 0.005f; ch = true; }
            pu = du; pd = dd; ph = dh; pe = de;
            if (ch) {
                char v[32];
                snprintf(v, sizeof v, "%.3f", g_cfg.physRadius); WritePrivateProfileStringA("Controllers", "PhysicsExtraRadius", v, g_ini);
                snprintf(v, sizeof v, "%.3f", g_cfg.physOffset); WritePrivateProfileStringA("Controllers", "PhysicsOffset", v, g_ini);
                Log("controller physics: sphere radius %.3f m (orb %.3f + extra %.3f), offset %.3f m along the pointing direction",
                    g_cfg.orbSize * 0.5f + g_cfg.physRadius, g_cfg.orbSize * 0.5f, g_cfg.physRadius, g_cfg.physOffset);
            }
        }
        {   // Ctrl+F5/F6: menu model depth (separation) -/+; Ctrl+F7/F8: menu model further away / closer (convergence)
            static bool p5, p6, p7, p8;
            bool d5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0, d6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            bool d7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0, d8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
            bool ch = false;
            if (ctrl && d5 && !p5) { g_cfg.menuModelSep = g_cfg.menuModelSep > 0.015f ? g_cfg.menuModelSep - 0.01f : 0.005f; ch = true; }
            if (ctrl && d6 && !p6) { g_cfg.menuModelSep += 0.01f; ch = true; }
            if (ctrl && d7 && !p7) { g_cfg.menuModelConv /= 1.15f; ch = true; }
            if (ctrl && d8 && !p8) { g_cfg.menuModelConv *= 1.15f; ch = true; }
            p5 = d5; p6 = d6; p7 = d7; p8 = d8;
            if (ch) {
                char v[32];
                snprintf(v, sizeof v, "%.3f", g_cfg.menuModelSep); WritePrivateProfileStringA("HUD", "MenuModelSeparation", v, g_ini);
                snprintf(v, sizeof v, "%.2f", g_cfg.menuModelConv); WritePrivateProfileStringA("HUD", "MenuModelConvergence", v, g_ini);
                Log("menu models: separation %.3f, convergence %.2f (smaller = further back)", g_cfg.menuModelSep, g_cfg.menuModelConv);
            }
        }
        {   static bool p12; bool d12 = (GetAsyncKeyState(g_cfg.keyFovLock) & 0x8000) != 0;
            if (ctrl && d12 && !p12) FovProbeStart();                 // Ctrl+F12: search the game's FOV by hand
            p12 = d12; }
        if (ctrl && d11 && !f11) { g_freezeTest = !g_freezeTest; Log("clock freeze test %s - if NPCs/animations keep moving while frozen, the game does not run on this clock", g_freezeTest ? "ON" : "OFF"); }
        f10 = d10; f11 = d11;
        if (ctrl) goto skipPlainKeys;      // Ctrl+key combos above, don't also trigger the plain keys
    }
    if (KeyPressed(g_cfg.keyToggle)) { g_cfg.enabled = !g_cfg.enabled; Log("stereo %s", g_cfg.enabled ? "ON" : "OFF"); SaveConfig(); }
    if (KeyPressed(g_cfg.keyMode)) {
        g_cfg.mode = (g_cfg.mode + 1) % 3;
        static const char* names[] = { "alternate", "left only", "right only" };
        Log("mode: %s", names[g_cfg.mode]); SaveConfig();
    }
    if (KeyPressed(g_cfg.keySepDown)) { g_cfg.worldScale *= 0.9f;  g_cfg.separation *= 0.9f;  Log("world scale %.4f (eye separation x%.3f)", g_cfg.worldScale, g_cfg.worldScale); SaveConfig(); }
    if (KeyPressed(g_cfg.keySepUp))   { g_cfg.worldScale /= 0.9f;  g_cfg.separation /= 0.9f;  Log("world scale %.4f (eye separation x%.3f)", g_cfg.worldScale, g_cfg.worldScale); SaveConfig(); }
    if (KeyPressed(g_cfg.keyRecenter)) { xr::Recenter(); Log("recenter requested"); }
    if (KeyPressed(g_cfg.keyTracking)) { g_cfg.headTracking = !g_cfg.headTracking; Log("head tracking %s", g_cfg.headTracking ? "ON" : "OFF"); SaveConfig(); }
    if (KeyPressed(g_cfg.keyLevel))    { g_cfg.levelHorizon = !g_cfg.levelHorizon; Log("level horizon %s", g_cfg.levelHorizon ? "ON" : "OFF"); SaveConfig(); }
    if (FovOverrideActive() || g_cfg.fovLock) {   // F2/F3 change the (locked / overridden) FOV
        if (KeyPressed(g_cfg.keyFovDown)) { g_cfg.gameFov = fmaxf(20.0f, g_cfg.gameFov - 2.0f); Log("game FOV %.0f deg (vertical)", g_cfg.gameFov); SaveConfig(); }
        if (KeyPressed(g_cfg.keyFovUp))   { g_cfg.gameFov = fminf(140.0f, g_cfg.gameFov + 2.0f); Log("game FOV %.0f deg (vertical)", g_cfg.gameFov); SaveConfig(); }
    } else {
        if (KeyPressed(g_cfg.keyFovDown))  { g_cfg.fovScale = fmaxf(0.5f, g_cfg.fovScale - 0.05f); Log("fov scale %.2f", g_cfg.fovScale); SaveConfig(); }
        if (KeyPressed(g_cfg.keyFovUp))    { g_cfg.fovScale = fminf(2.5f, g_cfg.fovScale + 0.05f); Log("fov scale %.2f", g_cfg.fovScale); SaveConfig(); }
    }
    if (KeyPressed(g_cfg.keyHud))      { g_cfg.hud = !g_cfg.hud; Log("HUD panel %s", g_cfg.hud ? "ON" : "OFF (UI drawn into the scene)"); SaveConfig(); }
    if (KeyPressed(g_cfg.keyHudLock))  { g_cfg.hudHeadLock = !g_cfg.hudHeadLock; Log("HUD %s", g_cfg.hudHeadLock ? "head-locked" : "room-locked"); SaveConfig(); }
    if (KeyPressed(g_cfg.keyHudNear))  { g_cfg.hudDistance = fmaxf(0.3f, g_cfg.hudDistance * 0.9f); Log("HUD distance %.2f m", g_cfg.hudDistance); SaveConfig(); }
    if (KeyPressed(g_cfg.keyHudFar))   { g_cfg.hudDistance = fminf(20.0f, g_cfg.hudDistance / 0.9f); Log("HUD distance %.2f m", g_cfg.hudDistance); SaveConfig(); }
    if (KeyPressed(g_cfg.keyHudBig))   { g_cfg.hudWidth = fminf(20.0f, g_cfg.hudWidth / 0.9f); Log("HUD width %.2f m", g_cfg.hudWidth); SaveConfig(); }
    if (KeyPressed(g_cfg.keyHudSmall)) { g_cfg.hudWidth = fmaxf(0.2f, g_cfg.hudWidth * 0.9f); Log("HUD width %.2f m", g_cfg.hudWidth); SaveConfig(); }
    if (KeyPressed(g_cfg.keyFovLock)) {
        { g_cfg.fovLock = !g_cfg.fovLock; Log("FOV lock %s (%.0f deg)", g_cfg.fovLock ? "ON" : "OFF - the game's own FOV changes come through", g_cfg.gameFov); SaveConfig(); }
    }
    UiHuntKeys(KeyPressed);
    if (KeyPressed(g_cfg.keySwapEyes)) { g_cfg.swapEyes = !g_cfg.swapEyes; Log("swap eyes %s", g_cfg.swapEyes ? "ON" : "OFF"); SaveConfig(); }

skipPlainKeys:
    // per-frame work: must run every frame (until v1.0 it sat before this label, so holding Ctrl
    // for a hotkey paused the FOV upkeep, and Ctrl+F12 could never reach the probe)
    SceneFrameEnd();
    FovAutoTick();
    FovProbeStep();
    FovOverrideApply();
    PairClockAtPresent();
    LONG f = InterlockedIncrement(&g_frame);
    if (f == 1500) { g_timeRecN = 0; }   // one automatic game-time log ~15 s in

    // inventory capture: one full frame, on F9 and automatically at a few points
    if (g_capture) { if (InterlockedDecrement(&g_capture) == 0) { DumpInventory(f); DumpSequence(); } }
    else if (KeyPressed(VK_F9) && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
        { std::lock_guard<std::mutex> lk(g_invMx); g_inv.clear(); g_capExec = 0; g_capCreateCB = 0; }
        g_seq.clear(); g_dumped2800 = false; g_curRT = nullptr; g_curDSV = nullptr;
        Log("inventory capture starts (frame %ld)", f);
        static const char* watch[] = { "amd_ags_x64.dll", "nvapi64.dll", "d3d12.dll", "d3d11on12.dll", "GameOverlayRenderer64.dll", "RTSSHooks64.dll" };
        for (const char* m : watch) if (GetModuleHandleA(m)) Log("  module loaded: %s", m);
        g_capture = 1;
    }

    if (f % 600 == 0) {
        char buf[512] = ""; static LONG last[C_Count];
        for (int i = 0; i < C_Count; i++) {
            LONG v = g_calls[i];
            if (v != last[i]) { char t[64]; snprintf(t, sizeof t, " %s=%ld", kCallNames[i], (long)((v - last[i]) / 600)); strncat(buf, t, sizeof buf - strlen(buf) - 1); }
            last[i] = v;
        }
        Log("calls per frame:%s", buf[0] ? buf : " (none)");
        for (auto& c : g_copyStats) {
            Log("camera copy %u-byte buffer +%u (%s): patched %d times in the last 600 frames%s", c.bytes, c.off, kCopyKind[c.kind], c.count,
                c.count < 500 ? "  <- not every frame (uploaded before cb_scene? only matches while the camera is still)" : "");
            c.count = 0;
        }
        LONG m = g_sceneMatches;
        Log("frame %ld: cb_scene updates last 600 frames = %ld (rejected look-alikes total %ld)",
            f, m - g_sceneMatchesLastReport, g_sceneRejects);
        g_sceneMatchesLastReport = m;
    }
}

// ---- drawing our own things in the middle of the game's frame ----
// Save the game's state by hand and restore it afterwards. (SwapDeviceContextState must NOT be used:
// it swaps the context's function table, which silently removed every SoraVR hook in step 7a.)
// All hooked methods are called through ORIG so the mod's own state tracking doesn't see our draws.
struct StateGuard {
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {}; ID3D11DepthStencilView* dsv = nullptr;
    UINT nvp = 0; D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11BlendState* bs = nullptr; float bf[4] = {}; UINT sm = 0;
    ID3D11DepthStencilState* dss = nullptr; UINT sref = 0;
    ID3D11RasterizerState* rs = nullptr; ID3D11InputLayout* il = nullptr; D3D11_PRIMITIVE_TOPOLOGY topo = {};
    ID3D11ClassInstance* vci[256]; UINT nvci = 256; ID3D11VertexShader* vs = nullptr;
    ID3D11ClassInstance* pci[256]; UINT npci = 256; ID3D11PixelShader* ps = nullptr;
    ID3D11GeometryShader* gs = nullptr; ID3D11HullShader* hs = nullptr; ID3D11DomainShader* ds = nullptr;
    ID3D11Buffer* vcb = nullptr; ID3D11Buffer* pcb = nullptr;
    ID3D11ShaderResourceView* srv[2] = {}; ID3D11SamplerState* smp = nullptr;
    void Save(ID3D11DeviceContext* c) {
        c->PSGetShaderResources(0, 2, srv); c->PSGetSamplers(0, 1, &smp);
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&nvp, vps);
        c->OMGetBlendState(&bs, bf, &sm); c->OMGetDepthStencilState(&dss, &sref);
        c->RSGetState(&rs); c->IAGetInputLayout(&il); c->IAGetPrimitiveTopology(&topo);
        c->VSGetShader(&vs, vci, &nvci); c->PSGetShader(&ps, pci, &npci);
        c->GSGetShader(&gs, nullptr, nullptr); c->HSGetShader(&hs, nullptr, nullptr); c->DSGetShader(&ds, nullptr, nullptr);
        c->VSGetConstantBuffers(0, 1, &vcb); c->PSGetConstantBuffers(0, 1, &pcb);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->PSSetShaderResources(0, 2, srv); c->PSSetSamplers(0, 1, &smp);
        for (auto* v : srv) if (v) v->Release();
        if (smp) smp->Release();
        ORIG(c, I_VSSetCB, PFN_SetCB)(c, 0, 1, &vcb);
        ORIG(c, I_PSSetCB, PFN_SetCB)(c, 0, 1, &pcb);
        ORIG(c, I_VSSetShader, PFN_SetShader)(c, vs, (void* const*)vci, nvci);
        ORIG(c, I_PSSetShader, PFN_SetShader)(c, ps, (void* const*)pci, npci);
        c->GSSetShader(gs, nullptr, 0); c->HSSetShader(hs, nullptr, 0); c->DSSetShader(ds, nullptr, 0);
        c->IASetPrimitiveTopology(topo); c->IASetInputLayout(il); c->RSSetState(rs);
        c->OMSetDepthStencilState(dss, sref); c->OMSetBlendState(bs, bf, sm);
        ORIG(c, I_OMSetRenderTargets, PFN_OMSetRT)(c, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        if (nvp) ORIG(c, I_RSSetViewports, PFN_RSSetViewports)(c, nvp, vps);
        for (auto* r : rtv) if (r) r->Release();
        if (dsv) dsv->Release();
        for (UINT i = 0; i < nvci; i++) if (vci[i]) vci[i]->Release();
        for (UINT i = 0; i < npci; i++) if (pci[i]) pci[i]->Release();
        IUnknown* held[] = { bs, dss, rs, il, vs, ps, gs, hs, ds, vcb, pcb };
        for (auto* h : held) if (h) h->Release();
    }
};
// one render target, no depth, no culling, triangle strip from SV_VertexID
static void PrepSimpleDraw(ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv, const D3D11_VIEWPORT& vp, ID3D11BlendState* blend) {
    ORIG(c, I_RSSetViewports, PFN_RSSetViewports)(c, 1, &vp);
    ORIG(c, I_OMSetRenderTargets, PFN_OMSetRT)(c, 1, &rtv, nullptr);
    c->OMSetBlendState(blend, nullptr, 0xffffffff);
    c->OMSetDepthStencilState(nullptr, 0);
    c->RSSetState(nullptr);
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
}

// ---- mouse cursor on the HUD panel + controller laser pointer ----
// The game uses the Windows cursor, which Windows draws on the monitor only, so it never reaches
// the headset. We draw a ring cursor onto the HUD panel at the matching spot, and a controller
// pointed at the panel moves the Windows cursor there (trigger = left click, grip = right click).
// Both only while the game shows a cursor, so they never interfere with mouse camera control.
static ID3D11VertexShader* g_crVS = nullptr; static ID3D11PixelShader* g_crPS = nullptr;
static ID3D11BlendState* g_crBlend = nullptr; static ID3D11Buffer* g_crCB = nullptr;
static bool g_crReady = false; static bool g_crInitTried = false;
static HWND g_gameHwnd = nullptr;
static const char* kCursorHlsl = R"HLSL(
cbuffer C : register(b0) { float4 rect; };
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 t = float2(id & 1, id >> 1);
    o.p = float4(rect.x + t.x * rect.z, rect.y - t.y * rect.w, 0, 1); o.uv = t; return o;
}
float4 ps(O i) : SV_Target {
    float r = length(i.uv * 2 - 1);
    float white = max(smoothstep(0.55, 0.62, r) * (1 - smoothstep(0.76, 0.83, r)), 1 - smoothstep(0.12, 0.19, r));
    float dark  = smoothstep(0.45, 0.53, r) * (1 - smoothstep(0.86, 0.95, r));
    float a = max(white, dark * 0.85);
    return float4(white.xxx * a, a);            // premultiplied alpha
}
)HLSL";

static bool CompileHlsl(const char* src, const char* entry, const char* target, ID3DBlob** out) {
    static pD3DCompile compile = nullptr;
    if (!compile) { HMODULE m = LoadLibraryA("d3dcompiler_47.dll"); if (m) compile = (pD3DCompile)GetProcAddress(m, "D3DCompile"); }
    if (!compile) return false;
    ID3DBlob* err = nullptr;
    HRESULT hr = compile(src, strlen(src), "soravr", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    if (FAILED(hr)) Log("shader compile failed (%s): %s", entry, err ? (const char*)err->GetBufferPointer() : "?");
    if (err) err->Release();
    return SUCCEEDED(hr);
}

static bool CursorInit(ID3D11DeviceContext* c) {
    if (g_crReady) return true;
    if (g_crInitTried) return false;
    g_crInitTried = true;
    ID3D11Device* dev = nullptr; c->GetDevice(&dev);
    ID3D11Device1* dev1 = nullptr;
    ID3DBlob *vb = nullptr, *pb = nullptr;
    bool ok = SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev1)) &&
              CompileHlsl(kCursorHlsl, "vs", "vs_5_0", &vb) && CompileHlsl(kCursorHlsl, "ps", "ps_5_0", &pb) &&
              SUCCEEDED(o_CreateVS(dev, vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &g_crVS)) &&
              SUCCEEDED(o_CreatePS(dev, pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &g_crPS));
    if (ok) {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_BUFFER_DESC cbd = {}; cbd.ByteWidth = 16; cbd.Usage = D3D11_USAGE_DEFAULT; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ok = SUCCEEDED(dev->CreateBlendState(&bd, &g_crBlend)) && SUCCEEDED(dev->CreateBuffer(&cbd, nullptr, &g_crCB));
        g_crReady = ok;
    }
    if (vb) vb->Release();
    if (pb) pb->Release();
    if (dev1) dev1->Release();
    dev->Release();
    Log("HUD cursor: %s", ok ? "ready" : "could not be set up (cursor won't show on the panel)");
    return ok;
}

static bool GameCursorPos(float* u, float* v) {        // Windows cursor inside the game window, 0..1
    if (!g_gameHwnd) return false;
    CURSORINFO ci = { sizeof ci };
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING)) return false;
    POINT pt; RECT rc;
    if (!GetCursorPos(&pt) || !ScreenToClient(g_gameHwnd, &pt) || !GetClientRect(g_gameHwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
    *u = (float)pt.x / rc.right; *v = (float)pt.y / rc.bottom;
    return *u >= 0 && *u <= 1 && *v >= 0 && *v <= 1;
}

static void DrawCursorOnPanel(ID3D11DeviceContext* c) {
    if (!g_cfg.hud || !g_cfg.panelCursor || !g_uiRTV || !g_uiTex || !g_uiDrawnThisFrame) return;
    float u, v;
    if (!GameCursorPos(&u, &v) || !CursorInit(c)) return;
    D3D11_TEXTURE2D_DESC td; g_uiTex->GetDesc(&td);
    float hN = 0.045f * 2.0f, wN = hN * (float)td.Height / (float)td.Width;     // cursor 4.5% of panel height
    float rect[4] = { u * 2 - 1 - wN * 0.5f, 1 - v * 2 + hN * 0.5f, wN, hN };
    StateGuard g; g.Save(c);
    using PFN_UpdSub = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
    ORIG(c, I_UpdateSubresource, PFN_UpdSub)(c, g_crCB, 0, nullptr, rect, 0, 0);
    D3D11_VIEWPORT vp = { 0, 0, (float)td.Width, (float)td.Height, 0, 1 };
    ID3D11RenderTargetView* targets[2] = { g_uiRTV, g_panelP0Valid ? g_uiRTVP0 : nullptr };   // both eyes' panels
    for (auto* t : targets) {
        if (!t) continue;
        PrepSimpleDraw(c, t, vp, g_crBlend);
        ORIG(c, I_VSSetShader, PFN_SetShader)(c, g_crVS, nullptr, 0);
        ORIG(c, I_PSSetShader, PFN_SetShader)(c, g_crPS, nullptr, 0);
        ORIG(c, I_VSSetCB, PFN_SetCB)(c, 0, 1, &g_crCB);
        ORIG(c, I_Draw, PFN_Draw)(c, 4, 0);
    }
    g.Restore(c);
}

// Controllers are drawn as camera-facing quad layers in xr.cpp (step 9e); see xr::ControllerOptions.

static void SendMouse(DWORD flag) { INPUT in = {}; in.type = INPUT_MOUSE; in.mi.dwFlags = flag; SendInput(1, &in, sizeof in); }

static void ApplyPointer() {
    static bool downL = false, downR = false;
    xr::PointerState ps{};
    CURSORINFO ci = { sizeof ci };
    bool cursorShown = GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);
    bool focused = g_gameHwnd && GetForegroundWindow() == g_gameHwnd;
    xr::SetPointerActive(g_cfg.pointer && cursorShown && focused);
    bool hit = xr::GetPointer(&ps);
    bool active = g_cfg.pointer && cursorShown && focused && hit;
    static int lastWhy = -1;                       // log why the pointer is blocked, on change
    int why = !hit ? 0 : !g_cfg.pointer ? 1 : !cursorShown ? 2 : !focused ? 3 : 4;
    if (why != lastWhy && why != 0) {
        static const char* k[] = { "", "ControllerPointer=0 in the ini", "the game shows no cursor", "the game window is not in focus", "moving the cursor" };
        Log("controller pointer on the panel: %s", k[why]);
    }
    lastWhy = why;
    if (active) {
        RECT rc; GetClientRect(g_gameHwnd, &rc);
        POINT pt{ (LONG)(ps.u * rc.right), (LONG)(ps.v * rc.bottom) };
        ClientToScreen(g_gameHwnd, &pt);
        // The game reads mouse MOVEMENT through raw input, which never sees SetCursorPos. While a button
        // is held (dragging, e.g. turning a 3D model in a menu), also send the movement as a relative
        // mouse move, then put the cursor exactly on the beam (acceleration may have moved it differently).
        POINT cur;
        if ((downL || downR) && GetCursorPos(&cur) && (pt.x != cur.x || pt.y != cur.y)) {
            INPUT in = {}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_MOVE; in.mi.dx = pt.x - cur.x; in.mi.dy = pt.y - cur.y;
            SendInput(1, &in, sizeof in);
        }
        SetCursorPos(pt.x, pt.y);
    }
    bool wantL = active && ps.click, wantR = active && ps.rightClick;
    if (wantL != downL) { SendMouse(wantL ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP); downL = wantL; }
    if (wantR != downR) { SendMouse(wantR ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP); downR = wantR; }
}

// ---- game code signatures ----
// The game is patched at a few places in its code. Instead of fixed addresses (which move with every
// game update) each place is found by a byte pattern ("??" = any byte: addresses and offsets that
// differ between builds) that must match exactly once in the exe's code. Checked offline against
// 1.06.1 and 1.07 with tools/sigcheck.py, which reads the patterns from this file (keep the format).
static const char kSigGameObj[]  = "48 8b 35 ?? ?? ?? ?? 48 8b ce e8 ?? ?? ?? ?? 40 38 ae b8 00 00 00 75 10";   // mov rsi,[game object A] (frame loop)
static const char kSigRenderCall[] = "48 8b 06 48 8b ce ff 50 60 8b c5 86 83 ?? 00 00 00 48 8b 8b ?? 00 00 00"; // call [rax+60h] = render (vtable slot 12)
static const char kSigMenuEarly[] = "49 8b 8e ?? ?? 00 00 48 8b 41 10 f3 44 0f 10 ?? ?? ?? ?? ?? 80 78 43 00 0f 84 ?? ?? ?? ?? f3 0f 10 81 c4 00 00 00 41 0f 2f"; // capture valid -> draw only the capture
static const char kSigMenuLate[]  = "f3 0f 10 81 c4 00 00 00 41 0f 2f ?? 76 ?? 48 8b 41 10 80 78 43 00 74";   // blend > 0 -> composite the capture
static const char kSigTitleR[]    = "48 8b 1d ?? ?? ?? ?? 4c 89 ac 24 e0 00 00 00 0f 29 b4 24 90 00 00 00 0f 29 bc 24 80 00 00 00 0f b6 83 a5 1b 00 00 84 c0 0f 84 ?? ?? ?? ??"; // mov rbx,[game object B]; "draw world" byte reader (render)
static const char kSigTitleP[]    = "0f b6 86 a5 1b 00 00 48 8b 8e 98 06 00 00 84 c0 75 ??";                 // "draw world" byte reader (prepare)
static const char kSigPhys1[]     = "4c 8b 8f d0 01 00 00 4d 8b c6 0f 28 ce e8 ?? ?? ?? ?? 48 83 c3 08 48 3b de 0f 85";       // call DynamicBone chain update
static const char kSigPhys2[]     = "4c 8b 8f d0 01 00 00 4d 8b c6 48 8b 0b 0f 28 ce e8 ?? ?? ?? ?? 48 83 c3 08 48 3b de 75"; // call DynamicBone chain update

static uintptr_t SigFind(const char* name, const char* pat) {
    uint8_t b[128]; bool any[128]; int n = 0;
    for (const char* s = pat; *s && n < 128; ) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (s[0] == '?') { any[n] = true; b[n++] = 0; s += 2; continue; }
        any[n] = false; b[n++] = (uint8_t)strtoul(std::string(s, 2).c_str(), nullptr, 16); s += 2;
    }
    uintptr_t exe = (uintptr_t)GetModuleHandleA(nullptr);
    auto* nt = (IMAGE_NT_HEADERS*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t hit = 0; int count = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* p = (const uint8_t*)(exe + sec->VirtualAddress);
        size_t len = sec->Misc.VirtualSize;
        for (size_t o = 0; o + n <= len; o++) {
            if (p[o] != b[0]) continue;
            int k = 1;
            while (k < n && (any[k] || p[o + k] == b[k])) k++;
            if (k == n) { if (!count++) hit = (uintptr_t)(p + o); }
        }
    }
    if (count != 1) { Log("game code: '%s' %s (%d matches) - this game version is not recognised there", name, count ? "ambiguous" : "not found", count); return 0; }
    return hit;
}
static uintptr_t RipTarget(uintptr_t insn, int dispAt, int len) { return insn + len + *(int32_t*)(insn + dispAt); }

// Everything the mod patches or reads in the game, resolved once at startup (before any patch).
struct GameAddrs {
    uintptr_t objA = 0;           // global: game object A (vtable slot 12 = render), was exe+905870
    uintptr_t objB = 0;           // global: game object B (+0x698 scene renderer, +0x1ba5 "draw world"), was exe+908db8
    uint32_t captureOff = 0;      // scene renderer -> screen-capture object (0x8b8 in 1.06.1, 0x910 in 1.07)
    uintptr_t menuEarly = 0, menuLate = 0;     // menu patches (je / jbe)
    uint8_t earlyOrig[6], earlyPatch[6], lateOrig[2], latePatch[2];
    uintptr_t titleR = 0, titleP = 0;          // title patches (je / jne)
    uint8_t trOrig[6], trPatch[6], tpOrig[2], tpPatch[2];
    uintptr_t phys[2] = { 0, 0 }, physTarget = 0;
};
static GameAddrs g_ga;
static BOOL CALLBACK ResolveGameOnce(PINIT_ONCE, PVOID, PVOID*) {
    uintptr_t exe = (uintptr_t)GetModuleHandleA(nullptr);
    GameAddrs& g = g_ga;
    {   auto* nt = (IMAGE_NT_HEADERS*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
        Log("game exe: build stamp %08lx, image size 0x%lx", (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)nt->OptionalHeader.SizeOfImage); }
    if (uintptr_t m = SigFind("render: game object", kSigGameObj)) {
        uintptr_t c = SigFind("render: render call", kSigRenderCall);
        if (c > m && c - m < 0x200) g.objA = RipTarget(m, 3, 7);
        else if (c) Log("game code: render call not next to the game object load - not recognised");
    }
    if (uintptr_t m = SigFind("menus: early exit", kSigMenuEarly)) {
        g.captureOff = *(uint32_t*)(m + 3);
        g.menuEarly = m + 24;
        memcpy(g.earlyOrig, (void*)g.menuEarly, 6);                    // je rel32 -> jmp rel32+1; nop (same target)
        g.earlyPatch[0] = 0xe9; *(int32_t*)(g.earlyPatch + 1) = *(int32_t*)(g.menuEarly + 2) + 1; g.earlyPatch[5] = 0x90;
    }
    if (uintptr_t m = SigFind("menus: late composite", kSigMenuLate)) {
        g.menuLate = m + 12;
        memcpy(g.lateOrig, (void*)g.menuLate, 2);                      // jbe -> jmp
        g.latePatch[0] = 0xeb; g.latePatch[1] = g.lateOrig[1];
    }
    if (uintptr_t m = SigFind("title: render reader", kSigTitleR)) {
        g.objB = RipTarget(m, 3, 7);
        g.titleR = m + 0x28;
        memcpy(g.trOrig, (void*)g.titleR, 6); memset(g.trPatch, 0x90, 6);  // je -> nops
    }
    if (uintptr_t m = SigFind("title: prepare reader", kSigTitleP)) {
        g.titleP = m + 16;
        memcpy(g.tpOrig, (void*)g.titleP, 2);                          // jne -> jmp
        g.tpPatch[0] = 0xeb; g.tpPatch[1] = g.tpOrig[1];
    }
    // The physics hook swaps the game's collision modifier object for ours, which only works if its
    // interface is the one 1st Chapter uses. 2nd Chapter matches the call sites but crashed on the
    // first physics update, so it is only enabled for sora_1st.exe.
    char exePath[MAX_PATH] = ""; GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const char* exeName = strrchr(exePath, '\\'); exeName = exeName ? exeName + 1 : exePath;
    bool firstChapter = _stricmp(exeName, "sora_1st.exe") == 0;
    Log("game exe: %s", exeName);
    uintptr_t p1 = firstChapter ? SigFind("physics: call 1", kSigPhys1) : 0, p2 = firstChapter ? SigFind("physics: call 2", kSigPhys2) : 0;
    if (!firstChapter) Log("game code: controller physics is only supported in sora_1st.exe - off");
    if (p1 && p2) {
        uintptr_t s1 = p1 + 13, s2 = p2 + 16, t1 = RipTarget(s1, 1, 5), t2 = RipTarget(s2, 1, 5);
        static const uint8_t prolog[] = { 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18 };
        if (t1 == t2 && !memcmp((void*)t1, prolog, sizeof prolog)) { g.phys[0] = s1; g.phys[1] = s2; g.physTarget = t1; }
        else Log("game code: physics calls do not lead to the expected function - not recognised");
    }
    auto r = [&](uintptr_t a) { return a ? (unsigned long long)(a - exe) : 0ull; };
    Log("game code: objA exe+%llx, objB exe+%llx, capture +0x%x, menus exe+%llx/%llx, title exe+%llx/%llx, physics exe+%llx/%llx -> exe+%llx",
        r(g.objA), r(g.objB), g.captureOff, r(g.menuEarly), r(g.menuLate), r(g.titleR), r(g.titleP), r(g.phys[0]), r(g.phys[1]), r(g.physTarget));
    return TRUE;
}
static const GameAddrs& Game() {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, ResolveGameOnce, nullptr, nullptr);
    return g_ga;
}

// ---- menu blur (step 11a) ----
// Since 9c the world is drawn live behind menus, sharp. The game still runs its menu fade (the
// screen-capture object: game object B +0x698 -> scene renderer, +captureOff -> capture, +0xc4 = blend
// 0..1), so we blur + dim each eye image by that amount: quarter-size copy, 2x separable Gaussian,
// mixed back in. Done after each eye's render pass, before it goes to the headset (monitor unchanged).
static float MenuBlendAmount() {
    const GameAddrs& ga = Game();
    if (!ga.objB || !ga.captureOff) return 0;
    SIZE_T got = 0; HANDLE pr = GetCurrentProcess();
    uintptr_t obj = 0, sr = 0, cap = 0, cv = 0; uint8_t valid = 0; float blend = 0;
    if (!ReadProcessMemory(pr, (void*)ga.objB, &obj, 8, &got) || !obj) return 0;
    if (!ReadProcessMemory(pr, (void*)(obj + 0x698), &sr, 8, &got) || !sr) return 0;
    if (!ReadProcessMemory(pr, (void*)(sr + ga.captureOff), &cap, 8, &got) || !cap) return 0;
    if (!ReadProcessMemory(pr, (void*)(cap + 0x10), &cv, 8, &got) || !cv) return 0;
    if (!ReadProcessMemory(pr, (void*)(cv + 0x43), &valid, 1, &got) || !valid) return 0;
    if (!ReadProcessMemory(pr, (void*)(cap + 0xc4), &blend, 4, &got)) return 0;
    return blend > 0 ? (blend < 1 ? blend : 1) : 0;
}

static const char* kBlurHlsl = R"HLSL(
Texture2D t0 : register(t0); Texture2D t1 : register(t1); SamplerState s0 : register(s0);
cbuffer C : register(b0) { float4 p; };          // p.xy = texel step (blur) / source texel (down), p.z = mix, p.w = dim
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) { O o; float2 t = float2((id << 1) & 2, id & 2); o.uv = t; o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
float4 down(O i) : SV_Target {                     // 4 bilinear taps = 4x4 box
    float2 d = p.xy;
    return 0.25 * (t0.Sample(s0, i.uv + float2(-d.x, -d.y)) + t0.Sample(s0, i.uv + float2(d.x, -d.y)) +
                   t0.Sample(s0, i.uv + float2(-d.x, d.y)) + t0.Sample(s0, i.uv + float2(d.x, d.y)));
}
float4 blur(O i) : SV_Target {                     // 9-tap Gaussian along p.xy
    static const float w[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
    float4 c = t0.Sample(s0, i.uv) * w[0];
    for (int k = 1; k < 5; k++) c += (t0.Sample(s0, i.uv + p.xy * k) + t0.Sample(s0, i.uv - p.xy * k)) * w[k];
    return c;
}
float4 comp(O i) : SV_Target {
    float4 a = t0.Sample(s0, i.uv), b = t1.Sample(s0, i.uv);
    float4 r = lerp(a, b * p.w, p.z); r.a = a.a; return r;
}
)HLSL";
struct BlurRes {
    bool tried = false, ok = false;
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader *down = nullptr, *blur = nullptr, *comp = nullptr;
    ID3D11SamplerState* smp = nullptr; ID3D11Buffer* cb = nullptr;
    void* target = nullptr; UINT w = 0, h = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    ID3D11RenderTargetView* targetRTV = nullptr;
    ID3D11Texture2D* full = nullptr; ID3D11ShaderResourceView* fullSRV = nullptr;
    ID3D11Texture2D* sm[2] = {}; ID3D11ShaderResourceView* smSRV[2] = {}; ID3D11RenderTargetView* smRTV[2] = {};
};
static BlurRes g_blur;
static float g_menuBlend = 0;
static bool g_liveOn = false;                    // live world behind menus patched in (step 9c)
static DXGI_FORMAT TypedFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}
static void BlurReleaseTargets() {
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(g_blur.targetRTV); rel(g_blur.fullSRV); rel(g_blur.full);
    for (int i = 0; i < 2; i++) { rel(g_blur.smSRV[i]); rel(g_blur.smRTV[i]); rel(g_blur.sm[i]); }
    g_blur.target = nullptr;
}
static bool BlurSetup(ID3D11Device* dev, ID3D11Texture2D* target) {
    if (!g_blur.tried) {
        g_blur.tried = true;
        ID3DBlob *v = nullptr, *a = nullptr, *b = nullptr, *c = nullptr;
        D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        g_blur.ok = CompileHlsl(kBlurHlsl, "vs", "vs_5_0", &v) && CompileHlsl(kBlurHlsl, "down", "ps_5_0", &a) &&
                    CompileHlsl(kBlurHlsl, "blur", "ps_5_0", &b) && CompileHlsl(kBlurHlsl, "comp", "ps_5_0", &c) &&
                    SUCCEEDED(o_CreateVS(dev, v->GetBufferPointer(), v->GetBufferSize(), nullptr, &g_blur.vs)) &&
                    SUCCEEDED(o_CreatePS(dev, a->GetBufferPointer(), a->GetBufferSize(), nullptr, &g_blur.down)) &&
                    SUCCEEDED(o_CreatePS(dev, b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_blur.blur)) &&
                    SUCCEEDED(o_CreatePS(dev, c->GetBufferPointer(), c->GetBufferSize(), nullptr, &g_blur.comp)) &&
                    SUCCEEDED(dev->CreateSamplerState(&sd, &g_blur.smp)) && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &g_blur.cb));
        for (auto* x : { v, a, b, c }) if (x) x->Release();
        Log("menu blur: %s", g_blur.ok ? "ready" : "could not be set up (menus stay sharp)");
    }
    if (!g_blur.ok) return false;
    D3D11_TEXTURE2D_DESC d; target->GetDesc(&d);
    if (g_blur.target == target && g_blur.w == d.Width && g_blur.h == d.Height && g_blur.fmt == d.Format) return true;
    BlurReleaseTargets();
    DXGI_FORMAT tf = TypedFormat(d.Format);
    D3D11_RENDER_TARGET_VIEW_DESC rd = {}; rd.Format = tf; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    D3D11_TEXTURE2D_DESC fd = {}; fd.Width = d.Width; fd.Height = d.Height; fd.MipLevels = 1; fd.ArraySize = 1; fd.Format = d.Format;
    fd.SampleDesc.Count = 1; fd.Usage = D3D11_USAGE_DEFAULT; fd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {}; vd.Format = tf; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
    D3D11_TEXTURE2D_DESC qd = fd; qd.Width = d.Width / 4 ? d.Width / 4 : 1; qd.Height = d.Height / 4 ? d.Height / 4 : 1; qd.Format = tf;
    qd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    bool ok = (d.BindFlags & D3D11_BIND_RENDER_TARGET) && d.SampleDesc.Count == 1 &&
              SUCCEEDED(dev->CreateRenderTargetView(target, &rd, &g_blur.targetRTV)) &&
              SUCCEEDED(dev->CreateTexture2D(&fd, nullptr, &g_blur.full)) && SUCCEEDED(dev->CreateShaderResourceView(g_blur.full, &vd, &g_blur.fullSRV));
    for (int i = 0; i < 2 && ok; i++)
        ok = SUCCEEDED(dev->CreateTexture2D(&qd, nullptr, &g_blur.sm[i])) && SUCCEEDED(dev->CreateShaderResourceView(g_blur.sm[i], nullptr, &g_blur.smSRV[i])) &&
             SUCCEEDED(dev->CreateRenderTargetView(g_blur.sm[i], nullptr, &g_blur.smRTV[i]));
    if (!ok) { BlurReleaseTargets(); Log("menu blur: can't use the scene image (format %d)", (int)d.Format); g_blur.ok = false; return false; }
    g_blur.target = target; g_blur.w = d.Width; g_blur.h = d.Height; g_blur.fmt = d.Format;
    return true;
}
static void BlurUiTarget(ID3D11DeviceContext* c, float k) {
    ID3D11Texture2D* target = (ID3D11Texture2D*)g_uiTarget;
    if (!target) return;
    ID3D11Device* dev = nullptr; c->GetDevice(&dev);
    bool ok = BlurSetup(dev, target);
    dev->Release();
    if (!ok) return;
    using PFN_UpdSub = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
    using PFN_Copy = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
    StateGuard g; g.Save(c);
    ORIG(c, I_CopyResource, PFN_Copy)(c, g_blur.full, target);
    UINT qw = g_blur.w / 4 ? g_blur.w / 4 : 1, qh = g_blur.h / 4 ? g_blur.h / 4 : 1;
    D3D11_VIEWPORT vq = { 0, 0, (float)qw, (float)qh, 0, 1 }, vf = { 0, 0, (float)g_blur.w, (float)g_blur.h, 0, 1 };
    ID3D11ShaderResourceView* none[2] = {};
    auto pass = [&](ID3D11RenderTargetView* rt, const D3D11_VIEWPORT& vp, ID3D11PixelShader* ps, ID3D11ShaderResourceView* s0, ID3D11ShaderResourceView* s1, const float* cbv) {
        c->PSSetShaderResources(0, 2, none);
        ORIG(c, I_UpdateSubresource, PFN_UpdSub)(c, g_blur.cb, 0, nullptr, cbv, 0, 0);
        PrepSimpleDraw(c, rt, vp, nullptr);
        ORIG(c, I_VSSetShader, PFN_SetShader)(c, g_blur.vs, nullptr, 0);
        ORIG(c, I_PSSetShader, PFN_SetShader)(c, ps, nullptr, 0);
        ORIG(c, I_PSSetCB, PFN_SetCB)(c, 0, 1, &g_blur.cb);
        ID3D11ShaderResourceView* sv[2] = { s0, s1 };
        c->PSSetShaderResources(0, 2, sv);
        c->PSSetSamplers(0, 1, &g_blur.smp);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ORIG(c, I_Draw, PFN_Draw)(c, 3, 0);
    };
    float r = g_cfg.menuBlurRadius;
    float cdown[4] = { 1.0f / g_blur.w, 1.0f / g_blur.h, 0, 0 };
    float ch[4] = { r / qw, 0, 0, 0 }, cv[4] = { 0, r / qh, 0, 0 };
    float cc[4] = { 0, 0, k, g_cfg.menuDim };      // mix in the dimmed blur by the menu fade amount
    pass(g_blur.smRTV[0], vq, g_blur.down, g_blur.fullSRV, nullptr, cdown);
    for (int it = 0; it < 2; it++) {
        pass(g_blur.smRTV[1], vq, g_blur.blur, g_blur.smSRV[0], nullptr, ch);
        pass(g_blur.smRTV[0], vq, g_blur.blur, g_blur.smSRV[1], nullptr, cv);
    }
    pass(g_blur.targetRTV, vf, g_blur.comp, g_blur.fullSRV, g_blur.smSRV[0], cc);
    c->PSSetShaderResources(0, 2, none);
    g.Restore(c);
}

// ---- dual render: hook the game's render call ----
// Frame loop (exe+0x495750): game->vfunc[+0x40](dt) prepares the frame (cb_scene upload),
// then game->vfunc[+0x60]() renders every pass, then Present. The game object is a global
// (exe+905870 in 1.06.1, found by kSigGameObj). We patch its vtable slot 12 (+0x60) and call the original twice per frame:
// left eye, capture, right eye (prepare-phase buffers re-uploaded), capture.
using PFN_Render = void(*)(void*);
static PFN_Render o_Render = nullptr;
static bool g_dualCaptured = false;
static ID3D11DeviceContext* g_immCtx = nullptr;   // immediate context (not ref-counted)

static void ReuploadPrep(ID3D11DeviceContext* c) {
    static uint8_t tmp[65536];
    for (int pass = 0; pass < 2; pass++)              // cb_scene first: it sets the correction the copies use
        for (auto& u : g_prep) {
            bool isMain = u.res == (ID3D11Resource*)g_mainSceneBuf;
            if (isMain != (pass == 0)) continue;
            UINT size = (UINT)u.mono.size();
            memcpy(tmp, u.mono.data(), size);
            // same handling as the live upload: any camera buffer (main OR a menu-model camera) gets the
            // eye applied, the rest are camera copies (9f only did that for the main camera, so the menu
            // model camera kept the left eye in both passes - flat models)
            bool ok = (size >= kSceneSize && size <= 4096 && ProcessSceneBuffer((float*)tmp, size, u.res)) ||
                      (!isMain && PatchMatrixCopies((float*)tmp, size));
            if (!ok) continue;
            if (u.dynamic) {
                D3D11_MAPPED_SUBRESOURCE m;
                if (SUCCEEDED(ORIG(c, I_Map, PFN_Map)(c, u.res, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    memcpy(m.pData, tmp, size);
                    ORIG(c, I_Unmap, PFN_Unmap)(c, u.res, 0);
                }
            } else ORIG(c, I_UpdateSubresource, PFN_UpdateSubresource)(c, u.res, 0, nullptr, tmp, 0, 0);
        }
}

static void CaptureEye(int eye) {
    if (!g_immCtx) return;
    // the scene composite target (UI on the panel) or, before it is learned, the backbuffer
    // Only the learned UI target (we hold a reference on it). The backbuffer pointer can be stale
    // while the game recreates its swapchain at startup -> likely cause of the occasional boot crash.
    ID3D11Texture2D* src = (ID3D11Texture2D*)g_uiTarget;
    if (!src) return;                                 // not learned yet: OnPresent uses the backbuffer itself
    ID3D11Device* dev = nullptr; g_immCtx->GetDevice(&dev);
    if (xr::CaptureEye(dev, g_immCtx, src, g_cfg.swapEyes ? -eye : eye)) g_dualCaptured = true;
    dev->Release();
}

static void Hook_Render(void* self) {
    if (!g_dualActive || !g_immCtx) { o_Render(self); return; }
    g_inRender = true;
    for (int k = 0; k < 2; k++) {
        g_dualEye = k == 0 ? -1 : +1;
        g_uiClearedThisFrame = false;                 // HUD panel: redraw cleanly for each pass
        if (k == 1) ReuploadPrep(g_immCtx);           // prepare phase already used the left eye
        g_sceneLit = false;
        o_Render(self);
        if (k == 0) g_menuBlend = (g_cfg.menuBlur && g_liveOn) ? MenuBlendAmount() : 0;
        if (g_menuBlend > 0.01f) BlurUiTarget(g_immCtx, g_menuBlend);
        CaptureEye(g_dualEye);
        if (k == 0 && g_cfg.panelPerEye && g_cfg.hud && g_uiTex && g_uiDrawnThisFrame) {   // keep the first eye's panel
            D3D11_TEXTURE2D_DESC d; g_uiTex->GetDesc(&d);
            if (g_uiTexP0) { D3D11_TEXTURE2D_DESC e; g_uiTexP0->GetDesc(&e);
                if (e.Width != d.Width || e.Height != d.Height || e.Format != d.Format) { g_uiRTVP0->Release(); g_uiTexP0->Release(); g_uiTexP0 = nullptr; g_uiRTVP0 = nullptr; } }
            if (!g_uiTexP0) {
                ID3D11Device* dev = nullptr; g_immCtx->GetDevice(&dev);
                if (FAILED(dev->CreateTexture2D(&d, nullptr, &g_uiTexP0)) || FAILED(dev->CreateRenderTargetView(g_uiTexP0, nullptr, &g_uiRTVP0))) {
                    if (g_uiTexP0) { g_uiTexP0->Release(); g_uiTexP0 = nullptr; }
                    g_uiRTVP0 = nullptr;
                }
                dev->Release();
            }
            if (g_uiTexP0) { g_immCtx->CopyResource(g_uiTexP0, g_uiTex); g_panelP0Valid = true; }
        }
    }
    // Menus show a flat (blurred) snapshot: the game draws the same picture into both eyes. Shown
    // with the live head pose it would follow the head, so pin it to the eye poses from when it
    // appeared. Detected by comparing the two eye images (a real per-eye render always differs;
    // 8c used the lighting shader instead, which also pinned the 3D title screen).
    {
        static bool holding = false; static int same = 0, differ = 0;
        ID3D11Device* dev = nullptr; g_immCtx->GetDevice(&dev);
        bool on = g_cfg.menuBackdrop || g_cfg.holdScene;
        int r = on ? xr::CompareEyes(dev, g_immCtx) : 0;
        dev->Release();
        if (r == 1) { same++; differ = 0; } else if (r == 0) { differ++; same = 0; }
        bool want = on && (holding ? differ < 3 : same >= 2);
        // MenuBackdrop=1: the snapshot becomes a screen in the room (no mirrored copy, HUD + controllers
        // in front). HoldSceneInMenus=1: the older frozen-pose pin (mirrored copy behind you).
        bool ok = g_cfg.menuBackdrop ? xr::SetBackdropMode(want, g_cfg.backdropDist, g_cfg.backdropScale) : xr::SetHoldPose(want);
        if (want != holding && ok) {
            holding = want;
            Log("menu background: %s", want ? (g_cfg.menuBackdrop ? "flat snapshot (both eyes identical) - shown as a screen in the room"
                                                                   : "flat snapshot (both eyes identical) - pinned in the room")
                                            : "3D again (eyes differ) - following the head");
        }
    }
    g_dualEye = 0;
    g_inRender = false;
}

static void QRotInvP(const float* q, const float* v, float* out) {    // rotate v by the inverse of unit quaternion q (x,y,z,w)
    float R[3][3]; QuatToMat3(q, R);
    for (int i = 0; i < 3; i++) out[i] = R[0][i] * v[0] + R[1][i] * v[1] + R[2][i] * v[2];
}

// ---- controller physics (step 10a) ----
// The game's DynamicBone system (hair, cloth, ribbons, accessories: fdk dynamic_bone.cpp) updates each
// chain set with exe+4e4680(set, dt, p3, modifier). For every particle it asks the modifier
// (vtable +0x10) for extra collision after the game's own colliders. We wrap that call and pass a
// stand-in modifier: it forwards the force call (+0x08) to the real one (or returns zero force) and
// pushes particles out of a sphere at each controller. Only physics bones react - the body skeleton
// is not simulated by this system, so it never moves (the controller just shows through it).
struct ProxyMod { void** vt; void* orig; };
using PFN_ChainUpdate = void (*)(void* set, float dt, float* p3, void* mod);
using PFN_ModForce = float* (*)(void* self, float* out, float dt, void* p4, void* particle);
using PFN_ModCollide = bool (*)(void* self, void* set, uint8_t* particle, uint8_t* shape);
static PFN_ChainUpdate o_ChainUpdate = nullptr;
static volatile float g_physC[2][4];                // controller spheres in game world: x y z valid
static volatile LONG g_physPushes = 0;
static volatile float g_physR = 0.0275f;            // sphere radius = orb radius + extra

static float* ProxyForce(ProxyMod* self, float* out, float dt, void* p4, void* particle) {
    if (self->orig) return ((PFN_ModForce)(*(void***)self->orig)[1])(self->orig, out, dt, p4, particle);
    out[0] = out[1] = out[2] = 0;
    return out;
}
static bool ProxyCollide(ProxyMod* self, void* set, uint8_t* particle, uint8_t* shape) {
    bool hit = false;
    if (self->orig) hit = ((PFN_ModCollide)(*(void***)self->orig)[2])(self->orig, set, particle, shape);
    float* p = (float*)(particle + 0x58);                       // particle position (world)
    float r = *(float*)(shape + 0x160);                         // particle radius (its sphere collider)
    if (!(r >= 0 && r < 10)) r = 0;
    for (int h = 0; h < 2; h++) {
        if (g_physC[h][3] == 0) continue;
        float dx = p[0] - g_physC[h][0], dy = p[1] - g_physC[h][1], dz = p[2] - g_physC[h][2];
        float minD = g_physR + r, d2 = dx*dx + dy*dy + dz*dz;
        if (d2 >= minD * minD || d2 < 1e-12f) continue;
        float d = sqrtf(d2), k = minD / d;
        p[0] = g_physC[h][0] + dx * k; p[1] = g_physC[h][1] + dy * k; p[2] = g_physC[h][2] + dz * k;
        hit = true;
        InterlockedIncrement(&g_physPushes);
    }
    return hit;
}
static void ProxyNop(ProxyMod*) {}
static void* g_proxyVt[4] = { (void*)ProxyNop, (void*)ProxyForce, (void*)ProxyCollide, (void*)ProxyNop };

static void ChainUpdateHook(void* set, float dt, float* p3, void* mod) {
    if (!g_cfg.physics || (g_physC[0][3] == 0 && g_physC[1][3] == 0)) { o_ChainUpdate(set, dt, p3, mod); return; }
    ProxyMod px{ g_proxyVt, mod };
    o_ChainUpdate(set, dt, p3, &px);
}

// The call sites are rel32 calls: our hook is reached through a jump stub allocated near the exe.
static void* AllocNear(uintptr_t target, size_t size) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t g = si.dwAllocationGranularity;
    for (uintptr_t d = g; d < 0x70000000; d += g) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            uintptr_t a = ((target + sgn * (intptr_t)d) / g) * g;
            void* p = VirtualAlloc((void*)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return nullptr;
}
static void InstallPhysicsHook() {
    static bool done = false;
    if (done) return;
    done = true;
    if (!g_cfg.physics) { Log("controller physics: off ([Controllers] Physics=0)"); return; }
    uintptr_t exe = (uintptr_t)GetModuleHandleA(nullptr);
    const GameAddrs& ga = Game();
    if (!ga.physTarget) { Log("controller physics: game version not recognised - off"); return; }
    const uintptr_t target = ga.physTarget, sites[2] = { ga.phys[0], ga.phys[1] };
    uint8_t* stub = (uint8_t*)AllocNear(exe, 64);
    if (!stub) { Log("controller physics: no memory near the game exe - off"); return; }
    stub[0] = 0xff; stub[1] = 0x25; *(int32_t*)(stub + 2) = 0;     // jmp [rip+0]
    *(void**)(stub + 6) = (void*)ChainUpdateHook;
    FlushInstructionCache(GetCurrentProcess(), stub, 14);
    o_ChainUpdate = (PFN_ChainUpdate)target;
    for (uintptr_t s2 : sites) {
        int32_t rel = (int32_t)((intptr_t)stub - (intptr_t)(s2 + 5));
        DWORD old;
        VirtualProtect((void*)(s2 + 1), 4, PAGE_EXECUTE_READWRITE, &old);
        InterlockedExchange((volatile LONG*)(s2 + 1), rel);            // aligned or not, one 4-byte store
        VirtualProtect((void*)(s2 + 1), 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)(s2 + 1), 4);
    }
    Log("controller physics: hooked the DynamicBone update (sphere = orb radius %.3f + extra %.3f m, offset %.3f m)", g_cfg.orbSize * 0.5f, g_cfg.physRadius, g_cfg.physOffset);
}

// Debug ring = where the physics sphere USED THIS FRAME really is, seen through THIS frame's camera
// (world -> mono view -> LOCAL). If it drifts off the orb, the controller -> world mapping or its
// timing is off (physics runs before the frame's camera is set, so it uses the previous one).
static void PhysicsDebugRings() {
    xr::Pose ref;
    bool ok = g_cfg.physics && g_cfg.physDebug && g_haveMonoView && xr::GetRenderEye(0, &ref);
    float pos[2][3]; bool valid[2] = { false, false };
    for (int h = 0; h < 2 && ok; h++) {
        if (g_physC[h][3] == 0) continue;
        const float* V = g_monoView;
        float w[3] = { g_physC[h][0], g_physC[h][1], g_physC[h][2] }, v[3];
        for (int r = 0; r < 3; r++) v[r] = (V[r*4] * w[0] + V[r*4+1] * w[1] + V[r*4+2] * w[2] + V[r*4+3]) / g_cfg.worldScale;
        float R[3][3]; QuatToMat3(ref.refQ, R);
        for (int r = 0; r < 3; r++) pos[h][r] = ref.refP[r] + R[r][0] * v[0] + R[r][1] * v[1] + R[r][2] * v[2];
        valid[h] = true;
    }
    xr::SetPhysicsDebug(pos, valid, g_physR / g_cfg.worldScale);
}

// Controllers (OpenXR LOCAL space) -> game world: relative to the recenter reference they are in the
// game camera's view space (same axes and units the head tracking uses), then the mono view inverse.
static void UpdatePhysicsControllers() {
    xr::ControllerDraw cd[2]; xr::GetControllers(cd);
    { float rr = g_cfg.orbSize * 0.5f + g_cfg.physRadius; g_physR = rr > 0.005f ? rr : 0.005f; }
    {   // keep xr's view of the controller settings current (live tuning, debug sphere)
        xr::ControllerOptions o; o.show = g_cfg.controllers; o.orbSize = g_cfg.orbSize; o.fade = g_cfg.ctrlFade;
        o.holdSec = g_cfg.ctrlHold; o.fadeSec = g_cfg.ctrlFadeTime; o.moveMargin = g_cfg.ctrlMargin;
        o.debugSphere = g_cfg.physDebug && g_cfg.physics;
        xr::SetControllerOptions(o);
    }
    xr::Pose ref;
    bool ok = g_cfg.physics && g_haveMonoView && xr::GetRenderEye(0, &ref);
    for (int h = 0; h < 2; h++) {
        if (!ok || !cd[h].valid) { g_physC[h][3] = 0; continue; }
        // sphere centre: the orb (aim point), moved along the pointing direction by PhysicsOffset
        float R[3][3]; QuatToMat3(cd[h].q, R);
        float fwd[3] = { -R[0][2], -R[1][2], -R[2][2] };                     // aim -Z in LOCAL space
        float cp[3] = { cd[h].p[0] + fwd[0] * g_cfg.physOffset, cd[h].p[1] + fwd[1] * g_cfg.physOffset, cd[h].p[2] + fwd[2] * g_cfg.physOffset };
        float rel[3] = { cp[0] - ref.refP[0], cp[1] - ref.refP[1], cp[2] - ref.refP[2] }, v[3];
        QRotInvP(ref.refQ, rel, v);
        for (float& c : v) c *= g_cfg.worldScale;      // real metres -> game units (as the head tracking does)
        const float* M = g_monoViewInv;                 // rows (out = M v)
        g_physC[h][0] = M[0] * v[0] + M[1] * v[1] + M[2] * v[2] + M[3];
        g_physC[h][1] = M[4] * v[0] + M[5] * v[1] + M[6] * v[2] + M[7];
        g_physC[h][2] = M[8] * v[0] + M[9] * v[1] + M[10] * v[2] + M[11];
        g_physC[h][3] = 1;
    }
    static LONG lastLog = 0;
    if (g_frame - lastLog >= 600) {
        lastLog = g_frame;
        LONG n = InterlockedExchange(&g_physPushes, 0);
        if (n) Log("controller physics: %ld particle pushes in the last 600 frames", (long)n);
    }
}

static void InstallRenderHook() {
    static bool done = false;
    if (done || !g_cfg.dual) return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(nullptr);
    const GameAddrs& ga = Game();
    if (!ga.objA) { done = true; Log("dual render: game version not recognised (frame loop differs) - staying with alternate-eye"); return; }
    void* obj = *(void**)ga.objA;
    if (!obj) return;                                  // game object not created yet: try again next frame
    done = true;
    void** vt = *(void***)obj;
    o_Render = (PFN_Render)vt[12];
    DWORD old;
    VirtualProtect(&vt[12], sizeof(void*), PAGE_READWRITE, &old);
    vt[12] = (void*)Hook_Render;
    VirtualProtect(&vt[12], sizeof(void*), old, &old);
    Log("dual render: game render call hooked (exe+%llx) - both eyes rendered every frame", (unsigned long long)((uintptr_t)o_Render - exe));
}

// ---- live world behind menus (step 9c) ----
// The in-game menu fades a blurred screenshot in over the world: a screen-capture object
// (scene renderer +0x8b8, blend amount at +0xc4). The scene renderer (exe+2b3a50):
//  - early exit at exe+2b3c19: capture valid and blend >= 1.0 -> draw only the captured picture
//    and return (the world is not drawn at all - why the menu backdrop is flat and frozen);
//  - late composite at exe+2b688d: blend > 0 -> composite the capture over the rendered world.
// Patch: always take the normal path (je -> jmp) and never composite (jbe -> jmp), so the world
// is drawn live (dual render = real 3D, head-tracked) behind menus, sharp.
// (9a patched the game's "draw world" byte readers instead: that only covers loading/transitions.)
// Addresses and bytes come from the signatures (GameAddrs).
static bool WriteCode(uintptr_t a, const uint8_t* from, const uint8_t* to, size_t n) {
    if (memcmp((void*)a, from, n)) return false;
    DWORD old;
    if (!VirtualProtect((void*)a, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)a, to, n);
    VirtualProtect((void*)a, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)a, n);
    return true;
}
static void SetLiveWorld(bool on) {           // called from Present (the game's render code is not running then)
    if (on == g_liveOn) return;
    const GameAddrs& g = Game();
    if (!g.menuEarly || !g.menuLate) {
        Log("live world in menus: game version not recognised - not patched"); g_cfg.liveWorld = false; return;
    }
    bool ok = on ? WriteCode(g.menuEarly, g.earlyOrig, g.earlyPatch, 6) && WriteCode(g.menuLate, g.lateOrig, g.latePatch, 2)
                 : WriteCode(g.menuEarly, g.earlyPatch, g.earlyOrig, 6) && WriteCode(g.menuLate, g.latePatch, g.lateOrig, 2);
    if (!ok) { Log("live world in menus: patch %s FAILED (bytes differ)", on ? "on" : "off"); g_cfg.liveWorld = g_liveOn; return; }
    g_liveOn = on;
    Log("live world in menus: %s", on ? "ON - the 3D world is drawn live behind menus (no blurred screenshot)" : "OFF - the game's blurred screenshot is used again");
}
// Title screen / transitions (experiment, opt-in): the game's "draw world" byte (game object
// +0x1ba5) is 0 there, so the prepare phase (exe+23dfb0) and the render function (exe+301b6) skip
// the world and show a cached still picture. Patching those two readers draws it live. Risky:
// other game code waits for that byte to be 0 (loading?) and may then run while we draw.
static void SetLiveTitle(bool on) {
    static bool cur = false;
    if (on == cur) return;
    const GameAddrs& g = Game();
    if (!g.titleR || !g.titleP) {
        Log("live title screen: game version not recognised - not patched"); g_cfg.liveTitle = false; return;
    }
    bool ok = on ? WriteCode(g.titleR, g.trOrig, g.trPatch, 6) && WriteCode(g.titleP, g.tpOrig, g.tpPatch, 2)
                 : WriteCode(g.titleR, g.trPatch, g.trOrig, 6) && WriteCode(g.titleP, g.tpPatch, g.tpOrig, 2);
    if (!ok) { Log("live title screen: patch FAILED (bytes differ)"); g_cfg.liveTitle = cur; return; }
    cur = on;
    Log("live title screen: %s", on ? "ON (experiment) - title screen and transitions drawn live" : "OFF");
}

static void LogGameMode() {                   // the game's render mode / "draw world" byte, on change
    const GameAddrs& ga = Game();
    void* obj = nullptr; SIZE_T got = 0;
    if (!ga.objB || !ReadProcessMemory(GetCurrentProcess(), (void*)ga.objB, &obj, sizeof obj, &got) || !obj) return;
    uint32_t mode = 0; uint8_t flag = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (uint8_t*)obj + 0x1ba0, &mode, 4, &got) ||
        !ReadProcessMemory(GetCurrentProcess(), (uint8_t*)obj + 0x1ba5, &flag, 1, &got)) return;
    static uint32_t lastMode = 0xFFFFFFFF; static int lastFlag = -1;
    if (mode != lastMode || flag != lastFlag) {
        Log("game render mode 0x%x, world drawn by the game: %s%s", mode, flag ? "yes" : "no (cached picture)",
            (!flag && g_liveOn) ? " - drawn live by SoraVR" : "");
        lastMode = mode; lastFlag = flag;
    }
}

static void BeforePresent(IDXGISwapChain* sc) {
    if (g_frame > 600) LogStack("present");
    {   // remember the backbuffer pointer so the frame-sequence capture can recognise it
        ID3D11Texture2D* bb = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) && bb) { g_backbuffer = bb; bb->Release(); }
    }
    // the frame about to be presented was rendered for CurrentEye(); hand it to OpenXR
    ID3D11Device* dev = nullptr;
    if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) && dev) {
        ID3D11DeviceContext* ctx = nullptr;
        dev->GetImmediateContext(&ctx);
        int eye = (g_cfg.enabled && g_cfg.mode == 0) ? CurrentEye() : 0;   // single-eye test modes -> same image both eyes
        float tv = g_tanV > 0 ? g_tanV : 0.577f, th = g_tanH > 0 ? g_tanH : tv * 16.0f / 9.0f;
        { DXGI_SWAP_CHAIN_DESC sd; if (SUCCEEDED(sc->GetDesc(&sd))) g_gameHwnd = sd.OutputWindow; }
        {   // one automatic recenter a few seconds after the headset starts tracking
            static ULONGLONG trackedSince = 0; static bool done = false;
            xr::Pose tp;
            if (!done && g_cfg.autoRecenterSec > 0 && xr::GetRenderEye(0, &tp)) {
                if (!trackedSince) trackedSince = GetTickCount64();
                else if (GetTickCount64() - trackedSince >= (ULONGLONG)(g_cfg.autoRecenterSec * 1000)) {
                    done = true; xr::Recenter(); Log("automatic recenter (%.0f s after head tracking started)", g_cfg.autoRecenterSec);
                }
            }
        }
        if (g_liveToggleReq || (g_cfg.keyLiveWorld && KeyPressed(g_cfg.keyLiveWorld))) { g_cfg.liveWorld = !g_cfg.liveWorld; g_liveToggleReq = false; }
        SetLiveWorld(g_cfg.liveWorld);
        SetLiveTitle(g_cfg.liveTitle);
        LogGameMode();
        ApplyPointer();
        DrawCursorOnPanel(ctx);
        xr::HudParams hp{ g_cfg.hud && g_uiDrawnThisFrame, g_cfg.hudHeadLock, g_cfg.hudDistance, g_cfg.hudWidth };
        // per-eye panels: pass 0 was rendered for the left eye (right if the eyes are swapped)
        ID3D11Texture2D* hudR = g_uiTex; ID3D11Texture2D* hudL = nullptr;
        if (g_panelP0Valid && g_dualActive && g_dualCaptured) { if (g_cfg.swapEyes) hudL = g_uiTex, hudR = g_uiTexP0; else hudL = g_uiTexP0; }
        xr::SetHudLeftEye(hudL);
        PhysicsDebugRings();
        xr::OnPresent(dev, ctx, sc, eye, g_cfg.swapEyes, th, tv, hudR, hp, g_cfg.paired && eye != 0, g_dualActive && g_dualCaptured);
        g_panelP0Valid = false;
        g_uiClearedThisFrame = false; g_uiDrawnThisFrame = false; g_uiPass = false; g_onUiTarget = false;
        InstallRenderHook();
        InstallPhysicsHook();
        UpdatePhysicsControllers();
        g_dualActive = g_cfg.dual && g_cfg.enabled && g_cfg.mode == 0 && o_Render != nullptr;   // for the next frame
        g_dualCaptured = false;
        g_prep.clear();
        if (ctx) ctx->Release();
        dev->Release();
    }
    OnFrameEnd();
}

// Who presents, from which thread (overlay / startup diagnostics): logged when the caller changes.
static void LogPresentCaller(const char* which, void* sc, UINT flags) {
    static DWORD lastTid = 0; static void* lastSc = nullptr; static int n = 0;
    DWORD tid = GetCurrentThreadId();
    if ((tid != lastTid || sc != lastSc) && n < 12) {
        n++; lastTid = tid; lastSc = sc;
        Log("%s: called by thread %lu, swapchain %p, flags 0x%x, frame %ld", which, tid, sc, flags, (long)g_frame);
        LogStack("present");
    }
}
static HRESULT STDMETHODCALLTYPE Hook_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    LogPresentCaller("Present", sc, flags);
    if (!(flags & DXGI_PRESENT_TEST)) BeforePresent(sc);
    return o_Present(sc, sync, flags);
}
static HRESULT STDMETHODCALLTYPE Hook_Present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    LogPresentCaller("Present1", sc, flags);
    if (!(flags & DXGI_PRESENT_TEST)) BeforePresent(sc);
    return o_Present1(sc, sync, flags, p);
}

// The Steam overlay (and other overlays) hook Present with an inline detour on whatever function the
// swapchain vtable points to - here ours. Our compiled hooks start with a short conditional jump,
// which such detours can't relocate: the overlay then calls a null trampoline and the game crashes
// (only when started from Steam, where the overlay is loaded). So the vtable gets a tiny stub with
// a detour-friendly start instead: mov rax, imm64 / jmp rax, padded with int3.
static void* DetourFriendlyThunk(void* target) {
    uint8_t* t = (uint8_t*)VirtualAlloc(nullptr, 32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!t) return target;
    memset(t, 0xcc, 32);
    t[0] = 0x48; t[1] = 0xb8; memcpy(t + 2, &target, 8);     // mov rax, target
    t[10] = 0xff; t[11] = 0xe0;                               // jmp rax
    DWORD old; VirtualProtect(t, 32, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 32);
    return t;
}

// If an overlay already detoured the real Present (Steam overlay when the game is started from Steam:
// it is loaded before us), the function starts with its jump: E9 rel32 or FF 25 [rip+disp32].
// Then we must not change the vtable: the overlay hooks whatever the vtable points to on every new
// swapchain and keeps only one "original", so it would end up calling itself. Instead we put our
// jump in front of the overlay's: P -> our hook -> (o_Present) the overlay's hook -> the real Present.
// The overlay finds P in its list of hooked functions and leaves it alone.
static uintptr_t ExistingDetourTarget(const uint8_t* p) {
    if (p[0] == 0xe9) return (uintptr_t)(p + 5) + *(const int32_t*)(p + 1);
    if (p[0] == 0xff && p[1] == 0x25) return *(const uintptr_t*)(p + 6 + *(const int32_t*)(p + 2));
    return 0;
}
static void* ChainInFront(void* fn, void* hook, const char* name) {
    uint8_t* p = (uint8_t*)fn;
    uintptr_t prev = ExistingDetourTarget(p);
    if (!prev) return nullptr;
    uint8_t* st = (uint8_t*)AllocNear((uintptr_t)p, 64);
    if (!st) { Log("%s: no memory near the function - cannot chain", name); return nullptr; }
    memset(st, 0xcc, 64);
    st[0] = 0xff; st[1] = 0x25; *(int32_t*)(st + 2) = 0; *(void**)(st + 6) = hook;            // +0: jmp hook
    st[32] = 0xff; st[33] = 0x25; *(int32_t*)(st + 34) = 0; *(uintptr_t*)(st + 38) = prev;     // +32: jmp previous detour
    DWORD old; VirtualProtect(st, 64, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), st, 64);
    int len = p[0] == 0xe9 ? 5 : 6;
    uint8_t patch[6] = { 0xe9, 0, 0, 0, 0, 0x90 };
    *(int32_t*)(patch + 1) = (int32_t)((intptr_t)st - (intptr_t)(p + 5));
    VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, patch, len);
    VirtualProtect(p, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    Log("%s: already hooked by another program (overlay) -> chained in front of it (no vtable change)", name);
    return st + 32;
}

static void HookPresentViaDummy() {
    static bool done = false;
    if (done || !g_realCreateDeviceAndSwapChain) return;
    done = true;
    WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "SoraVRDummy";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("SoraVRDummy", "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC d = {};
    d.BufferCount = 2; d.BufferDesc.Width = 64; d.BufferDesc.Height = 64; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.OutputWindow = hwnd; d.SampleDesc.Count = 1; d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = g_realCreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx);
    if (SUCCEEDED(hr) && sc) {
        void** vt = *(void***)sc;
        { const uint8_t* b = (const uint8_t*)vt[8];
          Log("Present starts with %02x %02x %02x %02x %02x %02x", b[0], b[1], b[2], b[3], b[4], b[5]); }
        if (void* o = ChainInFront(vt[8], (void*)Hook_Present, "Present")) o_Present = (PFN_Present)o;
        else if (void* p = PatchVtable(sc, 8, DetourFriendlyThunk((void*)Hook_Present))) o_Present = (PFN_Present)p;
        IDXGISwapChain1* sc1 = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)) && sc1) {
            void** vt1 = *(void***)sc1;
            if (void* o = ChainInFront(vt1[22], (void*)Hook_Present1, "Present1")) o_Present1 = (PFN_Present1)o;
            else if (void* p = PatchVtable(sc1, 22, DetourFriendlyThunk((void*)Hook_Present1))) o_Present1 = (PFN_Present1)p;
            sc1->Release();
        }
        Log("Present hooks installed (Present=%p Present1=%p)", (void*)o_Present, (void*)o_Present1);
    } else {
        Log("WARNING: dummy swapchain failed (0x%08lX) - no Present hook, eye will not alternate", hr);
    }
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (sc) sc->Release();
    DestroyWindow(hwnd);
}

static void OnDeviceCreated(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (!dev) return;
    ID3D11DeviceContext* imm = ctx;
    if (!imm) dev->GetImmediateContext(&imm);
    if (imm) {
        g_immCtx = imm;
        HookContext(imm, "immediate");
        if (!ctx) imm->Release();
    }
    if (void* p = PatchVtable(dev, 27, (void*)Hook_CreateDeferredContext)) o_CreateDeferredContext = (PFN_CreateDeferredContext)p;
    if (void* p = PatchVtable(dev, 3, (void*)Hook_CreateBuffer))           o_CreateBuffer = (PFN_CreateBuffer)p;
    if (void* p = PatchVtable(dev, 12, (void*)Hook_CreateVS))              o_CreateVS = (PFN_CreateVS)p;
    if (void* p = PatchVtable(dev, 15, (void*)Hook_CreatePS))              o_CreatePS = (PFN_CreatePS)p;
    Log("device %p hooked (CreateBuffer, CreateDeferredContext)", (void*)dev);
    HookPresentViaDummy();
}

// ----------------------------------------------------------------------------
// exports
// ----------------------------------------------------------------------------
extern "C" HRESULT WINAPI Proxy_D3D11CreateDevice(IDXGIAdapter* a, D3D_DRIVER_TYPE dt, HMODULE sw, UINT fl,
    const D3D_FEATURE_LEVEL* fls, UINT nfl, UINT sdk, ID3D11Device** dev, D3D_FEATURE_LEVEL* outFl, ID3D11DeviceContext** ctx) {
    Init();
    if (!g_realCreateDevice) return E_FAIL;
    HRESULT hr = g_realCreateDevice(a, dt, sw, fl, fls, nfl, sdk, dev, outFl, ctx);
    if (SUCCEEDED(hr) && dev && *dev) OnDeviceCreated(*dev, ctx ? *ctx : nullptr);
    return hr;
}

extern "C" HRESULT WINAPI Proxy_D3D11CreateDeviceAndSwapChain(IDXGIAdapter* a, D3D_DRIVER_TYPE dt, HMODULE sw, UINT fl,
    const D3D_FEATURE_LEVEL* fls, UINT nfl, UINT sdk, const DXGI_SWAP_CHAIN_DESC* scd, IDXGISwapChain** sc,
    ID3D11Device** dev, D3D_FEATURE_LEVEL* outFl, ID3D11DeviceContext** ctx) {
    Init();
    if (!g_realCreateDeviceAndSwapChain) return E_FAIL;
    HRESULT hr = g_realCreateDeviceAndSwapChain(a, dt, sw, fl, fls, nfl, sdk, scd, sc, dev, outFl, ctx);
    if (SUCCEEDED(hr) && dev && *dev) OnDeviceCreated(*dev, ctx ? *ctx : nullptr);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
