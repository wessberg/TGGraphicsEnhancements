// SPDX-License-Identifier: MIT
// PT-45: the game's own D3D11 textures read by the tracer's D3D12 device, with no copy (docs/agents/texture-share.md).
//
// Another device can open a texture only if it was born with a sharing flag, and a flag can't be added later. So a hook on
// the game's device (ID3D11Device::CreateTexture2D, vtable slot 5, and ID3D11Device3::CreateTexture2D1, slot 54, which
// does not pass through slot 5) adds the legacy flag, D3D11_RESOURCE_MISC_SHARED, to the textures made only for shaders to
// read. (The NT-handle flag refuses BC4 to BC7 and cube maps.) The registry opens such a texture on the shared D3D12 device
// by its legacy handle, writes a view over all its mips into a CPU descriptor heap of its own and gives it a number; the
// render thread copies the views into a range of the tracer's shader-visible heap (publish). D3D12's reference keeps the
// texture's memory alive after the game frees it, so a small object in the texture's private data tells the registry when
// D3D11 lets go, and the D3D12 side goes once the GPU has finished every frame recorded until then.
//
// Phase 3 (docs/agents/share-3.md): the legacy flag on compressed textures is promised by no vendor, so a self-test on a
// worker thread first shares and reads back one texture of each format the game uses on the player's own card; the hook
// goes in only then, and flags only the formats that passed. And the registry stops opening textures as the game's video
// memory count (which counts an opened texture twice) nears the budget Windows gives the game.
#pragma once
#include "gpu.hpp"
#include "tgpt_api.h"

namespace tgpt::share {
constexpr uint32_t kMaxShared=16384; // the numbers, and the range of the tracer's heap they take
// Every size is shared (PT-64, the owner: "Don't do copies"). A texture under 64 KB takes a 64 KB page of its own on the
// tracer's device while both devices have it open, which the registry's count of the tracer's memory includes
// (TGPT_SharedTexture.bytes, openBytes); until then such textures were left to their 64x64 atlas tiles.
// A handle (TGPT_SharedTexture.handle): the number in its low bits, its registration's serial (17 bits) above.
constexpr uint32_t kNumberBits=14, kSerialMask=(1u<<17)-1u;
static_assert((1u<<kNumberBits)==kMaxShared, "a handle's number covers the range");

// The game's device exists and path tracing can run (d3d12_device_ready), whatever the Mode row says: the self-test starts
// on a thread of its own (it returns at once), and the hook goes in when it has passed some format.
void game_device_ready(ID3D11Device* game, ID3D12Device* device);
// TGPT_RegisterSharedTexture (any thread but the render thread's events).
int32_t register_texture(ID3D12Device* device, uint64_t texture, TGPT_SharedTexture* info);
// Death notices taken in and textures the GPU is done with let go of; cheap when there are none (TGPT_ReadStatus calls it
// four times a second, so a texture the game frees while path tracing is off goes as well).
void collect();
uint64_t memory_bytes();
// The same, texture by texture: the bytes of each open texture by its number (the number resolve gives; 0: not open). For
// the tracer's "PT memory by distance" line (PT-87).
void open_bytes(std::vector<uint64_t>& bytes);
// What Windows gives the game of the card's own memory (its budget) and what it counts for the game now, in bytes
// (IDXGIAdapter3::QueryVideoMemoryInfo, the local segment group, as the cap below reads it): every device of the process,
// the game's own and the shared D3D12 one, a shared texture once for each device that holds it open. False: not known.
// For the tracer's memory line (PT-87). The count less the tracer's own figures is NOT what the game itself holds
// (docs/agents/memory-2.md): it also has the second count of the tracer's own shared textures and the driver's memory
// for the tracer's passes, about 2 GB in the run of 2026-10-09 20:25. device: the shared D3D12 device, for a call that
// may come before the first texture was registered (the registry takes its adapter from it).
bool video_memory(uint64_t& budget, uint64_t& counted, ID3D12Device* device=nullptr);
// PT-87 (docs/agents/memory-2.md, "memory3"): Windows' two counts for the process now, in bytes: the card's own memory (the
// local segment group, the one the budget above is for) and the PC's memory the card uses for the process (the non-local
// group). False: not known. device: as video_memory's. For the tracer's "PT memory at start" lines.
bool video_memory_now(uint64_t& local, uint64_t& nonLocal, ID3D12Device* device=nullptr);
// What opening the game's textures did to the first of those counts since the process started: the textures opened, their
// bytes as the registry counts them, and the count's own moves summed over the opens (read right before and right after
// each OpenSharedHandle, under the registry's lock: an open takes well under a millisecond, so little else moves the count
// meanwhile). moved near bytes: Windows counts an opened texture a second time. Near nothing: it does not.
struct OpenCount { uint64_t textures=0, bytes=0; int64_t moved=0; };
OpenCount open_count();
// The tracer's own textures that both devices hold (its atlases, the copies of HDRP's frame, the outputs, the characters'
// and the waves' copies), bytes: Windows counts them a second time once both devices have used them (memory3, M3-2). Told
// by the tracer once a second, for the cap's trial alone (TGPT_TEST_SHARE_COUNT_ONCE: texture_share.cpp within_budget).
// Not in it (memory4, docs/agents/memory-4.md): an atlas whose side the game's device has let go of until its next bake,
// which Windows counts once; the tracer says so again whenever a side goes or comes (tracer.cpp tell_own_shared).
void own_shared(uint64_t bytes);
// TGPT_ClearScene: every texture let go of (once the GPU is done with it); their numbers come free.
void clear();
// Render thread, before a frame is recorded: newly opened textures' views copied into the tracer's heap at base + number.
// retire: the consumer fence value the frame about to be recorded signals; fence: that fence.
void publish(ID3D12Device* device, DescriptorHeap& heap, uint32_t base, ID3D12Fence* fence, uint64_t retire);
// Render thread: the tracer's heap is about to go (release_all, after the GPU finished): nothing is published any more.
void heap_lost();
// PT-46. Moves on whenever a handle may have started or stopped reading its texture: whoever uploads handles resolves them
// again when it changed.
uint64_t generation();
// In place, just before an upload the GPU reads this frame: each material's handles (colorTexture, normalTexture,
// maskTexture) to the number its texture has in the tracer's heap, or -1 where it is dead, another texture's since or not
// published yet; TGPT_MATERIAL_SHARED set where one is left. The same for a list of handles.
void resolve(TGPT_Material* materials, size_t count);
void resolve(int32_t* handles, size_t count);
// Render thread, while a frame is recorded: the size of the top mip of the texture a number resolve gave this frame names
// (it stays on D3D12 until the GPU is past the frame, whatever the game does with it meanwhile). False: none.
bool dimensions(int32_t number, uint32_t& width, uint32_t& height);
// The handle of a texture registered and open (by its ID3D11Texture2D*), -1 otherwise: hair's and eyes' maps, which the
// plugin registers and the native bakes are given by pointer.
int32_t handle_of(uint64_t texture);
} // namespace tgpt::share
