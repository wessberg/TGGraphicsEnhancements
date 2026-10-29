// SPDX-License-Identifier: MIT
// Temporary ReShade 6.8 diagnostic add-on. No image changes, no release dependency.
// Capture the presented buffer after effects and overlay, preserving its native bytes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <reshade.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
namespace api = reshade::api;
static std::mutex gate;
static std::unordered_map<uint64_t, api::swapchain *> chains;
static std::filesystem::path target;
static int state = 0, width = 0, height = 0, format = 0, frame = 0;
static bool registered = false;

extern "C" __declspec(dllexport) int TGNR_HDR_Request(const wchar_t *path, int unity_frame)
{
    std::lock_guard<std::mutex> lock(gate);
    if (!registered || state == 1 || !path || !*path) return 0;
    target = path;
    frame = unity_frame;
    width = height = format = 0;
    state = 1;
    return 1;
}
extern "C" __declspec(dllexport) int TGNR_HDR_Status(int *w, int *h, int *f)
{
    std::lock_guard<std::mutex> lock(gate);
    *w = width; *h = height; *f = format;
    return state;
}
static void init_chain(api::swapchain *chain, bool)
{
    std::lock_guard<std::mutex> lock(gate);
    chains[chain->get_native()] = chain;
}
static void destroy_chain(api::swapchain *chain, bool)
{
    std::lock_guard<std::mutex> lock(gate);
    chains.erase(chain->get_native());
}
static void capture(api::effect_runtime *runtime)
{
    std::lock_guard<std::mutex> lock(gate);
    if (state != 1) return;
    auto *device = runtime->get_device();
    if (device->get_api() != api::device_api::d3d11 && device->get_api() != api::device_api::d3d12) return;
    const auto source = runtime->get_current_back_buffer();
    auto desc = device->get_resource_desc(source);
    // Ignore unrelated small utility windows. The harness runs one game view, FG disabled.
    if (desc.texture.width < 1280 || desc.texture.height < 720) return;
    api::resource staging = {};
    bool mapped = false;
    try
    {
        const auto original = desc;
        width = desc.texture.width; height = desc.texture.height; format = static_cast<int>(desc.texture.format);
        const uint32_t bpp = format == 10 ? 8 : (format == 24 || format == 28 || format == 87 || format == 29 || format == 91) ? 4 : 0;
        if (!bpp || desc.texture.samples != 1) throw std::runtime_error("Unsupported presented pixel format or MSAA");
        const auto it = chains.find(runtime->get_native());
        if (it == chains.end()) throw std::runtime_error("No matching presentation color-space record");
        const auto colorspace = it->second->get_color_space();
        desc.heap = api::memory_heap::readback;
        desc.usage = api::resource_usage::copy_dest;
        desc.flags = api::resource_flags::none;
        if (!device->create_resource(desc, nullptr, api::resource_usage::copy_dest, &staging))
            throw std::runtime_error("Could not create readback texture");
        auto *queue = runtime->get_command_queue();
        auto *cmd = queue->get_immediate_command_list();
        cmd->barrier(source, api::resource_usage::present, api::resource_usage::copy_source);
        cmd->copy_texture_region(source, 0, nullptr, staging, 0, nullptr);
        cmd->barrier(source, api::resource_usage::copy_source, api::resource_usage::present);
        queue->flush_immediate_command_list();
        queue->wait_idle(); // Diagnostic only. Never use these frames as performance evidence.
        api::subresource_data data = {};
        if (!device->map_texture_region(staging, 0, nullptr, api::map_access::read_only, &data))
            throw std::runtime_error("Could not map readback texture");
        mapped = true;
        if (!data.data || data.row_pitch < uint32_t(width) * bpp) throw std::runtime_error("Invalid readback layout");
        std::ofstream raw(target, std::ios::binary);
        for (int y = 0; y < height; ++y)
            raw.write(static_cast<const char *>(data.data) + size_t(y) * data.row_pitch, size_t(width) * bpp);
        raw.close();
        if (!raw) throw std::runtime_error("Could not write raw HDR image");
        device->unmap_texture_region(staging, 0);
        mapped = false;
        device->destroy_resource(staging); staging = {};

        DXGI_OUTPUT_DESC1 output = {};
        Microsoft::WRL::ComPtr<IDXGIOutput> dxgi_output;
        Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
        const bool have_output = SUCCEEDED(reinterpret_cast<IDXGISwapChain *>(runtime->get_native())->GetContainingOutput(&dxgi_output)) &&
            SUCCEEDED(dxgi_output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&output));
        std::ofstream meta(target.wstring() + L".json");
        meta << "{\"width\":" << width << ",\"height\":" << height << ",\"format\":" << format
             << ",\"bytesPerPixel\":" << bpp << ",\"rowPitch\":" << data.row_pitch
             << ",\"colorSpace\":" << static_cast<int>(colorspace) << ",\"api\":" << static_cast<int>(device->get_api())
             << ",\"unityFrame\":" << frame << ",\"swapchain\":" << runtime->get_native()
             << ",\"capturePoint\":\"reshade_present\",\"topDown\":true,\"displayKnown\":" << (have_output ? "true" : "false")
             << ",\"displayColorSpace\":" << static_cast<int>(output.ColorSpace)
             << ",\"displayBits\":" << output.BitsPerColor << ",\"displayMaxNits\":" << output.MaxLuminance << "}\n";
        meta.close();
        if (!meta) throw std::runtime_error("Could not write HDR metadata");
        state = 2;
    }
    catch (const std::exception &ex)
    {
        if (mapped) device->unmap_texture_region(staging, 0);
        if (staging.handle) device->destroy_resource(staging);
        std::ofstream(target.wstring() + L".error.txt") << ex.what();
        reshade::log::message(reshade::log::level::error, ex.what());
        state = -1;
    }
}
extern "C" __declspec(dllexport) const char *NAME = "TG NR HDR Capture (diagnostic)";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Requested raw presentation captures for the TG harness. No rendering changes.";
extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE reshade_module)
{
    if (!reshade::register_addon(module, reshade_module)) return false;
    reshade::register_event<reshade::addon_event::init_swapchain>(init_chain);
    reshade::register_event<reshade::addon_event::destroy_swapchain>(destroy_chain);
    reshade::register_event<reshade::addon_event::reshade_present>(capture);
    registered = true;
    return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE module, HMODULE reshade_module)
{
    registered = false;
    reshade::unregister_event<reshade::addon_event::init_swapchain>(init_chain);
    reshade::unregister_event<reshade::addon_event::destroy_swapchain>(destroy_chain);
    reshade::unregister_event<reshade::addon_event::reshade_present>(capture);
    reshade::unregister_addon(module, reshade_module);
}
