// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 caffeineowly - SoraVR, https://github.com/caffeineowly/SoraVR

#pragma once
#include <d3d11.h>
#include <dxgi.h>

namespace xr {
// Call once per game Present, before the real Present.
// eye: -1 = the frame just rendered was the left eye, +1 = right, 0 = stereo off (mono to both).
// tanH/tanV: tangent of the game's half field of view (from its projection matrix).
struct HudParams { bool show; bool headLocked; float distance; float width; };
void OnPresent(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* swap, int eye, bool swapEyes, float tanH, float tanV,
               ID3D11Texture2D* hudTex, const HudParams& hud, bool paired, bool dual);
// Dual render: copy a finished eye image (rendered for poseEye -1 left / +1 right) into that eye's slot.
bool CaptureEye(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* src, int poseEye);
// Menus: keep submitting new images but with the eye poses frozen at the moment hold turned on.
bool SetHoldPose(bool hold);
// Menus: show the (flat) eye image as a screen in the room, `distance` metres in front of where the
// head was when it turned on. False = no valid eye poses yet.
bool SetBackdropMode(bool on, float distance, float scale);
// Dual render: the left eye's copy of the HUD panel (nullptr = both eyes see the panel passed to OnPresent).
void SetHudLeftEye(ID3D11Texture2D* left);     // false = no valid eye poses yet (not pinned)
// Compares a few pixel rows of the two eye images (read back one frame later): -1 unknown, 0 differ, 1 identical.
int CompareEyes(ID3D11Device* dev, ID3D11DeviceContext* ctx);
void Shutdown();

// Eye pose the game should render the current frame with (OpenXR LOCAL space, metres),
// plus the recenter reference. eye: -1 left, +1 right, 0 centre. False if not tracking.
struct Pose { float q[4]; float p[3]; float refQ[4]; float refP[3]; };
bool GetRenderEye(int eye, Pose* out);
void Recenter();

// Controller laser pointer: where the active controller's ray hits the HUD panel (u,v in 0..1,
// top-left origin) and its buttons. False if no controller points at the panel.
struct PointerState { bool hit; float u, v; bool click, rightClick; int hand; };
bool GetPointer(PointerState* out);
// Whether the pointer may act now (game shows a cursor and has focus); the laser beam is drawn only then.
void SetPointerActive(bool active);

// Controllers drawn in the world: aim pose (LOCAL space) and fade alpha (1 = moving, 0 = faded out).
struct ControllerOptions { bool show = true; bool fade = true; float holdSec = 1.5f, fadeSec = 1.0f, moveMargin = 0.015f, turnMarginDeg = 8.0f, orbSize = 0.055f;
                           bool debugSphere = false; float physRadius = 0.0275f, physOffset = 0.0f; };   // debug: draw the physics sphere
void SetControllerOptions(const ControllerOptions& o);
// Debug: physics sphere centres (LOCAL space, as the physics really uses them this frame) and radius (metres).
void SetPhysicsDebug(const float pos[2][3], const bool valid[2], float radius);
struct ControllerDraw { bool valid; float p[3]; float q[4]; float alpha; };
void GetControllers(ControllerDraw out[2]);
// Called for each eye image right after it is copied into the headset swapchain, so the mod can
// draw on top (eye pose in LOCAL space = the pose the image was rendered with).
using EyeOverlayFn = void (*)(ID3D11DeviceContext* ctx, ID3D11Texture2D* target, DXGI_FORMAT rtvFormat, UINT w, UINT h,
                              const float* eyeQ, const float* eyeP, float tanH, float tanV);
void SetEyeOverlay(EyeOverlayFn fn);
}
