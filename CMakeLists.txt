# SPDX-License-Identifier: MIT
# Four native DLLs, one per mod:
#   TGGraphicsBridge.Native   TG Graphics Bridge: hooks, the game's device, the shared D3D12 device
#   TGFG.Native               TG Frame Generation extension: presentation bridge, DLSS-G, FSR/XeSS FG
#   TGBetterUpscaling.Native  TG Better Upscaling extension: FSR and XeSS upscaling where HDRP runs STP, and
#                             the bundled DLSS DLL for the game's own DLSS
#   TGPathTracing.Native      TG Path Tracing extension: ReSTIR path tracing on the shared D3D12 device
cmake_minimum_required(VERSION 3.24)
project(TGGraphicsMods VERSION 1.2.0 LANGUAGES C CXX)
# The C and C++ runtimes are linked into each DLL. The game ships no msvcp140.dll, so a shared runtime
# would be whatever the player's System32 has: none (the DLLs don't load) or one older than this
# compiler's, whose _Mtx_lock crashes on the constexpr-constructed std::mutex MSVC 14.40+ emits. Only
# raw pointers, COM objects and plain values cross between these DLLs (tgb/bridge_api.hpp), so each
# having its own runtime is safe. MinHook's older policy level must not bring back /MD.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
option(TGFG_BUILD_NATIVE "Build the Windows x64 native DLLs" ${WIN32})
set(BRIDGE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/native/bridge/include")
set(FG_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/native/framegen/include")
set(UP_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/native/upscaling/include")
set(PT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/native/pathtracing/include")
if(BUILD_TESTING)
  add_executable(tgfg_policy_tests tests/policy_tests.cpp)
  target_include_directories(tgfg_policy_tests PRIVATE ${FG_INCLUDE})
  add_test(NAME tgfg_policy COMMAND tgfg_policy_tests)
  add_executable(tgfg_interop_tests tests/interop_tests.cpp)
  target_include_directories(tgfg_interop_tests PRIVATE ${FG_INCLUDE})
  add_test(NAME tgfg_interop_policy COMMAND tgfg_interop_tests)
  add_executable(tgfg_camera_orientation_tests tests/camera_orientation_tests.cpp)
  target_include_directories(tgfg_camera_orientation_tests PRIVATE ${FG_INCLUDE})
  add_test(NAME tgfg_camera_orientation COMMAND tgfg_camera_orientation_tests)
  add_executable(tgup_policy_tests tests/upscaling_policy_tests.cpp)
  target_include_directories(tgup_policy_tests PRIVATE ${UP_INCLUDE})
  add_test(NAME tgup_policy COMMAND tgup_policy_tests)
endif()
if(TGFG_BUILD_NATIVE)
  if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Native rendering requires Windows x64. Use -DTGFG_BUILD_NATIVE=OFF for portable policy tests.")
  endif()
  set(STREAMLINE_ROOT "" CACHE PATH "NVIDIA Streamline v2.14.1 SDK root (contains include/sl.h)")
  set(NGX_INCLUDE_DIR "" CACHE PATH "Official NVIDIA DLSS/NGX include directory")
  set(MINHOOK_ROOT "" CACHE PATH "Optional local MinHook v1.3.4 source root")
  set(FIDELITYFX_ROOT "" CACHE PATH "AMD FSR (FidelityFX) SDK v2.3.0 root (contains Kits/FidelityFX/api/include)")
  set(XESS_ROOT "" CACHE PATH "Intel XeSS SDK v3.0.2 root (contains inc/xess_fg)")
  set(ANTILAG2_ROOT "" CACHE PATH "AMD Anti-Lag 2 SDK v2.0.0a root (contains ffx_antilag2_dx12.h)")
  set(DLSS_ROOT "" CACHE PATH "NVIDIA DLSS SDK 310.9.1 root (include/nvsdk_ngx_helpers_dlssd_d3d.h, lib/Windows_x86_64/x64/nvsdk_ngx_s.lib)")
  foreach(file include/nvsdk_ngx_helpers_dlssd_d3d.h lib/Windows_x86_64/x64/nvsdk_ngx_s.lib)
    if(NOT EXISTS "${DLSS_ROOT}/${file}")
      message(FATAL_ERROR "Missing ${DLSS_ROOT}/${file}. Set DLSS_ROOT to the NVIDIA DLSS SDK 310.9.1 release (github.com/NVIDIA/DLSS).")
    endif()
  endforeach()
  foreach(header sl.h sl_core_api.h sl_consts.h sl_dlss_g.h sl_reflex.h sl_pcl.h)
    if(NOT EXISTS "${STREAMLINE_ROOT}/include/${header}")
      message(FATAL_ERROR "Missing ${STREAMLINE_ROOT}/include/${header}. Supply the official v2.14.1 SDK.")
    endif()
  endforeach()
  if(NOT EXISTS "${NGX_INCLUDE_DIR}/nvsdk_ngx.h")
    message(FATAL_ERROR "Set NGX_INCLUDE_DIR to the official NVIDIA DLSS/NGX headers. Proprietary headers are not bundled.")
  endif()
  # Headers only: the AMD and Intel runtimes are loaded at run time and are optional.
  foreach(header Kits/FidelityFX/api/include/ffx_api.h Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h
                 Kits/FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.h
                 Kits/FidelityFX/upscalers/include/ffx_upscale.h Kits/FidelityFX/denoisers/include/ffx_denoiser.h)
    if(NOT EXISTS "${FIDELITYFX_ROOT}/${header}")
      message(FATAL_ERROR "Missing ${FIDELITYFX_ROOT}/${header}. Supply the AMD FSR SDK v2.3.0 headers.")
    endif()
  endforeach()
  foreach(header inc/xess_fg/xefg_swapchain_d3d12.h inc/xell/xell_d3d12.h inc/xess/xess_d3d12.h)
    if(NOT EXISTS "${XESS_ROOT}/${header}")
      message(FATAL_ERROR "Missing ${XESS_ROOT}/${header}. Supply the Intel XeSS SDK v3.0.2.")
    endif()
  endforeach()
  if(NOT EXISTS "${ANTILAG2_ROOT}/ffx_antilag2_dx12.h")
    message(FATAL_ERROR "Missing ${ANTILAG2_ROOT}/ffx_antilag2_dx12.h. Supply the AMD Anti-Lag 2 SDK.")
  endif()
  file(READ "${STREAMLINE_ROOT}/include/sl_dlss_g.h" TGFG_DLSSG_HEADER)
  foreach(field dynamicTargetFrameRate bIsDynamicMFGSupported)
    string(FIND "${TGFG_DLSSG_HEADER}" "${field}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR "Streamline SDK is too old: missing ${field}.")
    endif()
  endforeach()
  set(BUILD_SHARED_LIBS OFF) # the detouring helper is linked into the bridge only
  if(MINHOOK_ROOT)
    add_subdirectory("${MINHOOK_ROOT}" "${CMAKE_BINARY_DIR}/minhook")
  else()
    include(FetchContent)
    FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG v1.3.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(minhook)
  endif()
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/native/framegen/shaders/interop.hlsl" TGFG_INTEROP_SHADER)
  configure_file(native/framegen/shaders/interop_shader.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/interop_shader.hpp" @ONLY)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/native/upscaling/shaders/upscale.hlsl" TGUP_UPSCALE_SHADER)
  configure_file(native/upscaling/shaders/upscale_shader.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/upscale_shader.hpp" @ONLY)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/native/pathtracing/shaders/d3d11.hlsl" TGPT_D3D11_SHADER)
  configure_file(native/pathtracing/shaders/d3d11_shader.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/d3d11_shader.hpp" @ONLY)
  # The embedded HLSL is read at configure time: editing it has to configure again, or the DLL keeps the old text.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/native/pathtracing/shaders/d3d11.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/upscaling/shaders/upscale.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/framegen/shaders/interop.hlsl")
  set(TG_DEFINES UNICODE _UNICODE _WIN32_WINNT=0x0A00)
  # The path tracer's passes use Shader Model 6.6 and inline ray tracing, which only DXC compiles. The
  # Windows SDK's dxc.exe signs the DXIL through the dxil.dll next to it.
  set(TG_SDK_BIN_HINTS)
  if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    list(APPEND TG_SDK_BIN_HINTS "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  endif()
  if(DEFINED ENV{WindowsSdkVerBinPath})
    list(APPEND TG_SDK_BIN_HINTS "$ENV{WindowsSdkVerBinPath}/x64")
  endif()
  file(GLOB TG_SDK_BINS LIST_DIRECTORIES true "C:/Program Files (x86)/Windows Kits/10/bin/10.0.*")
  list(SORT TG_SDK_BINS ORDER DESCENDING)
  foreach(dir ${TG_SDK_BINS})
    list(APPEND TG_SDK_BIN_HINTS "${dir}/x64")
  endforeach()
  # DXC 1.9 or newer compiles Shader Model 6.9 (DirectX Raytracing 1.2): a release unpacked beside the SDKs
  # (DXC-<version>/bin/x64, github.com/microsoft/DirectXShaderCompiler) comes before the Windows SDK's.
  file(GLOB TG_LOCAL_DXC LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/DXC-*/bin/x64")
  list(SORT TG_LOCAL_DXC ORDER DESCENDING)
  find_program(TG_DXC dxc HINTS ${TG_LOCAL_DXC} ${TG_SDK_BIN_HINTS} NO_DEFAULT_PATH)
  if(NOT TG_DXC)
    message(FATAL_ERROR "dxc.exe not found. Unpack DXC 1.9 or newer into DXC-<version> beside this file, or set TG_DXC.")
  endif()
  set(PT_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native/pathtracing/shaders")
  set(PT_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/pathtracing")
  file(MAKE_DIRECTORY "${PT_GENERATED}")
  configure_file(native/pathtracing/shaders/pt_shaders.hpp.in "${PT_GENERATED}/pt_shaders.hpp" COPYONLY)
  set(PT_SHADER_HEADERS)
  # Each pass twice: Shader Model 6.6 for every DXR 1.1 GPU, and 6.9 (DirectX Raytracing 1.2: opacity
  # micromaps) for where the device offers it; the GI and reflection passes also as ray generation shaders
  # with Shader Execution Reordering. The tracer picks a set when it starts.
  set(PT_SHADER_DEPENDS "${PT_SHADER_DIR}/pt_common.hlsli" "${PT_SHADER_DIR}/pt_restir.hlsli" "${PT_SHADER_DIR}/pt_root.hlsli")
  foreach(entry prepare:pt_prepare gi:pt_gi giSpatial:pt_gi_spatial di:pt_di diSpatial:pt_di_spatial specular:pt_specular
                temporal:pt_temporal atrous:pt_atrous compose:pt_compose)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 variable)
    list(GET parts 1 name)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}.h"
      COMMAND "${TG_DXC}" -nologo -T cs_6_6 -E main -HV 2021 -O3 -WX -Qstrip_debug -Fh "${PT_GENERATED}/${name}.h" -Vn ${variable} "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl" VERBATIM)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}_dxr12.h"
      COMMAND "${TG_DXC}" -nologo -T cs_6_9 -E main -D PT_DXR12 -HV 2021 -O3 -WX -Qstrip_debug -Fh "${PT_GENERATED}/${name}_dxr12.h" -Vn ${variable}Dxr12 "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl (Shader Model 6.9)" VERBATIM)
    list(APPEND PT_SHADER_HEADERS "${PT_GENERATED}/${name}.h" "${PT_GENERATED}/${name}_dxr12.h")
  endforeach()
  foreach(entry gi:pt_gi specular:pt_specular)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 variable)
    list(GET parts 1 name)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}_ser.h"
      COMMAND "${TG_DXC}" -nologo -T lib_6_9 -D PT_DXR12 -D PT_SER -HV 2021 -O3 -WX -Qstrip_debug -Fh "${PT_GENERATED}/${name}_ser.h" -Vn ${variable}Ser "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl (ray generation, Shader Execution Reordering)" VERBATIM)
    list(APPEND PT_SHADER_HEADERS "${PT_GENERATED}/${name}_ser.h")
  endforeach()
  # Passes that trace no rays: one Shader Model 6.6 build serves both sets (the opacity micromap bake, AMD Ray
  # Regeneration's guides, and characters' skinned positions).
  foreach(entry ommBake:pt_omm_bake rrgGuides:pt_rrg_guides skinUnpack:pt_skin)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 variable)
    list(GET parts 1 name)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}.h"
      COMMAND "${TG_DXC}" -nologo -T cs_6_6 -E main -HV 2021 -O3 -WX -Qstrip_debug -Fh "${PT_GENERATED}/${name}.h" -Vn ${variable} "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl" VERBATIM)
    list(APPEND PT_SHADER_HEADERS "${PT_GENERATED}/${name}.h")
  endforeach()
  add_custom_target(TGPathTracingShaders DEPENDS ${PT_SHADER_HEADERS})

  add_library(TGGraphicsBridge.Native SHARED
    native/bridge/src/core.cpp native/bridge/src/hooks.cpp native/bridge/src/device.cpp
    native/bridge/src/extensions.cpp native/bridge/src/exports.cpp native/bridge/src/performance.cpp
    native/bridge/src/agility.cpp native/bridge/src/diagnostics.cpp)
  target_include_directories(TGGraphicsBridge.Native PRIVATE ${BRIDGE_INCLUDE})
  target_compile_definitions(TGGraphicsBridge.Native PRIVATE ${TG_DEFINES})
  target_link_libraries(TGGraphicsBridge.Native PRIVATE minhook dxgi d3d11 d3d12 dxguid wintrust crypt32 pdh version)

  add_library(TGFG.Native SHARED
    native/framegen/src/sl_runtime.cpp native/framegen/src/ngx_capture.cpp native/framegen/src/dx11_bridge.cpp
    native/framegen/src/control.cpp native/framegen/src/extension.cpp
    native/framegen/src/fsr_backend.cpp native/framegen/src/xess_backend.cpp native/framegen/src/antilag.cpp)
  target_include_directories(TGFG.Native PRIVATE ${FG_INCLUDE} ${BRIDGE_INCLUDE} "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${STREAMLINE_ROOT}/include" "${NGX_INCLUDE_DIR}"
    "${FIDELITYFX_ROOT}/Kits/FidelityFX/api/include" "${FIDELITYFX_ROOT}/Kits/FidelityFX/framegeneration/include"
    "${XESS_ROOT}/inc/xess_fg" "${XESS_ROOT}/inc/xell" "${ANTILAG2_ROOT}")
  target_compile_definitions(TGFG.Native PRIVATE ${TG_DEFINES})
  target_link_libraries(TGFG.Native PRIVATE dxgi d3d11 d3d12 d3dcompiler dxguid version)

  add_library(TGBetterUpscaling.Native SHARED
    native/upscaling/src/extension.cpp native/upscaling/src/upscaler.cpp
    native/upscaling/src/fsr_upscaler.cpp native/upscaling/src/xess_upscaler.cpp native/upscaling/src/dlss_dll.cpp
    native/upscaling/src/nvidia_profile.cpp native/upscaling/src/rr_upscaler.cpp)
  target_include_directories(TGBetterUpscaling.Native PRIVATE ${UP_INCLUDE} ${BRIDGE_INCLUDE} "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${FIDELITYFX_ROOT}/Kits/FidelityFX/api/include" "${FIDELITYFX_ROOT}/Kits/FidelityFX/upscalers/include"
    "${XESS_ROOT}/inc/xess" "${DLSS_ROOT}/include")
  target_compile_definitions(TGBetterUpscaling.Native PRIVATE ${TG_DEFINES})
  # NGX, statically linked (static C runtime, like this DLL), for DLSS Ray Reconstruction on the game's D3D11 device.
  target_link_libraries(TGBetterUpscaling.Native PRIVATE dxgi d3d11 d3d12 d3dcompiler dxguid version
    "${DLSS_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")

  # No d3d12.lib: every D3D12 call goes through the bridge's device, so the DLL never loads d3d12.dll itself.
  add_library(TGPathTracing.Native SHARED
    native/pathtracing/src/extension.cpp native/pathtracing/src/scene.cpp native/pathtracing/src/tracer.cpp
    native/pathtracing/src/ray_regeneration.cpp)
  add_dependencies(TGPathTracing.Native TGPathTracingShaders)
  # AMD FSR Ray Regeneration: the FSR API's headers; the runtime is AMD's signed DLLs, loaded at run time.
  target_include_directories(TGPathTracing.Native PRIVATE ${PT_INCLUDE} ${BRIDGE_INCLUDE} "${CMAKE_CURRENT_BINARY_DIR}/generated" "${PT_GENERATED}"
    "${FIDELITYFX_ROOT}/Kits/FidelityFX/api/include" "${FIDELITYFX_ROOT}/Kits/FidelityFX/denoisers/include")
  target_compile_definitions(TGPathTracing.Native PRIVATE ${TG_DEFINES})
  target_link_libraries(TGPathTracing.Native PRIVATE d3d11 d3dcompiler dxguid)

  if(MSVC)
    foreach(target TGGraphicsBridge.Native TGFG.Native TGBetterUpscaling.Native TGPathTracing.Native)
      target_compile_options(${target} PRIVATE /W4 /EHsc /permissive- /utf-8)
    endforeach()
  endif()
  if(BUILD_TESTING)
    add_executable(tgfg_shader_tests tests/shader_tests.cpp)
    target_include_directories(tgfg_shader_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(tgfg_shader_tests PRIVATE d3dcompiler)
    add_test(NAME tgfg_hlsl_compilation COMMAND tgfg_shader_tests)
    add_executable(tgfg_hudless_color_tests tests/hudless_color_tests.cpp)
    target_include_directories(tgfg_hudless_color_tests PRIVATE ${FG_INCLUDE} "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(tgfg_hudless_color_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME tgfg_hudless_color COMMAND tgfg_hudless_color_tests)
    add_executable(tgfg_renodx_final_pass_tests tests/renodx_final_pass_tests.cpp)
    target_include_directories(tgfg_renodx_final_pass_tests PRIVATE ${FG_INCLUDE})
    target_link_libraries(tgfg_renodx_final_pass_tests PRIVATE d3dcompiler)
    add_test(NAME tgfg_renodx_final_pass COMMAND tgfg_renodx_final_pass_tests)
    add_executable(tgfg_draw_observer_tests tests/d3d11_draw_observer_tests.cpp)
    target_include_directories(tgfg_draw_observer_tests PRIVATE ${FG_INCLUDE})
    target_link_libraries(tgfg_draw_observer_tests PRIVATE minhook d3d11 d3dcompiler)
    add_test(NAME tgfg_draw_observer COMMAND tgfg_draw_observer_tests)
    add_executable(tgfg_input_capture_tests tests/input_capture_tests.cpp)
    target_include_directories(tgfg_input_capture_tests PRIVATE ${FG_INCLUDE} ${BRIDGE_INCLUDE} "${STREAMLINE_ROOT}/include" "${NGX_INCLUDE_DIR}")
    target_link_libraries(tgfg_input_capture_tests PRIVATE d3d11)
    add_test(NAME tgfg_input_capture COMMAND tgfg_input_capture_tests)
    add_executable(tgup_shader_tests tests/upscaling_shader_tests.cpp)
    target_include_directories(tgup_shader_tests PRIVATE ${BRIDGE_INCLUDE} "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(tgup_shader_tests PRIVATE ${TG_DEFINES})
    target_link_libraries(tgup_shader_tests PRIVATE d3d11 d3dcompiler dxgi)
    add_test(NAME tgup_upscale_shaders COMMAND tgup_shader_tests)
    # Runs the whole bridge, both extensions' loading and a real FSR/XeSS upscale on this machine's GPU.
    # Needs a D3D12 GPU and the staged runtimes; reports SKIP (77) without them.
    add_executable(tgup_gpu_smoke tests/upscaling_gpu_smoke.cpp)
    target_include_directories(tgup_gpu_smoke PRIVATE ${BRIDGE_INCLUDE} ${UP_INCLUDE})
    target_compile_definitions(tgup_gpu_smoke PRIVATE ${TG_DEFINES})
    target_link_libraries(tgup_gpu_smoke PRIVATE dxgi d3d11 d3d12)
    add_test(NAME tgup_gpu_smoke COMMAND tgup_gpu_smoke "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Native")
    set_tests_properties(tgup_gpu_smoke PROPERTIES SKIP_RETURN_CODE 77)
    # Unity's order: the DXGI factory and D3D11 device exist before the bridge starts.
    add_test(NAME tgup_gpu_smoke_unity_order COMMAND tgup_gpu_smoke "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Native" --factory-first)
    set_tests_properties(tgup_gpu_smoke_unity_order PROPERTIES SKIP_RETURN_CODE 77)
    # With TG Path Tracing: DLSS Ray Reconstruction (NGX) starts, and denoises and upscales a noisy frame.
    add_test(NAME tgup_gpu_smoke_with_pt COMMAND tgup_gpu_smoke "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Native" --factory-first --with-pt)
    set_tests_properties(tgup_gpu_smoke_with_pt PROPERTIES SKIP_RETURN_CODE 77)
    # All three mods on one bridge, installed as in BepInEx: frame generation's Streamline and Ray Reconstruction's
    # NGX on the same device (Streamline runs when UnityPlayer.dll sits beside the test; see docs/TESTING.md).
    add_test(NAME tgup_gpu_smoke_all_mods COMMAND tgup_gpu_smoke "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Native" --factory-first --with-fg --with-pt --installed)
    set_tests_properties(tgup_gpu_smoke_all_mods PROPERTIES SKIP_RETURN_CODE 77)
    # Frame generation and upscaling without TG Path Tracing, installed: Ray Reconstruction never starts NGX.
    add_test(NAME tgup_gpu_smoke_fg_installed COMMAND tgup_gpu_smoke "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Native" --factory-first --with-fg --installed)
    set_tests_properties(tgup_gpu_smoke_fg_installed PROPERTIES SKIP_RETURN_CODE 77)
    # The path tracer end to end on this machine's GPU: the bridge boots with the extension, a synthetic
    # scene is registered and rasterized into an HDRP-style GBuffer, and frames are traced and composited
    # through the real render events. Writes PNGs of the result and the debug views; skips (77) without
    # a DXR 1.1 GPU or the staged DLLs.
    add_executable(tgpt_gpu_test tests/pathtracing_gpu_test.cpp)
    target_include_directories(tgpt_gpu_test PRIVATE ${BRIDGE_INCLUDE} ${PT_INCLUDE})
    target_compile_definitions(tgpt_gpu_test PRIVATE ${TG_DEFINES})
    target_link_libraries(tgpt_gpu_test PRIVATE dxgi d3d11 d3dcompiler)
    add_test(NAME tgpt_gpu_test COMMAND tgpt_gpu_test "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Native" "${CMAKE_CURRENT_BINARY_DIR}/tgpt-images")
    set_tests_properties(tgpt_gpu_test PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  # Component TGNative: MinHook's own install rules (headers, static library) stay out of the mods.
  install(TARGETS TGGraphicsBridge.Native RUNTIME DESTINATION TGGraphicsBridge COMPONENT TGNative)
  install(TARGETS TGFG.Native RUNTIME DESTINATION TGFrameGeneration COMPONENT TGNative)
  install(TARGETS TGBetterUpscaling.Native RUNTIME DESTINATION TGBetterUpscaling COMPONENT TGNative)
  install(TARGETS TGPathTracing.Native RUNTIME DESTINATION TGPathTracing COMPONENT TGNative)
endif()
