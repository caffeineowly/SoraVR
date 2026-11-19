// SoraVR OpenXR output.
// Each Present, the frame the game just rendered (one eye, alternating) is copied into a
// persistent per-eye texture; both eye textures are then submitted as a stereo projection
// layer. The layer is in VIEW space (head-locked) until head tracking is added (step 3).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <initializer_list>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include "../third_party/openxr/openxr.h"
#include "../third_party/openxr/openxr_platform.h"

#include "xr.h"

void Log(const char* fmt, ...);

namespace {

HMODULE g_loader = nullptr;
PFN_xrGetInstanceProcAddr p_xrGetInstanceProcAddr = nullptr;

#define XR_FUNCS(X) \
    X(xrCreateInstance) X(xrDestroyInstance) X(xrGetSystem) X(xrGetInstanceProperties) \
    X(xrGetD3D11GraphicsRequirementsKHR) X(xrCreateSession) X(xrDestroySession) \
    X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) \
    X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) X(xrPollEvent) X(xrBeginSession) X(xrEndSession) \
    X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) \
    X(xrReleaseSwapchainImage) X(xrLocateViews) X(xrResultToString) X(xrEnumerateViewConfigurationViews) \
    X(xrCreateActionSet) X(xrDestroyActionSet) X(xrCreateAction) X(xrStringToPath) X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) X(xrCreateActionSpace) X(xrSyncActions) X(xrGetActionStateFloat) X(xrLocateSpace) \
    X(xrGetCurrentInteractionProfile) X(xrPathToString)

#define DECL(name) PFN_##name p_##name = nullptr;
XR_FUNCS(DECL)
#undef DECL

XrInstance  g_inst = XR_NULL_HANDLE;
XrSystemId  g_sys = XR_NULL_SYSTEM_ID;
XrSession   g_sess = XR_NULL_HANDLE;
XrSpace     g_viewSpace = XR_NULL_HANDLE;
XrSpace     g_localSpace = XR_NULL_HANDLE;
// controllers (laser pointer onto the HUD panel)
XrActionSet g_actionSet = XR_NULL_HANDLE;
XrAction    g_aAim = XR_NULL_HANDLE, g_aTrigger = XR_NULL_HANDLE, g_aSqueeze = XR_NULL_HANDLE;
XrPath      g_handPath[2] = { XR_NULL_PATH, XR_NULL_PATH };
XrSpace     g_aimSpace[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
bool        g_actionsReady = false;
bool        g_frameBegun = false;       // xrBeginFrame done, xrEndFrame pending
XrTime      g_frameTime = 0;            // predicted display time of the frame being rendered
XrPosef     g_predEye[2];               // eye poses (LOCAL space) the game should render with now
bool        g_predValid = false;
XrPosef     g_ref;                      // recenter reference (LOCAL space, yaw only)
bool        g_refValid = false;
bool        g_recenterRequested = true;
XrSessionState g_state = XR_SESSION_STATE_UNKNOWN;
bool        g_running = false;
bool        g_failedPermanently = false;
int         g_retryCountdown = 0;

struct Eye {
    ID3D11Texture2D* tex = nullptr;          // latest game frame for this eye
    XrPosef pose{ { 0, 0, 0, 1 }, { 0, 0, 0 } };   // LOCAL-space pose that frame was rendered from
    XrSwapchain sc = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> images;
    bool hasFrame = false;
};
Eye  g_eye[2];
// paired mode: the first frame of a pair is held here until its partner is rendered
ID3D11Texture2D* g_pendingTex = nullptr; XrPosef g_pendingPose; int g_pendingSlot = -1;
struct HudSc { XrSwapchain sc = XR_NULL_HANDLE; std::vector<XrSwapchainImageD3D11KHR> images; UINT w = 0, h = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; };
HudSc g_hud;
struct OrbSc { XrSwapchain sc = XR_NULL_HANDLE; std::vector<XrSwapchainImageD3D11KHR> img; bool failed = false; };
OrbSc g_orb[2];
OrbSc g_ring[2];                         // debug: outline of the controller physics sphere
HudSc g_hudL;                            // HUD panel as seen by the left eye (dual render: per-eye UI, menu models in 3D)
ID3D11Texture2D* g_hudLeftTex = nullptr;
float g_bdScale = 1.4f;
HudSc g_bd;                              // menu backdrop screen (the game's flat snapshot as a quad in the room)
bool g_backdrop = false; XrPosef g_bdPose; float g_bdDist = 4.0f;
std::vector<std::pair<ID3D11Texture2D*, ID3D11RenderTargetView*>> g_scRTV;   // eye swapchain images, for clearing
// eye-image comparison (menu snapshot detection), see CompareEyes
ID3D11Texture2D* g_cmpStage[2] = {}; bool g_cmpPending[2] = {}; int g_cmpIdx = 0, g_cmpResult = -1;
UINT g_cmpW = 0; DXGI_FORMAT g_cmpFmt = DXGI_FORMAT_UNKNOWN;
void ReleaseCompare() { for (auto*& t : g_cmpStage) if (t) { t->Release(); t = nullptr; } g_cmpW = 0; g_cmpPending[0] = g_cmpPending[1] = false; g_cmpResult = -1; }
// laser beam: a thin quad layer from the controller to the panel (static texture, copied each frame)
XrSwapchain g_beamSc = XR_NULL_HANDLE; std::vector<XrSwapchainImageD3D11KHR> g_beamImg;
ID3D11Texture2D* g_beamSrc = nullptr; bool g_beamFailed = false;
const UINT kBeamW = 16, kBeamH = 128;
UINT g_w = 0, g_h = 0;
DXGI_FORMAT g_bbFmt = DXGI_FORMAT_UNKNOWN;
int64_t g_xrFmt = 0;

const char* R(XrResult r) {
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (p_xrResultToString && g_inst && XR_SUCCEEDED(p_xrResultToString(g_inst, r, buf))) return buf;
    snprintf(buf, sizeof buf, "XrResult(%d)", (int)r);
    return buf;
}

bool LoadLoader() {
    if (g_loader) return true;
    g_loader = LoadLibraryA("openxr_loader.dll");
    if (!g_loader) { Log("XR: openxr_loader.dll not found next to the game exe - copy it there (Khronos OpenXR loader, see README) - no VR output"); return false; }
    p_xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(g_loader, "xrGetInstanceProcAddr");
    if (!p_xrGetInstanceProcAddr) { Log("XR: xrGetInstanceProcAddr missing in openxr_loader.dll"); return false; }
    p_xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction*)&p_xrCreateInstance);
    return p_xrCreateInstance != nullptr;
}

void DestroySwapchains() {
    for (auto& e : g_eye) {
        if (e.sc) { p_xrDestroySwapchain(e.sc); e.sc = XR_NULL_HANDLE; }
        e.images.clear();
        if (e.tex) { e.tex->Release(); e.tex = nullptr; }
        if (g_pendingTex) { g_pendingTex->Release(); g_pendingTex = nullptr; g_pendingSlot = -1; }
        e.hasFrame = false;
    }
    g_w = g_h = 0;
    if (g_hud.sc) { p_xrDestroySwapchain(g_hud.sc); g_hud.sc = XR_NULL_HANDLE; }
    g_hud.images.clear(); g_hud.w = g_hud.h = 0;
    ReleaseCompare();
    for (auto& o : g_orb) { if (o.sc) { p_xrDestroySwapchain(o.sc); o.sc = XR_NULL_HANDLE; } o.img.clear(); o.failed = false; }
    for (auto& o : g_ring) { if (o.sc) { p_xrDestroySwapchain(o.sc); o.sc = XR_NULL_HANDLE; } o.img.clear(); o.failed = false; }
    if (g_hudL.sc) { p_xrDestroySwapchain(g_hudL.sc); g_hudL.sc = XR_NULL_HANDLE; }
    g_hudL.images.clear(); g_hudL.w = g_hudL.h = 0;
    if (g_bd.sc) { p_xrDestroySwapchain(g_bd.sc); g_bd.sc = XR_NULL_HANDLE; }
    g_bd.images.clear(); g_bd.w = g_bd.h = 0;
    for (auto& r : g_scRTV) r.second->Release();
    g_scRTV.clear();
    if (g_beamSc) { p_xrDestroySwapchain(g_beamSc); g_beamSc = XR_NULL_HANDLE; }
    g_beamImg.clear();
    if (g_beamSrc) { g_beamSrc->Release(); g_beamSrc = nullptr; }
}

void DestroyAll() {
    DestroySwapchains();
    if (g_viewSpace) { p_xrDestroySpace(g_viewSpace); g_viewSpace = XR_NULL_HANDLE; }
    if (g_localSpace) { p_xrDestroySpace(g_localSpace); g_localSpace = XR_NULL_HANDLE; }
    for (auto& sp : g_aimSpace) if (sp) { p_xrDestroySpace(sp); sp = XR_NULL_HANDLE; }
    if (g_actionSet) { p_xrDestroyActionSet(g_actionSet); g_actionSet = XR_NULL_HANDLE; }
    g_actionsReady = false;
    g_frameBegun = false; g_predValid = false; g_refValid = false; g_recenterRequested = true;
    if (g_sess) { p_xrDestroySession(g_sess); g_sess = XR_NULL_HANDLE; }
    if (g_inst) { p_xrDestroyInstance(g_inst); g_inst = XR_NULL_HANDLE; }
    g_sys = XR_NULL_SYSTEM_ID;
    g_running = false;
    g_state = XR_SESSION_STATE_UNKNOWN;
}

void InitActions() {
    XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(asci.actionSetName, "soravr");
    strcpy(asci.localizedActionSetName, "SoraVR");
    if (XR_FAILED(p_xrCreateActionSet(g_inst, &asci, &g_actionSet))) { Log("XR: action set create failed - no controller pointer"); return; }
    p_xrStringToPath(g_inst, "/user/hand/left", &g_handPath[0]);
    p_xrStringToPath(g_inst, "/user/hand/right", &g_handPath[1]);
    auto mk = [&](XrAction* out, const char* name, const char* loc, XrActionType type) {
        XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
        strcpy(ci.actionName, name); strcpy(ci.localizedActionName, loc);
        ci.actionType = type; ci.countSubactionPaths = 2; ci.subactionPaths = g_handPath;
        return XR_SUCCEEDED(p_xrCreateAction(g_actionSet, &ci, out));
    };
    if (!mk(&g_aAim, "aim", "Pointer aim", XR_ACTION_TYPE_POSE_INPUT) ||
        !mk(&g_aTrigger, "click", "Left click (pointer)", XR_ACTION_TYPE_FLOAT_INPUT) ||
        !mk(&g_aSqueeze, "right_click", "Right click (pointer)", XR_ACTION_TYPE_FLOAT_INPUT)) { Log("XR: action create failed"); return; }
    struct B { XrAction a; const char* path; };
    auto suggest = [&](const char* profile, std::vector<B> list) {
        XrPath prof; if (XR_FAILED(p_xrStringToPath(g_inst, profile, &prof))) return;
        std::vector<XrActionSuggestedBinding> sb;
        for (auto& b : list) for (const char* hand : { "/user/hand/left", "/user/hand/right" }) {
            char full[256]; snprintf(full, sizeof full, "%s%s", hand, b.path);
            XrPath pth; if (XR_SUCCEEDED(p_xrStringToPath(g_inst, full, &pth))) sb.push_back({ b.a, pth });
        }
        XrInteractionProfileSuggestedBinding s{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        s.interactionProfile = prof; s.countSuggestedBindings = (uint32_t)sb.size(); s.suggestedBindings = sb.data();
        XrResult r = p_xrSuggestInteractionProfileBindings(g_inst, &s);
        if (XR_FAILED(r)) Log("XR: bindings for %s rejected (%s)", profile, R(r));
    };
    suggest("/interaction_profiles/khr/simple_controller", { { g_aAim, "/input/aim/pose" }, { g_aTrigger, "/input/select/click" } });
    suggest("/interaction_profiles/valve/index_controller", { { g_aAim, "/input/aim/pose" }, { g_aTrigger, "/input/trigger/value" }, { g_aSqueeze, "/input/squeeze/value" } });
    suggest("/interaction_profiles/oculus/touch_controller", { { g_aAim, "/input/aim/pose" }, { g_aTrigger, "/input/trigger/value" }, { g_aSqueeze, "/input/squeeze/value" } });
    suggest("/interaction_profiles/htc/vive_controller", { { g_aAim, "/input/aim/pose" }, { g_aTrigger, "/input/trigger/value" }, { g_aSqueeze, "/input/squeeze/click" } });
    suggest("/interaction_profiles/microsoft/motion_controller", { { g_aAim, "/input/aim/pose" }, { g_aTrigger, "/input/trigger/value" }, { g_aSqueeze, "/input/squeeze/click" } });
    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo sci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        sci.action = g_aAim; sci.subactionPath = g_handPath[h]; sci.poseInActionSpace.orientation.w = 1;
        p_xrCreateActionSpace(g_sess, &sci, &g_aimSpace[h]);
    }
    XrSessionActionSetsAttachInfo ai{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    ai.countActionSets = 1; ai.actionSets = &g_actionSet;
    XrResult r = p_xrAttachSessionActionSets(g_sess, &ai);
    g_actionsReady = XR_SUCCEEDED(r);
    Log("XR: controller pointer %s", g_actionsReady ? "ready (trigger = left click, grip = right click)" : R(r));
}

bool CreateInstanceAndSession(ID3D11Device* dev) {
    if (!LoadLoader()) { g_failedPermanently = true; return false; }

    if (!g_inst) {
        const char* exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
        XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
        strcpy(ci.applicationInfo.applicationName, "SoraVR");
        strcpy(ci.applicationInfo.engineName, "SoraVR");
        ci.applicationInfo.applicationVersion = 1;
        ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        ci.enabledExtensionCount = 1;
        ci.enabledExtensionNames = exts;
        {   // which extensions does the runtime offer? (controller render models etc.)
            PFN_xrEnumerateInstanceExtensionProperties en = nullptr;
            p_xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction*)&en);
            uint32_t n = 0;
            if (en && XR_SUCCEEDED(en(nullptr, 0, &n, nullptr)) && n) {
                std::vector<XrExtensionProperties> ext(n, XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES });
                en(nullptr, n, &n, ext.data());
                std::string line; bool model = false;
                for (auto& e : ext) {
                    if (strstr(e.extensionName, "model")) model = true;
                    line += e.extensionName; line += ' ';
                    if (line.size() > 180) { Log("XR: runtime extensions: %s", line.c_str()); line.clear(); }
                }
                if (!line.empty()) Log("XR: runtime extensions: %s", line.c_str());
                Log("XR: controller render model extension %s", model ? "AVAILABLE (see list above)" : "not offered by this runtime");
            }
        }
        XrResult r = p_xrCreateInstance(&ci, &g_inst);
        if (XR_FAILED(r)) { Log("XR: xrCreateInstance failed (%d) - is the VR runtime running?", (int)r); g_inst = XR_NULL_HANDLE; return false; }
#define LOAD(name) p_xrGetInstanceProcAddr(g_inst, #name, (PFN_xrVoidFunction*)&p_##name);
        XR_FUNCS(LOAD)
#undef LOAD
        XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
        if (XR_SUCCEEDED(p_xrGetInstanceProperties(g_inst, &ip)))
            Log("XR: runtime \"%s\" %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
                XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    }

    if (!g_sys) {
        XrSystemGetInfo si{ XR_TYPE_SYSTEM_GET_INFO };
        si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrResult r = p_xrGetSystem(g_inst, &si, &g_sys);
        if (XR_FAILED(r)) { Log("XR: no headset yet (%s), will retry", R(r)); g_sys = XR_NULL_SYSTEM_ID; return false; }
    }

    XrGraphicsRequirementsD3D11KHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
    XrResult r = p_xrGetD3D11GraphicsRequirementsKHR(g_inst, g_sys, &req);
    if (XR_FAILED(r)) { Log("XR: graphics requirements failed (%s)", R(r)); return false; }
    IDXGIDevice* dxgiDev = nullptr; IDXGIAdapter* ad = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) && SUCCEEDED(dxgiDev->GetAdapter(&ad))) {
        DXGI_ADAPTER_DESC ad_d; ad->GetDesc(&ad_d);
        bool same = !memcmp(&ad_d.AdapterLuid, &req.adapterLuid, sizeof(LUID));
        Log("XR: game GPU %s the headset GPU", same ? "matches" : "DOES NOT match");
        ad->Release();
    }
    if (dxgiDev) dxgiDev->Release();

    XrGraphicsBindingD3D11KHR gb{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
    gb.device = dev;
    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &gb;
    sci.systemId = g_sys;
    r = p_xrCreateSession(g_inst, &sci, &g_sess);
    if (XR_FAILED(r)) { Log("XR: xrCreateSession failed (%s)", R(r)); g_sess = XR_NULL_HANDLE; return false; }

    XrReferenceSpaceCreateInfo rs{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    p_xrCreateReferenceSpace(g_sess, &rs, &g_viewSpace);
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    p_xrCreateReferenceSpace(g_sess, &rs, &g_localSpace);
    Log("XR: session created");
    InitActions();
    return true;
}

void PollEvents() {
    XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
    while (p_xrPollEvent(g_inst, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* sc = (XrEventDataSessionStateChanged*)&ev;
            g_state = sc->state;
            Log("XR: session state -> %d", (int)g_state);
            if (g_state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                XrResult r = p_xrBeginSession(g_sess, &bi);
                g_running = XR_SUCCEEDED(r);
                Log("XR: xrBeginSession %s", R(r));
            } else if (g_state == XR_SESSION_STATE_STOPPING) {
                p_xrEndSession(g_sess);
                g_running = false;
                g_frameBegun = false;
                g_predValid = false;
            } else if (g_state == XR_SESSION_STATE_EXITING || g_state == XR_SESSION_STATE_LOSS_PENDING) {
                Log("XR: session ending (state %d), will try again later", (int)g_state);
                DestroyAll();
                g_retryCountdown = 300;
                return;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED && g_actionsReady) {
            for (int h = 0; h < 2; h++) {
                XrInteractionProfileState ps{ XR_TYPE_INTERACTION_PROFILE_STATE };
                char name[XR_MAX_PATH_LENGTH] = "(none)"; uint32_t len = 0;
                if (XR_SUCCEEDED(p_xrGetCurrentInteractionProfile(g_sess, g_handPath[h], &ps)) && ps.interactionProfile != XR_NULL_PATH)
                    p_xrPathToString(g_inst, ps.interactionProfile, sizeof name, &len, name);
                Log("XR: %s controller profile: %s", h ? "right" : "left", name);
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            Log("XR: instance loss pending");
            DestroyAll();
            g_retryCountdown = 300;
            return;
        }
        ev = XrEventDataBuffer{ XR_TYPE_EVENT_DATA_BUFFER };
    }
}

int64_t PickFormat(DXGI_FORMAT bb) {
    uint32_t n = 0;
    p_xrEnumerateSwapchainFormats(g_sess, 0, &n, nullptr);
    std::vector<int64_t> f(n);
    p_xrEnumerateSwapchainFormats(g_sess, n, &n, f.data());
    auto has = [&](DXGI_FORMAT x) { for (auto v : f) if (v == x) return true; return false; };
    // Game output is gamma-encoded; an _SRGB swapchain makes the compositor read it correctly.
    DXGI_FORMAT want[3] = { bb, bb, bb };
    if (bb == DXGI_FORMAT_R8G8B8A8_UNORM) { want[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; want[1] = DXGI_FORMAT_R8G8B8A8_UNORM; }
    if (bb == DXGI_FORMAT_B8G8R8A8_UNORM) { want[0] = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; want[1] = DXGI_FORMAT_B8G8R8A8_UNORM; }
    for (auto w : want) if (has(w)) return w;
    return 0;
}

bool EnsureSwapchains(ID3D11Device* dev, ID3D11Texture2D* bb) {
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    if (g_eye[0].sc && d.Width == g_w && d.Height == g_h && d.Format == g_bbFmt) return true;
    DestroySwapchains();
    g_xrFmt = PickFormat(d.Format);
    if (!g_xrFmt) { Log("XR: runtime has no swapchain format compatible with the game's backbuffer (fmt %d)", (int)d.Format); return false; }
    for (int i = 0; i < 2; i++) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = d.Width; td.Height = d.Height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = d.Format; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_eye[i].tex))) { Log("XR: eye texture create failed"); return false; }

        XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format = g_xrFmt; ci.sampleCount = 1; ci.width = d.Width; ci.height = d.Height;
        ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
        XrResult r = p_xrCreateSwapchain(g_sess, &ci, &g_eye[i].sc);
        if (XR_FAILED(r)) { Log("XR: xrCreateSwapchain failed (%s)", R(r)); g_eye[i].sc = XR_NULL_HANDLE; return false; }
        uint32_t n = 0;
        p_xrEnumerateSwapchainImages(g_eye[i].sc, 0, &n, nullptr);
        g_eye[i].images.assign(n, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
        p_xrEnumerateSwapchainImages(g_eye[i].sc, n, &n, (XrSwapchainImageBaseHeader*)g_eye[i].images.data());
    }
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = d.Width; td.Height = d.Height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = d.Format; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        dev->CreateTexture2D(&td, nullptr, &g_pendingTex);
    }
    g_w = d.Width; g_h = d.Height; g_bbFmt = d.Format;
    Log("XR: eye swapchains %ux%u, game format %d -> XR format %lld, %zu images each",
        g_w, g_h, (int)d.Format, (long long)g_xrFmt, g_eye[0].images.size());
    return true;
}

bool EnsureQuadSc(HudSc& q, ID3D11Texture2D* tex, const char* name) {
    D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
    if (q.sc && q.w == d.Width && q.h == d.Height && q.fmt == d.Format) return true;
    if (q.sc) { p_xrDestroySwapchain(q.sc); q.sc = XR_NULL_HANDLE; q.images.clear(); }
    int64_t fmt = PickFormat(d.Format);
    if (!fmt) return false;
    XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = fmt; ci.sampleCount = 1; ci.width = d.Width; ci.height = d.Height; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    XrResult r = p_xrCreateSwapchain(g_sess, &ci, &q.sc);
    if (XR_FAILED(r)) { Log("XR: %s swapchain create failed (%s)", name, R(r)); q.sc = XR_NULL_HANDLE; return false; }
    uint32_t n = 0;
    p_xrEnumerateSwapchainImages(q.sc, 0, &n, nullptr);
    q.images.assign(n, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
    p_xrEnumerateSwapchainImages(q.sc, n, &n, (XrSwapchainImageBaseHeader*)q.images.data());
    q.w = d.Width; q.h = d.Height; q.fmt = d.Format;
    Log("XR: %s swapchain %ux%u", name, d.Width, d.Height);
    return true;
}
bool EnsureHudSwapchain(ID3D11Texture2D* tex) { return EnsureQuadSc(g_hud, tex, "HUD"); }

// copy a texture into the next image of a quad swapchain
bool FillQuadSc(ID3D11DeviceContext* ctx, HudSc& q, ID3D11Texture2D* src) {
    uint32_t img = 0;
    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(p_xrAcquireSwapchainImage(q.sc, &ai, &img))) return false;
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    p_xrWaitSwapchainImage(q.sc, &wi);
    ctx->CopyResource(q.images[img].texture, src);
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    p_xrReleaseSwapchainImage(q.sc, &ri);
    return true;
}

// controller orbs: one small quad swapchain per hand, refilled each frame with the fade alpha baked in
const UINT kOrbPx = 64; const float kOrbHalo = 1.35f;      // texture covers the ball plus its glow (1.35 x diameter)
std::vector<float> g_orbBase;                              // premultiplied RGBA at full opacity
std::vector<uint32_t> g_orbPx;
bool EnsureOrb(ID3D11Device* dev, int h) {
    if (g_orb[h].sc) return true;
    if (g_orb[h].failed) return false;
    g_orb[h].failed = true;
    int64_t fmt = PickFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!fmt) return false;
    if (g_orbBase.empty()) {
        g_orbBase.resize(kOrbPx * kOrbPx * 4); g_orbPx.resize(kOrbPx * kOrbPx);
        const float L[3] = { -0.3f / 1.0198f, 0.8f / 1.0198f, 0.5f / 1.0198f };
        for (UINT j = 0; j < kOrbPx; j++) for (UINT i = 0; i < kOrbPx; i++) {
            float x = ((i + 0.5f) / kOrbPx * 2 - 1) * kOrbHalo, y = (1 - (j + 0.5f) / kOrbPx * 2) * kOrbHalo;   // in ball radii, y up
            float d = sqrtf(x*x + y*y), r, g, b, a;
            if (d < 1) {
                float nz = sqrtf(1 - d*d);
                float diff = x*L[0] + y*L[1] + nz*L[2]; diff = diff < 0 ? 0 : diff;
                float rim = (1 - nz) * (1 - nz) * 0.5f;
                float edge = (1 - d) * kOrbPx / (2 * kOrbHalo); edge = edge > 1 ? 1 : edge;     // 1-pixel antialiased edge
                float k = 0.35f + 0.65f * diff;
                r = 0.85f * k + rim * 0.5f; g = 0.90f * k + rim * 0.85f; b = 0.95f * k + rim * 1.0f; a = 1;
                r *= edge; g *= edge; b *= edge; a *= edge;
                float gl = 0.5f * (1 - edge);                                                 // blend into the glow
                r += 0.5f * gl; g += 0.85f * gl; b += gl; a += 0.7f * gl;
            } else {
                float t = 1 - (d - 1) / (kOrbHalo - 1); t = t < 0 ? 0 : t; t = t * t * 0.5f;
                r = 0.5f * t; g = 0.85f * t; b = t; a = 0.7f * t;
            }
            float* o = &g_orbBase[(j * kOrbPx + i) * 4];
            o[0] = r > 1 ? 1 : r; o[1] = g > 1 ? 1 : g; o[2] = b > 1 ? 1 : b; o[3] = a > 1 ? 1 : a;
        }
    }
    XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = fmt; ci.sampleCount = 1; ci.width = kOrbPx; ci.height = kOrbPx; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    XrResult r = p_xrCreateSwapchain(g_sess, &ci, &g_orb[h].sc);
    if (XR_FAILED(r)) { Log("XR: orb swapchain failed (%s)", R(r)); g_orb[h].sc = XR_NULL_HANDLE; return false; }
    uint32_t n = 0;
    p_xrEnumerateSwapchainImages(g_orb[h].sc, 0, &n, nullptr);
    g_orb[h].img.assign(n, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
    p_xrEnumerateSwapchainImages(g_orb[h].sc, n, &n, (XrSwapchainImageBaseHeader*)g_orb[h].img.data());
    g_orb[h].failed = false;
    Log("XR: controller orb layer ready (%s hand)", h ? "right" : "left");
    return true;
}
// debug ring: orange outline + faint fill, covering the whole texture (radius = half the quad)
std::vector<uint32_t> g_ringPx;
bool EnsureRing(int h) {
    if (g_ring[h].sc) return true;
    if (g_ring[h].failed) return false;
    g_ring[h].failed = true;
    int64_t fmt = PickFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!fmt) return false;
    if (g_ringPx.empty()) {
        g_ringPx.resize(kOrbPx * kOrbPx);
        for (UINT j = 0; j < kOrbPx; j++) for (UINT i = 0; i < kOrbPx; i++) {
            float x = (i + 0.5f) / kOrbPx * 2 - 1, y = (j + 0.5f) / kOrbPx * 2 - 1, d = sqrtf(x*x + y*y);
            float px = 2.0f / kOrbPx;                                  // one pixel in these units
            float line = 1 - fabsf(d - (1 - 1.5f * px)) / (1.5f * px); line = line < 0 ? 0 : line;
            float fill = d < 1 - 1.5f * px ? 0.15f : 0;
            float a = line > fill ? line : fill, r = 1.0f * a, g = 0.55f * a, b = 0.1f * a;
            g_ringPx[j * kOrbPx + i] = (uint32_t)(r * 255) | ((uint32_t)(g * 255) << 8) | ((uint32_t)(b * 255) << 16) | ((uint32_t)(a * 255) << 24);
        }
    }
    XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = fmt; ci.sampleCount = 1; ci.width = kOrbPx; ci.height = kOrbPx; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    if (XR_FAILED(p_xrCreateSwapchain(g_sess, &ci, &g_ring[h].sc))) { g_ring[h].sc = XR_NULL_HANDLE; return false; }
    uint32_t n = 0;
    p_xrEnumerateSwapchainImages(g_ring[h].sc, 0, &n, nullptr);
    g_ring[h].img.assign(n, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
    p_xrEnumerateSwapchainImages(g_ring[h].sc, n, &n, (XrSwapchainImageBaseHeader*)g_ring[h].img.data());
    g_ring[h].failed = false;
    return true;
}
bool FillRing(ID3D11DeviceContext* ctx, int h) {
    uint32_t img = 0;
    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(p_xrAcquireSwapchainImage(g_ring[h].sc, &ai, &img))) return false;
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    p_xrWaitSwapchainImage(g_ring[h].sc, &wi);
    ctx->UpdateSubresource(g_ring[h].img[img].texture, 0, nullptr, g_ringPx.data(), kOrbPx * 4, 0);
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    p_xrReleaseSwapchainImage(g_ring[h].sc, &ri);
    return true;
}

bool FillOrb(ID3D11DeviceContext* ctx, int h, float alpha) {
    for (size_t k = 0; k < g_orbPx.size(); k++) {
        const float* b = &g_orbBase[k * 4];
        g_orbPx[k] = (uint32_t)(b[0] * alpha * 255) | ((uint32_t)(b[1] * alpha * 255) << 8) | ((uint32_t)(b[2] * alpha * 255) << 16) | ((uint32_t)(b[3] * alpha * 255) << 24);
    }
    uint32_t img = 0;
    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(p_xrAcquireSwapchainImage(g_orb[h].sc, &ai, &img))) return false;
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    p_xrWaitSwapchainImage(g_orb[h].sc, &wi);
    ctx->UpdateSubresource(g_orb[h].img[img].texture, 0, nullptr, g_orbPx.data(), kOrbPx * 4, 0);
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    p_xrReleaseSwapchainImage(g_orb[h].sc, &ri);
    return true;
}

bool EnsureBeam(ID3D11Device* dev) {
    if (g_beamSc) return true;
    if (g_beamFailed) return false;
    g_beamFailed = true;
    int64_t fmt = PickFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!fmt) { Log("XR: no format for the laser beam"); return false; }
    // premultiplied alpha: soft light-blue line, bright core, fading out near the controller
    std::vector<uint32_t> px(kBeamW * kBeamH);
    for (UINT y = 0; y < kBeamH; y++) for (UINT x = 0; x < kBeamW; x++) {
        float t = fabsf((x + 0.5f) / kBeamW * 2 - 1);                       // 0 centre .. 1 edge
        float a = t < 0.35f ? 1.0f : (t > 0.95f ? 0.0f : 1.0f - (t - 0.35f) / 0.6f);
        float fromHand = (kBeamH - 1 - y) / (float)(kBeamH - 1);             // row 0 = far end
        a *= fromHand < 0.15f ? fromHand / 0.15f : 1.0f;
        a *= 0.9f;
        float core = t < 0.2f ? 1.0f : 0.0f;
        float r = (0.45f + 0.55f * core) * a, g = (0.80f + 0.20f * core) * a, b = 1.0f * a;
        px[y * kBeamW + x] = (uint32_t)(r * 255) | ((uint32_t)(g * 255) << 8) | ((uint32_t)(b * 255) << 16) | ((uint32_t)(a * 255) << 24);
    }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = kBeamW; td.Height = kBeamH; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = (DXGI_FORMAT)fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA sd = { px.data(), kBeamW * 4, 0 };
    if (FAILED(dev->CreateTexture2D(&td, &sd, &g_beamSrc))) { Log("XR: laser beam texture failed"); return false; }
    XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = fmt; ci.sampleCount = 1; ci.width = kBeamW; ci.height = kBeamH; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    XrResult r = p_xrCreateSwapchain(g_sess, &ci, &g_beamSc);
    if (XR_FAILED(r)) { Log("XR: laser beam swapchain failed (%s)", R(r)); g_beamSc = XR_NULL_HANDLE; return false; }
    uint32_t n = 0;
    p_xrEnumerateSwapchainImages(g_beamSc, 0, &n, nullptr);
    g_beamImg.assign(n, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
    p_xrEnumerateSwapchainImages(g_beamSc, n, &n, (XrSwapchainImageBaseHeader*)g_beamImg.data());
    g_beamFailed = false;
    Log("XR: laser beam ready");
    return true;
}

} // namespace

// yaw-only reference from a head pose (keeps the horizon level after recentering)
static XrPosef YawOnly(const XrPosef& p) {
    const XrQuaternionf& q = p.orientation;
    // forward (-Z) rotated by q, projected onto the floor plane
    float fx = -(2 * (q.x * q.z + q.w * q.y));
    float fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
    float yaw = atan2f(-fx, -fz);
    XrPosef r;
    r.orientation = { 0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f) };
    r.position = p.position;
    return r;
}

namespace xr {

void Recenter() { g_recenterRequested = true; }

static XrVector3f Rot(const XrQuaternionf& q, XrVector3f v) {   // rotate v by unit quaternion q
    XrVector3f u{ q.x, q.y, q.z };
    float s = q.w;
    float d = u.x*v.x + u.y*v.y + u.z*v.z, uu = u.x*u.x + u.y*u.y + u.z*u.z;
    XrVector3f c{ u.y*v.z - u.z*v.y, u.z*v.x - u.x*v.z, u.x*v.y - u.y*v.x };
    return { 2*d*u.x + (s*s - uu)*v.x + 2*s*c.x, 2*d*u.y + (s*s - uu)*v.y + 2*s*c.y, 2*d*u.z + (s*s - uu)*v.z + 2*s*c.z };
}
static PointerState g_pointer;
static HudParams g_lastHud{};
static bool g_pointerActive = false;                 // set by soravr.cpp: game shows a cursor and has focus
void SetPointerActive(bool a) { g_pointerActive = a; }
struct BeamState { bool show; XrPosef pose; float len; };
static BeamState g_beam{};

// Beam quad: centred halfway along the ray, its local +Y along the ray, its face turned toward the head.
// rotation with the given orthonormal basis (columns x, y, z) -> quaternion
static XrQuaternionf BasisToQuat(XrVector3f x, XrVector3f y, XrVector3f z) {
    float m00 = x.x, m11 = y.y, m22 = z.z, tr = m00 + m11 + m22;
    XrQuaternionf q;
    if (tr > 0) { float s = sqrtf(tr + 1) * 2; q = { (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s }; }
    else if (m00 > m11 && m00 > m22) { float s = sqrtf(1 + m00 - m11 - m22) * 2; q = { 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s }; }
    else if (m11 > m22) { float s = sqrtf(1 + m11 - m00 - m22) * 2; q = { (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s }; }
    else { float s = sqrtf(1 + m22 - m00 - m11) * 2; q = { (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s }; }
    return q;
}

static void SetBeam(XrVector3f o, XrVector3f d, float len) {
    XrVector3f head{ 0.5f * (g_predEye[0].position.x + g_predEye[1].position.x), 0.5f * (g_predEye[0].position.y + g_predEye[1].position.y),
                     0.5f * (g_predEye[0].position.z + g_predEye[1].position.z) };
    XrVector3f c{ o.x + d.x * len * 0.5f, o.y + d.y * len * 0.5f, o.z + d.z * len * 0.5f };
    XrVector3f v{ head.x - c.x, head.y - c.y, head.z - c.z };
    float dv = v.x*d.x + v.y*d.y + v.z*d.z;
    XrVector3f z{ v.x - d.x*dv, v.y - d.y*dv, v.z - d.z*dv };
    float zl = sqrtf(z.x*z.x + z.y*z.y + z.z*z.z);
    if (zl < 1e-4f) { g_beam.show = false; return; }
    z = { z.x / zl, z.y / zl, z.z / zl };
    XrVector3f y = d;
    XrVector3f x{ y.y*z.z - y.z*z.y, y.z*z.x - y.x*z.z, y.x*z.y - y.y*z.x };
    g_beam.pose.orientation = BasisToQuat(x, y, z); g_beam.pose.position = c; g_beam.len = len; g_beam.show = true;
}

// The HUD quad's pose in LOCAL space (same placement as the layer submitted in OnPresent).
static bool HudPoseLocal(const HudParams& hud, XrPosef* out) {
    if (!g_refValid) return false;
    if (hud.headLocked) {
        if (!g_predValid) return false;
        XrPosef h = g_predEye[0];
        h.position = { 0.5f * (g_predEye[0].position.x + g_predEye[1].position.x), 0.5f * (g_predEye[0].position.y + g_predEye[1].position.y),
                       0.5f * (g_predEye[0].position.z + g_predEye[1].position.z) };
        XrVector3f f = Rot(h.orientation, { 0, 0, -hud.distance });
        out->orientation = h.orientation;
        out->position = { h.position.x + f.x, h.position.y + f.y, h.position.z + f.z };
        return true;
    }
    const XrQuaternionf& q = g_ref.orientation;
    float fx = -2 * (q.x * q.z + q.w * q.y), fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
    out->orientation = q;
    out->position = { g_ref.position.x + fx * hud.distance, g_ref.position.y, g_ref.position.z + fz * hud.distance };
    return true;
}

// ---- controllers drawn in the world (orb), fading out while held still ----
static ControllerOptions g_ctrlOpt;
void SetControllerOptions(const ControllerOptions& o) { g_ctrlOpt = o; }
static float g_dbgPos[2][3]; static bool g_dbgValid[2] = { false, false }; static float g_dbgR = 0.04f;
void SetPhysicsDebug(const float pos[2][3], const bool valid[2], float radius) {
    for (int h = 0; h < 2; h++) { g_dbgValid[h] = valid[h]; for (int k = 0; k < 3; k++) g_dbgPos[h][k] = pos[h][k]; }
    g_dbgR = radius;
}
static ControllerDraw g_ctrl[2];
static EyeOverlayFn g_overlay = nullptr;
void SetEyeOverlay(EyeOverlayFn fn) { g_overlay = fn; }
void GetControllers(ControllerDraw out[2]) { out[0] = g_ctrl[0]; out[1] = g_ctrl[1]; }

struct Motion { bool init; XrVector3f p; XrQuaternionf q; XrTime last; };
static Motion g_motion[2];
static void UpdateFade(int h, const XrPosef& pose, bool buttons, bool pointing, XrTime t) {
    Motion& m = g_motion[h];
    float dx = pose.position.x - m.p.x, dy = pose.position.y - m.p.y, dz = pose.position.z - m.p.z;
    float dot = fabsf(pose.orientation.x*m.q.x + pose.orientation.y*m.q.y + pose.orientation.z*m.q.z + pose.orientation.w*m.q.w);
    float ang = 2 * acosf(dot > 1 ? 1 : dot) * 57.29578f;
    // moved past the margin since the last "anchor" (jitter-proof) or a button / panel pointing -> visible
    if (!m.init || sqrtf(dx*dx + dy*dy + dz*dz) > g_ctrlOpt.moveMargin || ang > g_ctrlOpt.turnMarginDeg || buttons || pointing) {
        m.init = true; m.p = pose.position; m.q = pose.orientation; m.last = t;
    }
    float still = (float)((double)(t - m.last) * 1e-9);
    float a = still < g_ctrlOpt.holdSec ? 1.0f : 1.0f - (still - g_ctrlOpt.holdSec) / (g_ctrlOpt.fadeSec > 0.01f ? g_ctrlOpt.fadeSec : 0.01f);
    if (!g_ctrlOpt.fade) a = 1.0f;
    g_ctrl[h].alpha = a < 0 ? 0 : a;
}

static void UpdatePointer(XrTime t) {
    g_pointer.hit = false; g_beam.show = false;
    g_ctrl[0].valid = g_ctrl[1].valid = false;
    if (!g_actionsReady) return;
    XrActiveActionSet aas{ g_actionSet, XR_NULL_PATH };
    XrActionsSyncInfo si{ XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = 1; si.activeActionSets = &aas;
    XrResult sr = p_xrSyncActions(g_sess, &si);
    static XrResult lastSr = XR_SUCCESS;
    if (sr != lastSr) { Log("XR: xrSyncActions %s%s", R(sr), sr == XR_SESSION_NOT_FOCUSED ? " (the game doesn't have VR input focus - dashboard open?)" : ""); lastSr = sr; }
    if (sr != XR_SUCCESS) return;
    XrPosef quad{};
    bool panel = g_lastHud.show && HudPoseLocal(g_lastHud, &quad);
    float w = g_lastHud.width, h = g_lastHud.width * (g_hud.w ? (float)g_hud.h / (float)g_hud.w : 9.0f / 16.0f);
    XrVector3f n = Rot(quad.orientation, { 0, 0, 1 });
    static int lastHand = 1;
    int best = -1; float bu = 0, bv = 0, bt = 0;
    bool tracked[2] = { false, false }; XrVector3f ro[2], rd[2];
    float trig[2] = { 0, 0 }, sq[2] = { 0, 0 };
    for (int hnd = 0; hnd < 2; hnd++) {
        XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO }; gi.subactionPath = g_handPath[hnd];
        XrActionStateFloat fs{ XR_TYPE_ACTION_STATE_FLOAT };
        gi.action = g_aTrigger; if (XR_SUCCEEDED(p_xrGetActionStateFloat(g_sess, &gi, &fs)) && fs.isActive) trig[hnd] = fs.currentState;
        fs = XrActionStateFloat{ XR_TYPE_ACTION_STATE_FLOAT };
        gi.action = g_aSqueeze; if (XR_SUCCEEDED(p_xrGetActionStateFloat(g_sess, &gi, &fs)) && fs.isActive) sq[hnd] = fs.currentState;
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (!g_aimSpace[hnd] || XR_FAILED(p_xrLocateSpace(g_aimSpace[hnd], g_localSpace, t, &loc))) continue;
        if (!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) || !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) continue;
        XrVector3f o = loc.pose.position, d = Rot(loc.pose.orientation, { 0, 0, -1 });
        tracked[hnd] = true; ro[hnd] = o; rd[hnd] = d;
        g_ctrl[hnd].valid = true;
        memcpy(g_ctrl[hnd].p, &loc.pose.position, 12); memcpy(g_ctrl[hnd].q, &loc.pose.orientation, 16);
        UpdateFade(hnd, loc.pose, trig[hnd] > 0.1f || sq[hnd] > 0.1f, g_pointerActive && hnd == lastHand && panel, t);
        if (!panel) continue;
        float den = d.x*n.x + d.y*n.y + d.z*n.z;
        if (fabsf(den) < 1e-5f) continue;
        XrVector3f co{ quad.position.x - o.x, quad.position.y - o.y, quad.position.z - o.z };
        float tt = (co.x*n.x + co.y*n.y + co.z*n.z) / den;
        if (tt <= 0) continue;
        XrVector3f hit{ o.x + d.x*tt - quad.position.x, o.y + d.y*tt - quad.position.y, o.z + d.z*tt - quad.position.z };
        XrQuaternionf qi{ -quad.orientation.x, -quad.orientation.y, -quad.orientation.z, quad.orientation.w };
        XrVector3f l = Rot(qi, hit);
        float u = l.x / w + 0.5f, v = 0.5f - l.y / h;
        if (u < 0 || u > 1 || v < 0 || v > 1) continue;
        if (best < 0 || hnd == lastHand || trig[hnd] > 0.5f) { best = hnd; bu = u; bv = v; bt = tt; }
    }
    static bool loggedTrack = false;
    if (!loggedTrack && (tracked[0] || tracked[1])) { loggedTrack = true; Log("XR: controller aim tracked (left %d, right %d)", tracked[0], tracked[1]); }
    if (g_pointerActive && panel) {
        if (best >= 0) SetBeam(ro[best], rd[best], bt);
        else { int h = tracked[lastHand] ? lastHand : (tracked[1 - lastHand] ? 1 - lastHand : -1); if (h >= 0) SetBeam(ro[h], rd[h], 1.5f); }
    }
    if (best < 0) return;
    static bool loggedHit = false;
    if (!loggedHit) { loggedHit = true; Log("XR: controller ray hit the HUD panel (%s hand, u=%.2f v=%.2f)", best ? "right" : "left", bu, bv); }
    if (trig[best] > 0.5f || sq[best] > 0.5f) lastHand = best;
    g_pointer.hit = true; g_pointer.u = bu; g_pointer.v = bv;
    g_pointer.click = trig[best] > 0.5f; g_pointer.rightClick = sq[best] > 0.5f; g_pointer.hand = best;
}

bool GetPointer(PointerState* out) { *out = g_pointer; return g_pointer.hit; }



bool GetRenderEye(int eye, Pose* out) {
    if (!g_predValid || !g_refValid) return false;
    XrPosef p;
    if (eye < 0) p = g_predEye[0];
    else if (eye > 0) p = g_predEye[1];
    else {   // center
        p = g_predEye[0];
        p.position.x = 0.5f * (g_predEye[0].position.x + g_predEye[1].position.x);
        p.position.y = 0.5f * (g_predEye[0].position.y + g_predEye[1].position.y);
        p.position.z = 0.5f * (g_predEye[0].position.z + g_predEye[1].position.z);
    }
    memcpy(out->refQ, &g_ref.orientation, 16); memcpy(out->refP, &g_ref.position, 12);
    memcpy(out->q, &p.orientation, 16);         memcpy(out->p, &p.position, 12);
    return true;
}

// Dual render: store one finished eye image (called after each of the two render passes).
// Menus: the game shows a flat snapshot. Pin it to the eye poses from when the menu opened, so it
// stays where it was in the room instead of following the head.
static bool g_holdPose = false; static XrPosef g_heldPose[2];
static bool PoseOk(const XrPosef& p) {
    const XrQuaternionf& q = p.orientation;
    float n = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return n > 0.9f && n < 1.1f;
}
// Menu backdrop screen: the game shows a flat snapshot (same picture in both eyes). Instead of
// gluing it to the eyes, show it as a big flat screen in the room, placed exactly where it was
// seen when it appeared (so the switch is invisible), with the controllers and HUD in front.
void SetHudLeftEye(ID3D11Texture2D* left) { g_hudLeftTex = left; }

bool SetBackdropMode(bool on, float distance, float scale) {
    g_bdScale = scale;
    if (on && !g_backdrop) {
        if (!g_eye[0].hasFrame || !g_eye[1].hasFrame || !PoseOk(g_eye[0].pose) || !PoseOk(g_eye[1].pose)) return false;
        XrPosef c = g_eye[0].pose;
        c.position.x = 0.5f * (g_eye[0].pose.position.x + g_eye[1].pose.position.x);
        c.position.y = 0.5f * (g_eye[0].pose.position.y + g_eye[1].pose.position.y);
        c.position.z = 0.5f * (g_eye[0].pose.position.z + g_eye[1].pose.position.z);
        g_bdDist = distance;
        XrVector3f f = Rot(c.orientation, { 0, 0, -distance });
        g_bdPose.orientation = c.orientation;
        g_bdPose.position = { c.position.x + f.x, c.position.y + f.y, c.position.z + f.z };
    }
    g_backdrop = on;
    return true;
}

bool SetHoldPose(bool hold) {
    if (hold && !g_holdPose) {
        // 8c pinned at startup with poses that were never set (zero quaternion) -> garbage copy
        if (!g_eye[0].hasFrame || !g_eye[1].hasFrame || !PoseOk(g_eye[0].pose) || !PoseOk(g_eye[1].pose)) return false;
        g_heldPose[0] = g_eye[0].pose; g_heldPose[1] = g_eye[1].pose;
    }
    g_holdPose = hold;
    return true;
}

// Are the two eye images identical? A flat snapshot (menu backdrop) is drawn identically into
// both eyes; a real per-eye render always differs somewhere (parallax). A few full-width pixel
// rows of each eye are copied to a staging texture and read back a frame later (no GPU stall).
// Returns -1 unknown, 0 different, 1 identical.
static const UINT kCmpStrips = 4, kCmpRows = 2;
int CompareEyes(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (!g_eye[0].tex || !g_eye[1].tex || !dev || !ctx) return -1;
    D3D11_TEXTURE2D_DESC d; g_eye[0].tex->GetDesc(&d);
    UINT bpp = (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 : 4;
    if (!g_cmpStage[0] || g_cmpW != d.Width || g_cmpFmt != d.Format) {
        for (auto*& t : g_cmpStage) if (t) { t->Release(); t = nullptr; }
        D3D11_TEXTURE2D_DESC sd = {}; sd.Width = d.Width; sd.Height = 2 * kCmpStrips * kCmpRows; sd.MipLevels = 1; sd.ArraySize = 1;
        sd.Format = d.Format; sd.SampleDesc.Count = 1; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto*& t : g_cmpStage) if (FAILED(dev->CreateTexture2D(&sd, nullptr, &t))) { t = nullptr; return -1; }
        g_cmpW = d.Width; g_cmpFmt = d.Format; g_cmpPending[0] = g_cmpPending[1] = false; g_cmpResult = -1;
    }
    for (int k = 1; k <= 2; k++) {                          // read back any finished copy (8d stalled here)
    int r = (g_cmpIdx + k) & 1;
    if (g_cmpPending[r]) {
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = ctx->Map(g_cmpStage[r], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
        if (SUCCEEDED(hr)) {
            UINT diff = 0, total = 0;
            for (UINT row = 0; row < kCmpStrips * kCmpRows; row++) {
                const uint8_t* a = (const uint8_t*)m.pData + row * m.RowPitch;
                const uint8_t* b = (const uint8_t*)m.pData + (row + kCmpStrips * kCmpRows) * m.RowPitch;
                for (UINT x = 0; x < g_cmpW; x++) { total++; if (memcmp(a + x * bpp, b + x * bpp, bpp)) diff++; }
            }
            ctx->Unmap(g_cmpStage[r], 0);
            g_cmpPending[r] = false;
            g_cmpResult = (diff * 2000 < total) ? 1 : 0;          // < 0.05% of pixels differ = identical
        } else if (hr != DXGI_ERROR_WAS_STILL_DRAWING) g_cmpPending[r] = false;
    }
    }
    if (!g_cmpPending[g_cmpIdx]) {
        for (UINT s = 0; s < kCmpStrips; s++) {
            UINT y = d.Height * (s + 1) / (kCmpStrips + 1);
            D3D11_BOX box = { 0, y, 0, d.Width, y + kCmpRows, 1 };
            for (int e = 0; e < 2; e++) ctx->CopySubresourceRegion(g_cmpStage[g_cmpIdx], 0, 0, (e * kCmpStrips + s) * kCmpRows, 0, g_eye[e].tex, 0, &box);
        }
        g_cmpPending[g_cmpIdx] = true;
        g_cmpIdx ^= 1;
    }
    return g_cmpResult;
}

bool CaptureEye(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* src, int poseEye) {
    if (!g_sess || !g_running || !g_frameBegun || !dev || !ctx || !src || !poseEye) return false;
    if (!EnsureSwapchains(dev, src)) return false;
    int idx = poseEye < 0 ? 0 : 1;
    ctx->CopyResource(g_eye[idx].tex, src);
    XrPosef p; Pose tmp;
    if (GetRenderEye(poseEye, &tmp)) { memcpy(&p.orientation, tmp.q, 16); memcpy(&p.position, tmp.p, 12); }
    else p = g_predEye[idx];
    if (g_holdPose) p = g_heldPose[idx];
    g_eye[idx].pose = p;
    g_eye[idx].hasFrame = true;
    return true;
}

void OnPresent(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* swap, int eye, bool swapEyes, float tanH, float tanV,
               ID3D11Texture2D* hudTex, const HudParams& hud, bool paired, bool dual) {
    if (g_failedPermanently || !dev || !ctx || !swap) return;
    g_lastHud = hud;
    if (!g_sess) {
        if (g_retryCountdown > 0) { g_retryCountdown--; return; }
        if (!CreateInstanceAndSession(dev)) { g_retryCountdown = 120; return; }
    }
    PollEvents();
    if (!g_sess || !g_running) return;

    // ---- 1. finish the frame the game just rendered ----
    if (g_frameBegun) {
        ID3D11Texture2D* bb = nullptr;
        bool haveBB = SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) && bb;
        bool scOk = haveBB && EnsureSwapchains(dev, bb);
        if (scOk && g_predValid && !dual) {   // (dual render: both eyes were captured during rendering)
            // which image slot(s) this frame belongs to, and the pose it was rendered from
            // swapEyes: the game rendered this frame from the OTHER eye's pose (see soravr.cpp),
            // so the image, its slot and its pose all stay consistent.
            int poseEye = swapEyes ? -eye : eye;
            XrPosef rendered;
            Pose tmp;
            if (GetRenderEye(poseEye, &tmp)) { memcpy(&rendered.orientation, tmp.q, 16); memcpy(&rendered.position, tmp.p, 12); }
            else rendered = g_predEye[poseEye > 0 ? 1 : 0];
            if (poseEye == 0) {
                for (auto& e : g_eye) { ctx->CopyResource(e.tex, bb); e.pose = rendered; e.hasFrame = true; }
            } else if (paired && g_pendingTex) {
                // pairs: the first frame (render parity "left") is held, the second completes the pair
                // and both are shown together - both eyes then show the same game moment
                int idx = poseEye < 0 ? 0 : 1;
                if (eye < 0) { ctx->CopyResource(g_pendingTex, bb); g_pendingPose = rendered; g_pendingSlot = idx; }
                else if (g_pendingSlot >= 0 && g_pendingSlot != idx) {
                    ctx->CopyResource(g_eye[g_pendingSlot].tex, g_pendingTex); g_eye[g_pendingSlot].pose = g_pendingPose; g_eye[g_pendingSlot].hasFrame = true;
                    ctx->CopyResource(g_eye[idx].tex, bb); g_eye[idx].pose = rendered; g_eye[idx].hasFrame = true;
                    g_pendingSlot = -1;
                }
            } else {
                int idx = poseEye < 0 ? 0 : 1;
                ctx->CopyResource(g_eye[idx].tex, bb); g_eye[idx].pose = rendered; g_eye[idx].hasFrame = true;
            }
        }
        if (bb) bb->Release();

        XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
        XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        XrCompositionLayerQuad quad{ XR_TYPE_COMPOSITION_LAYER_QUAD }, quadL{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        XrCompositionLayerQuad beam{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        XrCompositionLayerQuad bdq{ XR_TYPE_COMPOSITION_LAYER_QUAD };
        const XrCompositionLayerBaseHeader* layers[10] = {};
        uint32_t layerCount = 0;
        float ah = atanf(tanH), av = atanf(tanV);
        bool backdrop = g_backdrop && scOk && g_eye[0].hasFrame && g_eye[0].tex && EnsureQuadSc(g_bd, g_eye[0].tex, "menu backdrop");
        if (backdrop && FillQuadSc(ctx, g_bd, g_eye[0].tex)) {      // menu: the flat snapshot as a screen in the room
            bdq.layerFlags = 0;
            bdq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            bdq.subImage.swapchain = g_bd.sc;
            bdq.subImage.imageRect = { { 0, 0 }, { (int32_t)g_bd.w, (int32_t)g_bd.h } };
            // behind the HUD panel, on the same reference (follows recenter); 9d pinned it wherever the
            // head pointed when it appeared (at startup: before the automatic recenter)
            float dist = g_bdDist > hud.distance + 1.0f ? g_bdDist : hud.distance + 1.0f;
            if (hud.headLocked) { bdq.space = g_viewSpace; bdq.pose = { { 0, 0, 0, 1 }, { 0, 0, -dist } }; }
            else if (g_refValid) {
                const XrQuaternionf& q = g_ref.orientation;
                float fx = -2 * (q.x * q.z + q.w * q.y), fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
                bdq.space = g_localSpace; bdq.pose.orientation = q;
                bdq.pose.position = { g_ref.position.x + fx * dist, g_ref.position.y, g_ref.position.z + fz * dist };
            } else { bdq.space = g_localSpace; bdq.pose = g_bdPose; dist = g_bdDist; }
            bdq.size = { 2 * dist * tanH * g_bdScale, 2 * dist * tanV * g_bdScale };
            layers[layerCount++] = (XrCompositionLayerBaseHeader*)&bdq;
        } else backdrop = false;
        if (!backdrop && scOk && g_eye[0].hasFrame && g_eye[1].hasFrame) {   // the 3D world (not while the backdrop screen shows)
            bool ok = true;
            for (int i = 0; i < 2 && ok; i++) {
                uint32_t img = 0;
                XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                if (XR_FAILED(p_xrAcquireSwapchainImage(g_eye[i].sc, &ai, &img))) { ok = false; break; }
                XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wi.timeout = XR_INFINITE_DURATION;
                p_xrWaitSwapchainImage(g_eye[i].sc, &wi);
                ctx->CopyResource(g_eye[i].images[img].texture, g_eye[i].tex);
                XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                p_xrReleaseSwapchainImage(g_eye[i].sc, &ri);
                pv[i].pose = g_eye[i].pose;
                pv[i].fov = { -ah, ah, av, -av };
                pv[i].subImage.swapchain = g_eye[i].sc;
                pv[i].subImage.imageRect = { { 0, 0 }, { (int32_t)g_w, (int32_t)g_h } };
                pv[i].subImage.imageArrayIndex = 0;
            }
            if (ok) {
                layer.space = g_localSpace; layer.viewCount = 2; layer.views = pv;
                layers[layerCount++] = (XrCompositionLayerBaseHeader*)&layer;
            }
        }
        // floating HUD panel (the game's UI, separated from the scene in soravr.cpp)
        if (layerCount >= 1 && hud.show && hudTex && g_refValid && EnsureHudSwapchain(hudTex)) {
            uint32_t img = 0;
            XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            if (XR_SUCCEEDED(p_xrAcquireSwapchainImage(g_hud.sc, &ai, &img))) {
                XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wi.timeout = XR_INFINITE_DURATION;
                p_xrWaitSwapchainImage(g_hud.sc, &wi);
                ctx->CopyResource(g_hud.images[img].texture, hudTex);
                XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                p_xrReleaseSwapchainImage(g_hud.sc, &ri);
                quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain = g_hud.sc;
                quad.subImage.imageRect = { { 0, 0 }, { (int32_t)g_hud.w, (int32_t)g_hud.h } };
                quad.size = { hud.width, hud.width * (float)g_hud.h / (float)g_hud.w };
                if (hud.headLocked) {
                    quad.space = g_viewSpace;
                    quad.pose = { { 0, 0, 0, 1 }, { 0, 0, -hud.distance } };
                } else {
                    // in front of the recenter point, facing it
                    const XrQuaternionf& q = g_ref.orientation;   // yaw only: (0, sy, 0, cy)
                    float fx = -2 * (q.x * q.z + q.w * q.y), fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
                    quad.space = g_localSpace;
                    quad.pose.orientation = q;
                    quad.pose.position = { g_ref.position.x + fx * hud.distance, g_ref.position.y, g_ref.position.z + fz * hud.distance };
                }
                layers[layerCount++] = (XrCompositionLayerBaseHeader*)&quad;
                // dual render: the left eye gets its own copy of the panel (UI is drawn per eye, so 3D
                // menu models get real depth); the main panel image is then the right eye's
                if (g_hudLeftTex && EnsureQuadSc(g_hudL, g_hudLeftTex, "HUD (left eye)") && FillQuadSc(ctx, g_hudL, g_hudLeftTex)) {
                    quadL = quad;
                    quadL.subImage.swapchain = g_hudL.sc;
                    quadL.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
                    quad.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
                    layers[layerCount++] = (XrCompositionLayerBaseHeader*)&quadL;
                }
            }
        }
        // controllers: one small camera-facing quad per controller (a shaded ball looks the same from any
        // side). SteamVR draws quad layers above every projection layer whatever the order, so orbs drawn
        // into the eye images (8a-9c) or a separate projection layer (9d) always ended up under the panel.
        XrCompositionLayerQuad orbq[2] = { { XR_TYPE_COMPOSITION_LAYER_QUAD }, { XR_TYPE_COMPOSITION_LAYER_QUAD } };
        XrCompositionLayerQuad ringq[2] = { { XR_TYPE_COMPOSITION_LAYER_QUAD }, { XR_TYPE_COMPOSITION_LAYER_QUAD } };
        // debug: the controller physics sphere as an outline (always shown while on, ignores the fade)
        if (layerCount >= 1 && g_ctrlOpt.debugSphere) for (int h = 0; h < 2; h++) {
            if (!g_dbgValid[h] || !EnsureRing(h) || !FillRing(ctx, h)) continue;
            XrVector3f p{ g_dbgPos[h][0], g_dbgPos[h][1], g_dbgPos[h][2] };            // where the physics really has it
            XrVector3f head{ 0.5f * (g_eye[0].pose.position.x + g_eye[1].pose.position.x), 0.5f * (g_eye[0].pose.position.y + g_eye[1].pose.position.y),
                             0.5f * (g_eye[0].pose.position.z + g_eye[1].pose.position.z) };
            XrVector3f z{ head.x - p.x, head.y - p.y, head.z - p.z };
            float zl = sqrtf(z.x*z.x + z.y*z.y + z.z*z.z);
            if (zl < 0.02f) continue;
            z = { z.x / zl, z.y / zl, z.z / zl };
            XrVector3f up{ 0, 1, 0 }; if (fabsf(z.y) > 0.99f) up = { 0, 0, -1 };
            XrVector3f x{ up.y*z.z - up.z*z.y, up.z*z.x - up.x*z.z, up.x*z.y - up.y*z.x };
            float xl = sqrtf(x.x*x.x + x.y*x.y + x.z*x.z); x = { x.x / xl, x.y / xl, x.z / xl };
            XrVector3f y{ z.y*x.z - z.z*x.y, z.z*x.x - z.x*x.z, z.x*x.y - z.y*x.x };
            // seen from distance zl, the sphere's outline on a plane through its centre is slightly larger than R
            float R = g_dbgR, sil = zl > R ? R * zl / sqrtf(zl * zl - R * R) : R;
            XrCompositionLayerQuad& q = ringq[h];
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g_ring[h].sc;
            q.subImage.imageRect = { { 0, 0 }, { (int32_t)kOrbPx, (int32_t)kOrbPx } };
            q.space = g_localSpace;
            q.pose.orientation = BasisToQuat(x, y, z);
            q.pose.position = p;
            q.size = { 2 * sil, 2 * sil };
            if (layerCount < 10) layers[layerCount++] = (XrCompositionLayerBaseHeader*)&q;
        }
        if (layerCount >= 1) for (int h = 0; h < 2; h++) {
            if (!g_ctrlOpt.show || !g_ctrl[h].valid || g_ctrl[h].alpha <= 0.01f || !EnsureOrb(dev, h)) continue;
            if (!FillOrb(ctx, h, g_ctrl[h].alpha)) continue;
            XrVector3f p{ g_ctrl[h].p[0], g_ctrl[h].p[1], g_ctrl[h].p[2] };
            XrVector3f head{ 0.5f * (g_eye[0].pose.position.x + g_eye[1].pose.position.x), 0.5f * (g_eye[0].pose.position.y + g_eye[1].pose.position.y),
                             0.5f * (g_eye[0].pose.position.z + g_eye[1].pose.position.z) };
            XrVector3f z{ head.x - p.x, head.y - p.y, head.z - p.z };
            float zl = sqrtf(z.x*z.x + z.y*z.y + z.z*z.z);
            if (zl < 0.02f) continue;                                  // inside the head
            z = { z.x / zl, z.y / zl, z.z / zl };
            XrVector3f up{ 0, 1, 0 };
            if (fabsf(z.y) > 0.99f) up = { 0, 0, -1 };
            XrVector3f x{ up.y*z.z - up.z*z.y, up.z*z.x - up.x*z.z, up.x*z.y - up.y*z.x };
            float xl = sqrtf(x.x*x.x + x.y*x.y + x.z*x.z); x = { x.x / xl, x.y / xl, x.z / xl };
            XrVector3f y{ z.y*x.z - z.z*x.y, z.z*x.x - z.x*x.z, z.x*x.y - z.y*x.x };
            XrCompositionLayerQuad& q = orbq[h];
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g_orb[h].sc;
            q.subImage.imageRect = { { 0, 0 }, { (int32_t)kOrbPx, (int32_t)kOrbPx } };
            q.space = g_localSpace;
            q.pose.orientation = BasisToQuat(x, y, z);
            q.pose.position = p;
            q.size = { g_ctrlOpt.orbSize * kOrbHalo, g_ctrlOpt.orbSize * kOrbHalo };
            layers[layerCount++] = (XrCompositionLayerBaseHeader*)&q;
        }
        // controller laser beam (only while the game shows a cursor)
        if (layerCount >= 1 && g_beam.show && EnsureBeam(dev)) {
            uint32_t img = 0;
            XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            if (XR_SUCCEEDED(p_xrAcquireSwapchainImage(g_beamSc, &ai, &img))) {
                XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wi.timeout = XR_INFINITE_DURATION;
                p_xrWaitSwapchainImage(g_beamSc, &wi);
                ctx->CopyResource(g_beamImg[img].texture, g_beamSrc);
                XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                p_xrReleaseSwapchainImage(g_beamSc, &ri);
                beam.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                beam.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                beam.subImage.swapchain = g_beamSc;
                beam.subImage.imageRect = { { 0, 0 }, { (int32_t)kBeamW, (int32_t)kBeamH } };
                beam.space = g_localSpace;
                beam.pose = g_beam.pose;
                beam.size = { 0.008f, g_beam.len };
                layers[layerCount++] = (XrCompositionLayerBaseHeader*)&beam;
            }
        }
        XrFrameEndInfo ei{ XR_TYPE_FRAME_END_INFO };
        ei.displayTime = g_frameTime;
        ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        ei.layerCount = layerCount;
        ei.layers = layers;
        XrResult r = p_xrEndFrame(g_sess, &ei);
        g_frameBegun = false;
        static int endFails = 0;
        if (XR_FAILED(r) && endFails++ < 5) Log("XR: xrEndFrame %s", R(r));
        static bool first = true;
        if (first && layerCount) { first = false; Log("XR: first tracked stereo frame submitted (fov h=%.1f v=%.1f deg)", 2 * ah * 57.2958f, 2 * av * 57.2958f); }
    }

    // ---- 2. start the next frame and predict where the eyes will be when it is shown ----
    XrFrameState fs{ XR_TYPE_FRAME_STATE };
    XrResult r = p_xrWaitFrame(g_sess, nullptr, &fs);
    if (XR_FAILED(r)) { Log("XR: xrWaitFrame %s", R(r)); return; }
    r = p_xrBeginFrame(g_sess, nullptr);
    if (XR_FAILED(r)) { Log("XR: xrBeginFrame %s", R(r)); return; }
    g_frameBegun = true;
    g_frameTime = fs.predictedDisplayTime;

    XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    XrViewState vs{ XR_TYPE_VIEW_STATE };
    XrViewLocateInfo li{ XR_TYPE_VIEW_LOCATE_INFO };
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = fs.predictedDisplayTime;
    li.space = g_localSpace;
    uint32_t nv = 0;
    if (XR_SUCCEEDED(p_xrLocateViews(g_sess, &li, &vs, 2, &nv, views)) && nv == 2 &&
        (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) && (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {
        g_predEye[0] = views[0].pose;
        g_predEye[1] = views[1].pose;
        if (!g_predValid) Log("XR: head tracking valid");
        g_predValid = true;
        if (g_recenterRequested) {
            XrPosef c = views[0].pose;
            c.position.x = 0.5f * (views[0].pose.position.x + views[1].pose.position.x);
            c.position.y = 0.5f * (views[0].pose.position.y + views[1].pose.position.y);
            c.position.z = 0.5f * (views[0].pose.position.z + views[1].pose.position.z);
            g_ref = YawOnly(c);
            g_refValid = true;
            g_recenterRequested = false;
            Log("XR: recentered");
        }
    }
    UpdatePointer(fs.predictedDisplayTime);
}

void Shutdown() { if (g_inst) DestroyAll(); }

} // namespace xr
