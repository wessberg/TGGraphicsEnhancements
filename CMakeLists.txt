# SPDX-License-Identifier: MIT
# Three native DLLs, one per mod:
#   TGGraphicsBridge.Native   TG Graphics Bridge: hooks, the game's device, the shared D3D12 device
#   TGFG.Native               TG Frame Generation extension: presentation bridge, DLSS-G, FSR/XeSS FG
#   TGBetterUpscaling.Native  TG Better Upscaling extension: FSR and XeSS upscaling where HDRP runs STP, and
#                             the bundled DLSS DLL for the game's own DLSS
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
                 Kits/FidelityFX/upscalers/include/ffx_upscale.h)
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
  set(TG_DEFINES UNICODE _UNICODE _WIN32_WINNT=0x0A00)

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
    native/upscaling/src/nvidia_profile.cpp)
  target_include_directories(TGBetterUpscaling.Native PRIVATE ${UP_INCLUDE} ${BRIDGE_INCLUDE} "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${FIDELITYFX_ROOT}/Kits/FidelityFX/api/include" "${FIDELITYFX_ROOT}/Kits/FidelityFX/upscalers/include"
    "${XESS_ROOT}/inc/xess")
  target_compile_definitions(TGBetterUpscaling.Native PRIVATE ${TG_DEFINES})
  target_link_libraries(TGBetterUpscaling.Native PRIVATE dxgi d3d11 d3d12 d3dcompiler dxguid version)

  if(MSVC)
    foreach(target TGGraphicsBridge.Native TGFG.Native TGBetterUpscaling.Native)
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
  endif()
  # Component TGNative: MinHook's own install rules (headers, static library) stay out of the mods.
  install(TARGETS TGGraphicsBridge.Native RUNTIME DESTINATION TGGraphicsBridge COMPONENT TGNative)
  install(TARGETS TGFG.Native RUNTIME DESTINATION TGFrameGeneration COMPONENT TGNative)
  install(TARGETS TGBetterUpscaling.Native RUNTIME DESTINATION TGBetterUpscaling COMPONENT TGNative)
endif()
