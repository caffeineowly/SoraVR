// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 caffeineowly - SoraVR, https://github.com/caffeineowly/SoraVR

// Forwarders for the rest of the real d3d11.dll exports (generated list, see build notes).
// SoraVR only needs D3D11CreateDevice(AndSwapChain), but other DLLs loaded into the game (the
// Steam overlay, for example) look up more exports from "d3d11.dll" and would get nothing.
// Each stub jumps to the real function, resolved from the system d3d11.dll on first use.
#include <windows.h>

static const char* const kFwdNames[49] = {
    "CreateDirect3D11DeviceFromDXGIDevice",
    "CreateDirect3D11SurfaceFromDXGISurface",
    "D3D11CoreCreateDevice",
    "D3D11CoreCreateLayeredDevice",
    "D3D11CoreGetLayeredDeviceSize",
    "D3D11CoreRegisterLayers",
    "D3D11CreateDeviceForD3D12",
    "D3D11On12CreateDevice",
    "D3DKMTCloseAdapter",
    "D3DKMTCreateAllocation",
    "D3DKMTCreateContext",
    "D3DKMTCreateDevice",
    "D3DKMTCreateSynchronizationObject",
    "D3DKMTDestroyAllocation",
    "D3DKMTDestroyContext",
    "D3DKMTDestroyDevice",
    "D3DKMTDestroySynchronizationObject",
    "D3DKMTEscape",
    "D3DKMTGetContextSchedulingPriority",
    "D3DKMTGetDeviceState",
    "D3DKMTGetDisplayModeList",
    "D3DKMTGetMultisampleMethodList",
    "D3DKMTGetRuntimeData",
    "D3DKMTGetSharedPrimaryHandle",
    "D3DKMTLock",
    "D3DKMTOpenAdapterFromHdc",
    "D3DKMTOpenResource",
    "D3DKMTPresent",
    "D3DKMTQueryAdapterInfo",
    "D3DKMTQueryAllocationResidency",
    "D3DKMTQueryResourceInfo",
    "D3DKMTRender",
    "D3DKMTSetAllocationPriority",
    "D3DKMTSetContextSchedulingPriority",
    "D3DKMTSetDisplayMode",
    "D3DKMTSetDisplayPrivateDriverFormat",
    "D3DKMTSetGammaRamp",
    "D3DKMTSetVidPnSourceOwner",
    "D3DKMTSignalSynchronizationObject",
    "D3DKMTUnlock",
    "D3DKMTWaitForSynchronizationObject",
    "D3DKMTWaitForVerticalBlankEvent",
    "D3DPerformance_BeginEvent",
    "D3DPerformance_EndEvent",
    "D3DPerformance_GetStatus",
    "D3DPerformance_SetMarker",
    "EnableFeatureLevelUpgrade",
    "OpenAdapter10",
    "OpenAdapter10_2",
};
extern "C" void* g_fwd[49];
void* g_fwd[49];
static HRESULT WINAPI FwdMissing() { return E_NOTIMPL; }
extern "C" void* SoraVR_ResolveFwd(unsigned i) {
    static HMODULE real = nullptr;
    if (!real) { char sys[MAX_PATH]; GetSystemDirectoryA(sys, MAX_PATH); lstrcatA(sys, "\\d3d11.dll"); real = LoadLibraryA(sys); }
    void* p = real ? (void*)GetProcAddress(real, kFwdNames[i]) : nullptr;
    if (!p) p = (void*)FwdMissing;
    InterlockedExchangePointer(&g_fwd[i], p);
    return p;
}

asm(".intel_syntax noprefix\n"
    ".globl Fwd_CreateDirect3D11DeviceFromDXGIDevice\n"
    "Fwd_CreateDirect3D11DeviceFromDXGIDevice:\n"
    "  mov rax, qword ptr [rip + g_fwd + 0]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 0\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_CreateDirect3D11SurfaceFromDXGISurface\n"
    "Fwd_CreateDirect3D11SurfaceFromDXGISurface:\n"
    "  mov rax, qword ptr [rip + g_fwd + 8]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 1\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3D11CoreCreateDevice\n"
    "Fwd_D3D11CoreCreateDevice:\n"
    "  mov rax, qword ptr [rip + g_fwd + 16]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 2\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3D11CoreCreateLayeredDevice\n"
    "Fwd_D3D11CoreCreateLayeredDevice:\n"
    "  mov rax, qword ptr [rip + g_fwd + 24]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 3\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3D11CoreGetLayeredDeviceSize\n"
    "Fwd_D3D11CoreGetLayeredDeviceSize:\n"
    "  mov rax, qword ptr [rip + g_fwd + 32]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 4\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3D11CoreRegisterLayers\n"
    "Fwd_D3D11CoreRegisterLayers:\n"
    "  mov rax, qword ptr [rip + g_fwd + 40]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 5\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3D11CreateDeviceForD3D12\n"
    "Fwd_D3D11CreateDeviceForD3D12:\n"
    "  mov rax, qword ptr [rip + g_fwd + 48]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 6\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3D11On12CreateDevice\n"
    "Fwd_D3D11On12CreateDevice:\n"
    "  mov rax, qword ptr [rip + g_fwd + 56]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 7\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTCloseAdapter\n"
    "Fwd_D3DKMTCloseAdapter:\n"
    "  mov rax, qword ptr [rip + g_fwd + 64]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 8\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTCreateAllocation\n"
    "Fwd_D3DKMTCreateAllocation:\n"
    "  mov rax, qword ptr [rip + g_fwd + 72]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 9\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTCreateContext\n"
    "Fwd_D3DKMTCreateContext:\n"
    "  mov rax, qword ptr [rip + g_fwd + 80]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 10\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTCreateDevice\n"
    "Fwd_D3DKMTCreateDevice:\n"
    "  mov rax, qword ptr [rip + g_fwd + 88]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 11\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTCreateSynchronizationObject\n"
    "Fwd_D3DKMTCreateSynchronizationObject:\n"
    "  mov rax, qword ptr [rip + g_fwd + 96]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 12\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTDestroyAllocation\n"
    "Fwd_D3DKMTDestroyAllocation:\n"
    "  mov rax, qword ptr [rip + g_fwd + 104]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 13\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTDestroyContext\n"
    "Fwd_D3DKMTDestroyContext:\n"
    "  mov rax, qword ptr [rip + g_fwd + 112]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 14\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTDestroyDevice\n"
    "Fwd_D3DKMTDestroyDevice:\n"
    "  mov rax, qword ptr [rip + g_fwd + 120]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 15\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTDestroySynchronizationObject\n"
    "Fwd_D3DKMTDestroySynchronizationObject:\n"
    "  mov rax, qword ptr [rip + g_fwd + 128]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 16\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTEscape\n"
    "Fwd_D3DKMTEscape:\n"
    "  mov rax, qword ptr [rip + g_fwd + 136]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 17\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTGetContextSchedulingPriority\n"
    "Fwd_D3DKMTGetContextSchedulingPriority:\n"
    "  mov rax, qword ptr [rip + g_fwd + 144]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 18\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTGetDeviceState\n"
    "Fwd_D3DKMTGetDeviceState:\n"
    "  mov rax, qword ptr [rip + g_fwd + 152]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 19\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTGetDisplayModeList\n"
    "Fwd_D3DKMTGetDisplayModeList:\n"
    "  mov rax, qword ptr [rip + g_fwd + 160]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 20\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTGetMultisampleMethodList\n"
    "Fwd_D3DKMTGetMultisampleMethodList:\n"
    "  mov rax, qword ptr [rip + g_fwd + 168]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 21\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTGetRuntimeData\n"
    "Fwd_D3DKMTGetRuntimeData:\n"
    "  mov rax, qword ptr [rip + g_fwd + 176]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 22\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTGetSharedPrimaryHandle\n"
    "Fwd_D3DKMTGetSharedPrimaryHandle:\n"
    "  mov rax, qword ptr [rip + g_fwd + 184]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 23\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTLock\n"
    "Fwd_D3DKMTLock:\n"
    "  mov rax, qword ptr [rip + g_fwd + 192]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 24\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTOpenAdapterFromHdc\n"
    "Fwd_D3DKMTOpenAdapterFromHdc:\n"
    "  mov rax, qword ptr [rip + g_fwd + 200]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 25\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTOpenResource\n"
    "Fwd_D3DKMTOpenResource:\n"
    "  mov rax, qword ptr [rip + g_fwd + 208]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 26\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTPresent\n"
    "Fwd_D3DKMTPresent:\n"
    "  mov rax, qword ptr [rip + g_fwd + 216]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 27\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTQueryAdapterInfo\n"
    "Fwd_D3DKMTQueryAdapterInfo:\n"
    "  mov rax, qword ptr [rip + g_fwd + 224]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 28\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTQueryAllocationResidency\n"
    "Fwd_D3DKMTQueryAllocationResidency:\n"
    "  mov rax, qword ptr [rip + g_fwd + 232]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 29\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTQueryResourceInfo\n"
    "Fwd_D3DKMTQueryResourceInfo:\n"
    "  mov rax, qword ptr [rip + g_fwd + 240]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 30\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTRender\n"
    "Fwd_D3DKMTRender:\n"
    "  mov rax, qword ptr [rip + g_fwd + 248]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 31\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTSetAllocationPriority\n"
    "Fwd_D3DKMTSetAllocationPriority:\n"
    "  mov rax, qword ptr [rip + g_fwd + 256]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 32\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTSetContextSchedulingPriority\n"
    "Fwd_D3DKMTSetContextSchedulingPriority:\n"
    "  mov rax, qword ptr [rip + g_fwd + 264]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 33\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTSetDisplayMode\n"
    "Fwd_D3DKMTSetDisplayMode:\n"
    "  mov rax, qword ptr [rip + g_fwd + 272]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 34\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTSetDisplayPrivateDriverFormat\n"
    "Fwd_D3DKMTSetDisplayPrivateDriverFormat:\n"
    "  mov rax, qword ptr [rip + g_fwd + 280]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 35\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTSetGammaRamp\n"
    "Fwd_D3DKMTSetGammaRamp:\n"
    "  mov rax, qword ptr [rip + g_fwd + 288]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 36\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTSetVidPnSourceOwner\n"
    "Fwd_D3DKMTSetVidPnSourceOwner:\n"
    "  mov rax, qword ptr [rip + g_fwd + 296]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 37\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTSignalSynchronizationObject\n"
    "Fwd_D3DKMTSignalSynchronizationObject:\n"
    "  mov rax, qword ptr [rip + g_fwd + 304]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 38\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTUnlock\n"
    "Fwd_D3DKMTUnlock:\n"
    "  mov rax, qword ptr [rip + g_fwd + 312]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 39\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTWaitForSynchronizationObject\n"
    "Fwd_D3DKMTWaitForSynchronizationObject:\n"
    "  mov rax, qword ptr [rip + g_fwd + 320]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 40\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DKMTWaitForVerticalBlankEvent\n"
    "Fwd_D3DKMTWaitForVerticalBlankEvent:\n"
    "  mov rax, qword ptr [rip + g_fwd + 328]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 41\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DPerformance_BeginEvent\n"
    "Fwd_D3DPerformance_BeginEvent:\n"
    "  mov rax, qword ptr [rip + g_fwd + 336]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 42\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DPerformance_EndEvent\n"
    "Fwd_D3DPerformance_EndEvent:\n"
    "  mov rax, qword ptr [rip + g_fwd + 344]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 43\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DPerformance_GetStatus\n"
    "Fwd_D3DPerformance_GetStatus:\n"
    "  mov rax, qword ptr [rip + g_fwd + 352]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 44\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_D3DPerformance_SetMarker\n"
    "Fwd_D3DPerformance_SetMarker:\n"
    "  mov rax, qword ptr [rip + g_fwd + 360]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 45\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_EnableFeatureLevelUpgrade\n"
    "Fwd_EnableFeatureLevelUpgrade:\n"
    "  mov rax, qword ptr [rip + g_fwd + 368]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 46\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_OpenAdapter10\n"
    "Fwd_OpenAdapter10:\n"
    "  mov rax, qword ptr [rip + g_fwd + 376]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 47\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".globl Fwd_OpenAdapter10_2\n"
    "Fwd_OpenAdapter10_2:\n"
    "  mov rax, qword ptr [rip + g_fwd + 384]\n"
    "  test rax, rax\n  jz 1f\n  jmp rax\n"
    "1: push rcx\n  push rdx\n  push r8\n  push r9\n  sub rsp, 0x28\n  mov ecx, 48\n"
    "  call SoraVR_ResolveFwd\n  add rsp, 0x28\n  pop r9\n  pop r8\n  pop rdx\n  pop rcx\n  jmp rax\n"
    ".att_syntax prefix\n");
