# SPDX-License-Identifier: MIT
# Four native DLLs, one per mod:
#   TGGraphicsBridge.Native   TG Graphics Bridge: hooks, the game's device, the shared D3D12 device
#   TGFG.Native               TG Frame Generation extension: presentation bridge, DLSS-G, FSR/XeSS FG
#   TGBetterUpscaling.Native  TG Better Upscaling extension: FSR and XeSS upscaling where HDRP runs STP, and
#                             the bundled DLSS DLL for the game's own DLSS
#   TGPathTracing.Native      TG Path Tracing extension: ReSTIR path tracing on the shared D3D12 device
cmake_minimum_required(VERSION 3.24)
project(TGGraphicsMods VERSION 1.2.4 LANGUAGES C CXX)
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
  set(STREAMLINE_ROOT "" CACHE PATH "NVIDIA Streamline v2.14.16 SDK root (contains include/sl.h)")
  set(NGX_INCLUDE_DIR "" CACHE PATH "Official NVIDIA DLSS/NGX include directory")
  set(MINHOOK_ROOT "" CACHE PATH "Optional local MinHook v1.3.4 source root")
  set(FIDELITYFX_ROOT "" CACHE PATH "AMD FSR (FidelityFX) SDK v2.4.0 root (contains Kits/FidelityFX/api/include)")
  set(XESS_ROOT "" CACHE PATH "Intel XeSS SDK v3.0.2 root (contains inc/xess_fg)")
  set(ANTILAG2_ROOT "" CACHE PATH "AMD Anti-Lag 2 SDK v2.0.0a root (contains ffx_antilag2_dx12.h)")
  set(DLSS_ROOT "" CACHE PATH "NVIDIA DLSS SDK 310.9.16 root (include/nvsdk_ngx_helpers_dlssd_d3d.h, lib/Windows_x86_64/x64/nvsdk_ngx_s.lib)")
  foreach(file include/nvsdk_ngx_helpers_dlssd_d3d.h lib/Windows_x86_64/x64/nvsdk_ngx_s.lib)
    if(NOT EXISTS "${DLSS_ROOT}/${file}")
      message(FATAL_ERROR "Missing ${DLSS_ROOT}/${file}. Set DLSS_ROOT to the NVIDIA DLSS SDK 310.9.16 release (github.com/NVIDIA/DLSS).")
    endif()
  endforeach()
  foreach(header sl.h sl_core_api.h sl_consts.h sl_dlss_g.h sl_reflex.h sl_pcl.h)
    if(NOT EXISTS "${STREAMLINE_ROOT}/include/${header}")
      message(FATAL_ERROR "Missing ${STREAMLINE_ROOT}/include/${header}. Supply the official v2.14.16 SDK.")
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
      message(FATAL_ERROR "Missing ${FIDELITYFX_ROOT}/${header}. Supply the AMD FSR SDK v2.4.0 headers.")
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
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/native/pathtracing/shaders/d3d11.hlsl" TGPT_D3D11_SOURCE)
  # MSVC takes at most 16380 characters in one string literal: the text goes in as adjacent raw literals of 8000 each.
  string(LENGTH "${TGPT_D3D11_SOURCE}" TGPT_D3D11_LENGTH)
  set(TGPT_D3D11_SHADER "")
  set(TGPT_D3D11_OFFSET 0)
  while(TGPT_D3D11_OFFSET LESS TGPT_D3D11_LENGTH)
    string(SUBSTRING "${TGPT_D3D11_SOURCE}" ${TGPT_D3D11_OFFSET} 8000 TGPT_D3D11_CHUNK)
    string(APPEND TGPT_D3D11_SHADER "R\"TGPTHLSL(${TGPT_D3D11_CHUNK})TGPTHLSL\"\n")
    math(EXPR TGPT_D3D11_OFFSET "${TGPT_D3D11_OFFSET}+8000")
  endwhile()
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
  # NVIDIA RTXDI (ReSTIR's temporal and spatial reuse: permutation sampling, the boiling filter, bias correction),
  # fetched beside this file as RTXDI-Library-<version> (or RTXDI_ROOT) and compiled into the path tracer, never
  # committed: the NVIDIA RTX SDKs License allows it only in object code (docs/SOURCES.md).
  set(RTXDI_ROOT "" CACHE PATH "NVIDIA RTXDI-Library root (contains Include/Rtxdi/RtxdiParameters.h)")
  set(TG_RTXDI "${RTXDI_ROOT}")
  if(NOT TG_RTXDI)
    file(GLOB TG_LOCAL_RTXDI LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/RTXDI-Library-*")
    list(SORT TG_LOCAL_RTXDI ORDER DESCENDING)
    if(TG_LOCAL_RTXDI)
      list(GET TG_LOCAL_RTXDI 0 TG_RTXDI)
    endif()
  endif()
  if(NOT EXISTS "${TG_RTXDI}/Include/Rtxdi/RtxdiParameters.h" OR NOT EXISTS "${TG_RTXDI}/Source/RtxdiUtils.cpp")
    message(FATAL_ERROR "NVIDIA RTXDI not found. Clone github.com/NVIDIA-RTX/RTXDI-Library into RTXDI-Library-<version> beside this file, or set RTXDI_ROOT.")
  endif()
  # NVIDIA NRD (Real-Time Denoisers: RELAX, the fallback denoiser for GPUs without Ray Reconstruction or Ray Regeneration),
  # fetched beside this file as NRD-SDK-<version> (or NRD_ROOT) and compiled in as a static library with its shaders
  # embedded as DXIL, never committed: the NVIDIA RTX SDKs License allows it only in object code (docs/SOURCES.md).
  # Its configure step downloads the tools it builds with (ShaderMake, MathLib, DXC). Without it the path tracer builds
  # without NRD, and the built-in denoiser serves.
  option(TGPT_WITH_NRD "Build TG Path Tracing with NVIDIA NRD when its SDK is present" ON)
  set(NRD_ROOT "" CACHE PATH "NVIDIA NRD root (contains Include/NRD.h)")
  set(TG_NRD "${NRD_ROOT}")
  if(NOT TG_NRD)
    file(GLOB TG_LOCAL_NRD LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/NRD-SDK-*")
    list(SORT TG_LOCAL_NRD ORDER DESCENDING)
    if(TG_LOCAL_NRD)
      list(GET TG_LOCAL_NRD 0 TG_NRD)
    endif()
  endif()
  set(TGPT_NRD OFF)
  if(TGPT_WITH_NRD AND EXISTS "${TG_NRD}/Include/NRD.h")
    # The release the integration was checked against (nrd_denoiser.cpp, pt_nrd_prep.hlsl): another one is refused
    # rather than compiled in unnoticed.
    file(STRINGS "${TG_NRD}/Include/NRD.h" TG_NRD_VERSION REGEX "#define NRD_VERSION_(MAJOR|MINOR) ")
    if(NOT TG_NRD_VERSION MATCHES "NRD_VERSION_MAJOR 4" OR NOT TG_NRD_VERSION MATCHES "NRD_VERSION_MINOR 17")
      message(FATAL_ERROR "TG Path Tracing is integrated with NVIDIA NRD 4.17 (${TG_NRD} is another version): check out v4.17.3, or configure with -DTGPT_WITH_NRD=OFF")
    endif()
    set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
    set(NRD_NRI OFF CACHE BOOL "" FORCE)
    set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "" FORCE)
    set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
    set(NRD_EMBEDS_DXIL_SHADERS ON CACHE BOOL "" FORCE)
    set(NRD_SUPPORTS_HISTORY_CONFIDENCE ON CACHE BOOL "" FORCE) # PT-127: optional lighting-gradient inputs
    set(NRD_SHADERS_PATH "${CMAKE_CURRENT_BINARY_DIR}/nrd-shaders" CACHE STRING "" FORCE)
    # A worker may only read the SDK: NRD's configure writes NRDConfig.hlsli in its source tree.
    # An already built library with the same SDK/configuration avoids that write (no integration change).
    set(TGPT_NRD_PREBUILT_LIBRARY "" CACHE FILEPATH "Optional existing NRD 4.17 static library for a read-only SDK")
    if(TGPT_NRD_PREBUILT_LIBRARY)
      set(TGPT_NRD_PREBUILT_DEPS "" CACHE STRING "Libraries required by the existing NRD static library")
      add_library(NRD STATIC IMPORTED)
      set_target_properties(NRD PROPERTIES IMPORTED_LOCATION "${TGPT_NRD_PREBUILT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TG_NRD}/Include" INTERFACE_COMPILE_DEFINITIONS "NRD_STATIC_LIBRARY=1"
        INTERFACE_LINK_LIBRARIES "${TGPT_NRD_PREBUILT_DEPS}")
    else()
      add_subdirectory("${TG_NRD}" "${CMAKE_CURRENT_BINARY_DIR}/nrd" EXCLUDE_FROM_ALL)
    endif()
    set(TGPT_NRD ON)
    message(STATUS "TG Path Tracing: NVIDIA NRD from ${TG_NRD}")
  else()
    message(STATUS "TG Path Tracing: no NVIDIA NRD (NRD-SDK-<version> beside this file, or NRD_ROOT); the built-in denoiser serves")
  endif()
  set(PT_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native/pathtracing/shaders")
  set(PT_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/pathtracing")
  file(MAKE_DIRECTORY "${PT_GENERATED}")
  configure_file(native/pathtracing/shaders/pt_shaders.hpp.in "${PT_GENERATED}/pt_shaders.hpp" COPYONLY)
  set(PT_SHADER_HEADERS)
  # The measuring build (PT-99, docs/agents/speed-1.md; pt_common speed_measure): OFF for every build that is installed,
  # deployed or packaged. ON compiles the switches in that leave parts of the tracing passes out (and the picture with
  # them), to time those parts by the stages' GPU times: for a build of its own, in a build folder of its own.
  option(TGPT_SPEED_MEASURE "TG Path Tracing: compile the passes' timing switches in (never for a build that is deployed)" OFF)
  set(PT_SPEED_DEFINES)
  if(TGPT_SPEED_MEASURE)
    set(PT_SPEED_DEFINES -D PT_SPEED_MEASURE)
    message(WARNING "TG Path Tracing: TGPT_SPEED_MEASURE is ON: a measuring build, never to be deployed or packaged")
  endif()
  # The "Lamp trial lit surroundings" trial (PT-85 and PT-40, docs/agents/flame-near.md; pt_common lamp_around), compiled
  # into the passes or not. OFF, the default since 2026-10-09 (PT-99, docs/agents/speed-1.md): its code is in no pass, the
  # hidden setting does nothing (the tracer's log says so when it is set), and what the passes compute with the setting
  # at 0 is the same. The game's own logs: the tavern's bounced light went from 29.4 to 61.9 ms in the build that brought
  # the trial in, with the setting at 0. ON: the trial as it was, for a build that is to try it (recipes lamp-near,
  # near-sweep, watch-near).
  option(TGPT_LAMP_AROUND "TG Path Tracing: compile the 'Lamp trial lit surroundings' trial into the passes" OFF)
  if(NOT TGPT_LAMP_AROUND)
    list(APPEND PT_SPEED_DEFINES -D PT_NO_LAMP_AROUND)
  endif()
  # The rain's wetness (PT-108 and PT-104, docs/agents/rain-1.md; pt_common rain_roughness), ON TRIAL: compiled into the
  # passes or not. OFF, the default until the game has answered: no pass holds a line of it, the plugin's hidden
  # "Rain wetness" does nothing (the tracer's log says so when it is set), and every pass computes what it did. ON: a
  # hit's roughness is wetted as the game's own wetness pass wets the camera's (recipe `rain`, scene spec-terrain).
  option(TGPT_RAIN_WETNESS "TG Path Tracing: compile the rain's wetness (on trial) into the passes" OFF)
  if(TGPT_RAIN_WETNESS)
    list(APPEND PT_SPEED_DEFINES -D PT_RAIN_WETNESS)
  endif()
  # NVIDIA RTX Mega Geometry (PT-109, docs/agents/megageo-1.md; native/pathtracing/src/mega_geometry.hpp), ON TRIAL:
  # cluster-level acceleration structures and a partitioned top level, through NVAPI on Direct3D 12. OFF, the default:
  # the tracer holds no line of it and links no NVAPI: the build of every day. ON: the tracer asks the driver at start-up
  # what it reports (never a table of cards) and logs it; the plain acceleration structures stay the default and serve
  # whenever the driver does not report a part or a call fails. NVAPI (github.com/NVIDIA/nvapi, MIT) is fetched beside
  # this file as NVAPI-<release> (or NVAPI_ROOT), never committed; its nvapi64.lib only finds the installed driver's
  # nvapi64.dll at run time, so nothing is added to a package.
  option(TGPT_MEGA_GEOMETRY "TG Path Tracing: compile the NVIDIA RTX Mega Geometry path (through NVAPI; on trial) in" OFF)
  if(TGPT_MEGA_GEOMETRY)
    set(NVAPI_ROOT "" CACHE PATH "NVIDIA NVAPI root (contains nvapi.h and amd64/nvapi64.lib)")
    set(TG_NVAPI "${NVAPI_ROOT}")
    if(NOT TG_NVAPI)
      file(GLOB TG_LOCAL_NVAPI LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/NVAPI-*")
      list(SORT TG_LOCAL_NVAPI ORDER DESCENDING)
      if(TG_LOCAL_NVAPI)
        list(GET TG_LOCAL_NVAPI 0 TG_NVAPI)
      endif()
    endif()
    if(NOT EXISTS "${TG_NVAPI}/nvapi.h" OR NOT EXISTS "${TG_NVAPI}/amd64/nvapi64.lib")
      message(FATAL_ERROR "TGPT_MEGA_GEOMETRY needs NVIDIA NVAPI. Clone github.com/NVIDIA/nvapi into NVAPI-<release> beside this file, or set NVAPI_ROOT.")
    endif()
    message(STATUS "TG Path Tracing: NVIDIA RTX Mega Geometry (on trial) with NVAPI from ${TG_NVAPI}")
    # The clusters' cut (PT-109's plants as clusters, docs/agents/megageo-4.md): NVIDIA's own clusterizer, which its samples
    # take from meshoptimizer (meshopt_buildMeshletsSpatial; vk_animated_clusters, vk_lod_clusters, docs/agents/megageo-3.md).
    # meshoptimizer (github.com/zeux/meshoptimizer, MIT) is fetched here at its release v1.3 and linked in statically, never
    # committed; its notice goes beside the DLL. A tree configured with FETCHCONTENT_FULLY_DISCONNECTED=ON (build/native)
    # fetches it once with that OFF, or names a checkout of v1.3 in FETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER.
    include(FetchContent)
    set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
    set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(meshoptimizer GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git GIT_TAG v1.3 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(meshoptimizer)
  endif()
  # ON TRIAL (PT-99, docs/agents/perf-1.md; pt_water_path.hlsli water_regroup): the water's path regroups its threads by
  # what they do next (Shader Execution Reordering). OFF, the default: no pass holds a line of it. ON: the two reordering
  # passes alone are compiled with it (every other pass is the same to the byte), and it runs only with its numbers of the
  # hidden "Speed trial": 2, at every turn of the path's loop; 4, before a point makes its own patch of waves. These ADD
  # regroupings inside loops: what the driver makes of the two passes with them is not known (the offline test could not
  # make them under its memory ceiling, 2026-10-09). For a build that is to try it; never together with
  # TGPT_SPEED_MEASURE.
  # (Two trials that stood here until 2026-10-10 are the rule in every build since, with no switch: the two passes' own
  # regrouping also by whether the pixel feeds the radiance cache, pt_common.hlsli regroup_key; and the lamps a ray off
  # water or glass runs into found by a search tree, pt_common.hlsli reflected_lamps and
  # native/pathtracing/src/lamp_tree.hpp. A third, the water's short rays, came out. docs/agents/rule-1.md.)
  option(TGPT_REGROUP_WATER "TG Path Tracing: on trial, the water's path regroups its threads inside the reordering passes" OFF)
  set(PT_REGROUP_DEFINES)
  if(TGPT_REGROUP_WATER)
    list(APPEND PT_REGROUP_DEFINES -D PT_REGROUP_WATER)
  endif()
  if(TGPT_REGROUP_WATER AND TGPT_SPEED_MEASURE)
    message(FATAL_ERROR "TG Path Tracing: TGPT_REGROUP_WATER and TGPT_SPEED_MEASURE give the Speed trial's numbers 2 and 4 two meanings: build them apart")
  endif()
  # Each pass twice: Shader Model 6.6 for every DXR 1.1 GPU, and 6.9 (DirectX Raytracing 1.2: opacity
  # micromaps) for where the device offers it; the GI and reflection passes also as ray generation shaders
  # with Shader Execution Reordering. The tracer picks a set when it starts.
  # The shaders' own headers and RTXDI's: an edit to either rebuilds every pass (its parameter blocks are in the
  # constant layout the C++ side fills).
  file(GLOB_RECURSE TG_RTXDI_HEADERS "${TG_RTXDI}/Include/*.h" "${TG_RTXDI}/Include/*.hlsli")
  set(PT_SHADER_DEPENDS "${PT_SHADER_DIR}/pt_psr_record.hlsli" "${PT_SHADER_DIR}/pt_psr_capture.hlsli" "${PT_SHADER_DIR}/pt_decal_graphs.hlsli" "${PT_SHADER_DIR}/pt_common.hlsli" "${PT_SHADER_DIR}/pt_restir.hlsli" "${PT_SHADER_DIR}/pt_root.hlsli"
    "${PT_SHADER_DIR}/pt_rtxdi.hlsli" "${PT_SHADER_DIR}/pt_boil.hlsli" "${PT_SHADER_DIR}/pt_primary.hlsli" "${PT_SHADER_DIR}/pt_waves.hlsli" "${PT_SHADER_DIR}/pt_materials.hlsli" "${PT_SHADER_DIR}/pt_dielectric.hlsli"
    "${PT_SHADER_DIR}/pt_interface.hlsli" "${PT_SHADER_DIR}/pt_medium.hlsli" "${PT_SHADER_DIR}/pt_query.hlsli" "${PT_SHADER_DIR}/pt_water_surface.hlsli" "${PT_SHADER_DIR}/pt_water_path.hlsli" "${PT_SHADER_DIR}/pt_water_sun.hlsli" "${PT_SHADER_DIR}/pt_water_foam.hlsli" "${PT_SHADER_DIR}/pt_water_lights.hlsli"
    "${PT_SHADER_DIR}/pt_gi_own.hlsli" ${TG_RTXDI_HEADERS})
  # giReuse (pt_gi_reuse): ReSTIR GI's reuse across frames, a pass of its own right after pt_gi, in the screen's order
  # (PT-99, PT-110, since 2026-10-10: docs/agents/rule-2.md).
  foreach(entry prepare:pt_prepare probe:pt_probe gi:pt_gi giReuse:pt_gi_reuse giSpatial:pt_gi_spatial di:pt_di diSpatial:pt_di_spatial specular:pt_specular
                temporal:pt_temporal atrous:pt_atrous compose:pt_compose caustics:pt_caustics walk:pt_walk wet:pt_wet
                fogFroxel:pt_fog_froxel fogIntegrate:pt_fog_integrate fogTail:pt_fog_tail
                nrdConfidence:pt_nrd_confidence)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 variable)
    list(GET parts 1 name)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}.h"
      COMMAND "${TG_DXC}" -nologo -T cs_6_6 -E main ${PT_SPEED_DEFINES} -HV 2021 -O3 -WX -Qstrip_debug -I "${TG_RTXDI}/Include" -Fh "${PT_GENERATED}/${name}.h" -Vn ${variable} "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl" VERBATIM)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}_dxr12.h"
      COMMAND "${TG_DXC}" -nologo -T cs_6_9 -E main -D PT_DXR12 ${PT_SPEED_DEFINES} -HV 2021 -O3 -WX -Qstrip_debug -I "${TG_RTXDI}/Include" -Fh "${PT_GENERATED}/${name}_dxr12.h" -Vn ${variable}Dxr12 "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl (Shader Model 6.9)" VERBATIM)
    list(APPEND PT_SHADER_HEADERS "${PT_GENERATED}/${name}.h" "${PT_GENERATED}/${name}_dxr12.h")
  endforeach()
  foreach(header pt_fog_froxel.h pt_fog_froxel_dxr12.h pt_fog_integrate.h pt_fog_integrate_dxr12.h pt_fog_tail.h pt_fog_tail_dxr12.h)
    add_custom_command(OUTPUT "${PT_GENERATED}/${header}" APPEND DEPENDS "${PT_SHADER_DIR}/pt_froxel.hlsli")
  endforeach()
  foreach(entry gi:pt_gi specular:pt_specular)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 variable)
    list(GET parts 1 name)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}_ser.h"
      COMMAND "${TG_DXC}" -nologo -T lib_6_9 -D PT_DXR12 -D PT_SER ${PT_REGROUP_DEFINES} ${PT_SPEED_DEFINES} -HV 2021 -O3 -WX -Qstrip_debug -I "${TG_RTXDI}/Include" -Fh "${PT_GENERATED}/${name}_ser.h" -Vn ${variable}Ser "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl (ray generation, Shader Execution Reordering)" VERBATIM)
    list(APPEND PT_SHADER_HEADERS "${PT_GENERATED}/${name}_ser.h")
  endforeach()
  # (Two options of Shader Execution Reordering stood here until 2026-10-10: the reordering study, the bounced light's pass
  # in more arrangements, and the split trial, that pass split in two. The game answered: pt_gi regroups once after the
  # pixel's first ray and its reuse across frames is pt_gi_reuse, in every build; the rest came out. docs/agents/rule-2.md.)
  # Passes that trace no rays: one Shader Model 6.6 build serves both sets (the opacity micromap bake, AMD Ray
  # Regeneration's guides, characters' skinned positions, RTXDI's boiling filter for the GI reservoirs, the radiance
  # cache's frame, the emissive triangles' records, swaying cloth's pose, the water's wave triangles, its wave corners kept for the sun's question, and texture sharing's start-up self-test, which
  # texture_share.cpp includes on its own, with its own root signature).
  foreach(entry ommBake:pt_omm_bake rrgGuides:pt_rrg_guides skinUnpack:pt_skin giBoil:pt_gi_boil meter:pt_meter cache:pt_cache emissive:pt_emissive sway:pt_sway waterMesh:pt_water_mesh sunGrid:pt_sun_grid shareTest:pt_share_test yardstick:pt_yardstick sky:pt_sky nrdConfidenceFilter:pt_nrd_confidence_filter)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 variable)
    list(GET parts 1 name)
    add_custom_command(OUTPUT "${PT_GENERATED}/${name}.h"
      COMMAND "${TG_DXC}" -nologo -T cs_6_6 -E main -HV 2021 -O3 -WX -Qstrip_debug -I "${TG_RTXDI}/Include" -Fh "${PT_GENERATED}/${name}.h" -Vn ${variable} "${PT_SHADER_DIR}/${name}.hlsl"
      DEPENDS "${PT_SHADER_DIR}/${name}.hlsl" ${PT_SHADER_DEPENDS}
      COMMENT "DXC ${name}.hlsl" VERBATIM)
    list(APPEND PT_SHADER_HEADERS "${PT_GENERATED}/${name}.h")
  endforeach()
  # PT-112, the sky (on trial): its model (pt_sky.hlsli) is read by its own passes and the composite alone.
  foreach(header pt_sky.h pt_compose.h pt_compose_dxr12.h)
    add_custom_command(OUTPUT "${PT_GENERATED}/${header}" APPEND DEPENDS "${PT_SHADER_DIR}/pt_sky.hlsli")
  endforeach()
  # NRD's front end (the tracer's signals into its inputs), with NRD's own packing from its NRD.hlsli.
  if(TGPT_NRD)
    add_custom_command(OUTPUT "${PT_GENERATED}/pt_nrd_prep.h"
      COMMAND "${TG_DXC}" -nologo -T cs_6_6 -E main -HV 2021 -O3 -WX -Qstrip_debug -I "${TG_RTXDI}/Include" -I "${TG_NRD}/Shaders" -Fh "${PT_GENERATED}/pt_nrd_prep.h" -Vn nrdPrep "${PT_SHADER_DIR}/pt_nrd_prep.hlsl"
      DEPENDS "${PT_SHADER_DIR}/pt_nrd_prep.hlsl" ${PT_SHADER_DEPENDS} "${TG_NRD}/Shaders/NRD.hlsli" "${TG_NRD}/Shaders/NRDConfig.hlsli"
      COMMENT "DXC pt_nrd_prep.hlsl" VERBATIM)
    list(APPEND PT_SHADER_HEADERS "${PT_GENERATED}/pt_nrd_prep.h")
  endif()
  add_custom_target(TGPathTracingShaders DEPENDS ${PT_SHADER_HEADERS})

  add_library(TGGraphicsBridge.Native SHARED
    native/bridge/src/core.cpp native/bridge/src/hooks.cpp native/bridge/src/device.cpp
    native/bridge/src/extensions.cpp native/bridge/src/exports.cpp native/bridge/src/performance.cpp
    native/bridge/src/agility.cpp native/bridge/src/diagnostics.cpp native/bridge/src/ngx_session.cpp native/bridge/src/dlss_inputs.cpp)
  target_include_directories(TGGraphicsBridge.Native PRIVATE ${BRIDGE_INCLUDE} "${NGX_INCLUDE_DIR}")
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
    native/pathtracing/src/extension.cpp native/pathtracing/src/scene.cpp native/pathtracing/src/scene_water.cpp native/pathtracing/src/tracer.cpp
    native/pathtracing/src/ray_regeneration.cpp native/pathtracing/src/nrd_denoiser.cpp native/pathtracing/src/texture_share.cpp "${TG_RTXDI}/Source/RtxdiUtils.cpp")
  add_dependencies(TGPathTracing.Native TGPathTracingShaders)
  if(TGPT_NRD)
    target_link_libraries(TGPathTracing.Native PRIVATE NRD)
    target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_NRD=1)
  endif()
  # AMD FSR Ray Regeneration: the FSR API's headers; the runtime is AMD's signed DLLs, loaded at run time.
  target_include_directories(TGPathTracing.Native PRIVATE ${PT_INCLUDE} ${BRIDGE_INCLUDE} "${CMAKE_CURRENT_BINARY_DIR}/generated" "${PT_GENERATED}"
    "${FIDELITYFX_ROOT}/Kits/FidelityFX/api/include" "${FIDELITYFX_ROOT}/Kits/FidelityFX/denoisers/include" "${TG_RTXDI}/Include")
  target_compile_definitions(TGPathTracing.Native PRIVATE ${TG_DEFINES})
  if(NOT TGPT_LAMP_AROUND)
    target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_NO_LAMP_AROUND=1) # the tracer's log says the trial is compiled out
  endif()
  if(TGPT_RAIN_WETNESS)
    target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_RAIN_WETNESS=1) # without it the tracer's log says the trial is compiled out
  endif()
  if(TGPT_MEGA_GEOMETRY)
    # NVAPI's static library is the door to the driver's own nvapi64.dll: no Direct3D 12 import of its own.
    target_sources(TGPathTracing.Native PRIVATE native/pathtracing/src/mega_geometry.cpp)
    target_include_directories(TGPathTracing.Native PRIVATE "${TG_NVAPI}")
    # (And meshoptimizer's clusterizer, static: the clusters' cut, mega_geometry.cpp.)
    target_link_libraries(TGPathTracing.Native PRIVATE "${TG_NVAPI}/amd64/nvapi64.lib" meshoptimizer)
    target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_MEGA_GEOMETRY=1)
  endif()
  if(TGPT_REGROUP_WATER)
    target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_REGROUP_WATER=1) # the tracer's log says whether the trial is compiled in
  endif()
  if(TGPT_SPEED_MEASURE)
    target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_SPEED_MEASURE=1) # the measuring build's own switches of the tracer (tracer.cpp)
  endif()
  target_link_libraries(TGPathTracing.Native PRIVATE d3d11 d3dcompiler dxguid)

  add_library(TGNeuralRendering.Native SHARED native/neural/src/extension.cpp)
  # The model's caller-name requirement stays inside this optional NR-only adapter (NR-30/31/32).
  add_library(TGNeuralRendering.Community SHARED native/neural/src/community_adapter.cpp)
  set_target_properties(TGNeuralRendering.Community PROPERTIES OUTPUT_NAME "TGNeuralRendering.nvngx")
  target_include_directories(TGNeuralRendering.Community PRIVATE native/neural/include "${DLSS_ROOT}/include")
  target_compile_definitions(TGNeuralRendering.Community PRIVATE ${TG_DEFINES})
  target_link_libraries(TGNeuralRendering.Community PRIVATE bcrypt)
  add_dependencies(TGNeuralRendering.Native TGNeuralRendering.Community)
  target_include_directories(TGNeuralRendering.Native PRIVATE native/neural/include ${BRIDGE_INCLUDE} "${DLSS_ROOT}/include")
  target_compile_definitions(TGNeuralRendering.Native PRIVATE ${TG_DEFINES})
  target_link_libraries(TGNeuralRendering.Native PRIVATE d3d11 d3d12 d3dcompiler dxguid version bcrypt
    "${DLSS_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
  install(TARGETS TGNeuralRendering.Native TGNeuralRendering.Community RUNTIME DESTINATION TGNeuralRendering COMPONENT TGNative)
  install(FILES "${DLSS_ROOT}/LICENSE.txt" DESTINATION TGNeuralRendering/Notices RENAME LICENSE-NVIDIA-RTX-SDKs.txt COMPONENT TGNative)
  install(TARGETS TGNeuralRendering.Native TGNeuralRendering.Community RUNTIME DESTINATION TGNeuralRendering COMPONENT TGNRProbe)
  install(TARGETS TGGraphicsBridge.Native RUNTIME DESTINATION TGGraphicsBridge COMPONENT TGNRProbe)
  install(TARGETS TGFG.Native RUNTIME DESTINATION TGFrameGeneration COMPONENT TGNRProbe)
  install(TARGETS TGBetterUpscaling.Native RUNTIME DESTINATION TGBetterUpscaling COMPONENT TGNRProbe)
  if(BUILD_TESTING)
    add_executable(tgnr_policy_tests tests/neural_policy_tests.cpp)
    target_include_directories(tgnr_policy_tests PRIVATE native/neural/include)
    add_test(NAME tgnr_policy COMMAND tgnr_policy_tests)
    add_executable(tgnr_runtime_tests tests/neural_runtime_tests.cpp)
    target_include_directories(tgnr_runtime_tests PRIVATE native/neural/include native/neural/src "${DLSS_ROOT}/include")
    target_link_libraries(tgnr_runtime_tests PRIVATE bcrypt)
    add_dependencies(tgnr_runtime_tests TGNeuralRendering.Community)
    add_test(NAME tgnr_runtime COMMAND tgnr_runtime_tests "${CMAKE_CURRENT_BINARY_DIR}/nr-runtime-test" "$<TARGET_FILE:TGNeuralRendering.Community>")
    add_library(tgnr_mock_runtime SHARED tests/neural_mock_runtime.cpp)
    target_include_directories(tgnr_mock_runtime PRIVATE "${DLSS_ROOT}/include")
    add_executable(tgnr_adapter_test tests/neural_adapter_test.cpp)
    target_include_directories(tgnr_adapter_test PRIVATE native/neural/include native/neural/src "${DLSS_ROOT}/include")
    target_link_libraries(tgnr_adapter_test PRIVATE bcrypt)
    add_dependencies(tgnr_adapter_test TGNeuralRendering.Community tgnr_mock_runtime)
    foreach(case success changed api unsupported)
      add_test(NAME tgnr_adapter_${case} COMMAND tgnr_adapter_test "$<TARGET_FILE:TGNeuralRendering.Community>" "$<TARGET_FILE:tgnr_mock_runtime>" ${case})
    endforeach()
    add_executable(tgnr_gpu_smoke tests/neural_gpu_smoke.cpp)
    target_include_directories(tgnr_gpu_smoke PRIVATE native/neural/include ${BRIDGE_INCLUDE})
    target_link_libraries(tgnr_gpu_smoke PRIVATE dxgi d3d11)
  endif()

  if(MSVC)
    foreach(target TGGraphicsBridge.Native TGFG.Native TGBetterUpscaling.Native TGPathTracing.Native TGNeuralRendering.Native TGNeuralRendering.Community)
      target_compile_options(${target} PRIVATE /W4 /EHsc /permissive- /utf-8)
    endforeach()
  endif()
  if(BUILD_TESTING)
    # The staged native DLLs the GPU tests load (build.cmd install stages them there).
    set(TG_TEST_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Native" CACHE PATH "Staged native DLLs the GPU tests load")
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
    add_test(NAME tgup_gpu_smoke COMMAND tgup_gpu_smoke "${TG_TEST_NATIVE_DIR}")
    set_tests_properties(tgup_gpu_smoke PROPERTIES SKIP_RETURN_CODE 77)
    # Unity's order: the DXGI factory and D3D11 device exist before the bridge starts.
    add_test(NAME tgup_gpu_smoke_unity_order COMMAND tgup_gpu_smoke "${TG_TEST_NATIVE_DIR}" --factory-first)
    set_tests_properties(tgup_gpu_smoke_unity_order PROPERTIES SKIP_RETURN_CODE 77)
    # With TG Path Tracing: DLSS Ray Reconstruction (NGX) starts, and denoises and upscales a noisy frame.
    add_test(NAME tgup_gpu_smoke_with_pt COMMAND tgup_gpu_smoke "${TG_TEST_NATIVE_DIR}" --factory-first --with-pt)
    set_tests_properties(tgup_gpu_smoke_with_pt PROPERTIES SKIP_RETURN_CODE 77)
    # All three mods on one bridge, installed as in BepInEx: frame generation's Streamline and Ray Reconstruction's
    # NGX on the same device (Streamline runs when UnityPlayer.dll sits beside the test; see docs/TESTING.md).
    add_test(NAME tgup_gpu_smoke_all_mods COMMAND tgup_gpu_smoke "${TG_TEST_NATIVE_DIR}" --factory-first --with-fg --with-pt --installed)
    set_tests_properties(tgup_gpu_smoke_all_mods PROPERTIES SKIP_RETURN_CODE 77)
    # Frame generation and upscaling without TG Path Tracing, installed: Ray Reconstruction never starts NGX.
    add_test(NAME tgup_gpu_smoke_fg_installed COMMAND tgup_gpu_smoke "${TG_TEST_NATIVE_DIR}" --factory-first --with-fg --installed)
    set_tests_properties(tgup_gpu_smoke_fg_installed PROPERTIES SKIP_RETURN_CODE 77)
    # The path tracer end to end on this machine's GPU: the bridge boots with the extension, a synthetic
    # scene is registered and rasterized into an HDRP-style GBuffer, and frames are traced and composited
    # through the real render events. Writes PNGs of the result and the debug views; skips (77) without
    # a DXR 1.1 GPU or the staged DLLs.
    # Focused diagnostic only; never starts the renderer's full GPU suite.
    add_executable(tgpt_vfx_simulation_test tests/vfx_simulation_test.cpp)
    target_include_directories(tgpt_vfx_simulation_test PRIVATE ${BRIDGE_INCLUDE})
    target_compile_definitions(tgpt_vfx_simulation_test PRIVATE ${TG_DEFINES})
    target_link_libraries(tgpt_vfx_simulation_test PRIVATE minhook d3d11 dxgi d3dcompiler)
    # The lamps' search tree (lamp_tree.hpp; pt_common.hlsli reflected_lamps walks it) against the loop over every lamp
    # it took the place of, lamp by lamp, over rays that graze lamps from a centimetre to kilometres away. On the CPU
    # alone, seconds.
    add_executable(tgpt_lamp_tree_test tests/lamp_tree_test.cpp)
    target_include_directories(tgpt_lamp_tree_test PRIVATE ${PT_INCLUDE})
    add_test(NAME tgpt_lamp_tree COMMAND tgpt_lamp_tree_test)
    # The light grid's cells (light_grid.hpp build_cells: one walk a light; PT-99, docs/agents/reccpu.md) against the loop
    # they took the place of (build_cells_reference), byte by byte, over made-up lights. On the CPU alone, seconds.
    add_executable(tgpt_light_grid_test tests/light_grid_test.cpp)
    target_include_directories(tgpt_light_grid_test PRIVATE ${PT_INCLUDE})
    add_test(NAME tgpt_light_grid COMMAND tgpt_light_grid_test)
    add_executable(tgpt_gpu_test tests/pathtracing_gpu_test.cpp)
    target_include_directories(tgpt_gpu_test PRIVATE ${BRIDGE_INCLUDE} ${PT_INCLUDE})
    target_compile_definitions(tgpt_gpu_test PRIVATE ${TG_DEFINES})
    target_link_libraries(tgpt_gpu_test PRIVATE dxgi d3d11 d3dcompiler)
    if(TGPT_NRD)
      target_compile_definitions(tgpt_gpu_test PRIVATE TGPT_EXPECT_NRD=1) # the staged DLL has NRD: its checks must run
    endif()
    add_test(NAME tgpt_gpu_test COMMAND tgpt_gpu_test "${TG_TEST_NATIVE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tgpt-images")
    set_tests_properties(tgpt_gpu_test PROPERTIES SKIP_RETURN_CODE 77)
    # PT-45 phase 3: the texture-sharing checks alone, with the start-up self-test made to read BC7 sRGB back wrong: that
    # format must stay unshared (its atlas tile serves) while every other is read directly.
    add_test(NAME tgpt_share_selftest_fails_one COMMAND tgpt_gpu_test "${TG_TEST_NATIVE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tgpt-images-share")
    set_tests_properties(tgpt_share_selftest_fails_one PROPERTIES SKIP_RETURN_CODE 77
      ENVIRONMENT "TGPT_TEST_SHARE_ONLY=1;TGPT_TEST_SHARE_BREAK=99;TGPT_TEST_STAGE=tgpt-native-share")
  endif()
  # Component TGNative: MinHook's own install rules (headers, static library) stay out of the mods.
  install(TARGETS TGGraphicsBridge.Native RUNTIME DESTINATION TGGraphicsBridge COMPONENT TGNative)
  install(TARGETS TGFG.Native RUNTIME DESTINATION TGFrameGeneration COMPONENT TGNative)
  install(TARGETS TGBetterUpscaling.Native RUNTIME DESTINATION TGBetterUpscaling COMPONENT TGNative)
  install(TARGETS TGPathTracing.Native RUNTIME DESTINATION TGPathTracing COMPONENT TGNative)
  # The notices of what is compiled into it, next to it: NVIDIA's RTXDI (and NRD) under the NVIDIA RTX SDKs license (object
  # code only), and with NRD its MathLib and ShaderMake's blob reader under MIT (LICENSES/THIRD_PARTY.md).
  install(FILES "${TG_RTXDI}/LICENSE.txt" DESTINATION TGPathTracing/Notices RENAME LICENSE-NVIDIA-RTX-SDKs.txt COMPONENT TGNative)
  if(TGPT_MEGA_GEOMETRY)
    # NVAPI's library is linked into it (MIT: its notice goes with it).
    install(FILES "${TG_NVAPI}/License.txt" DESTINATION TGPathTracing/Notices RENAME LICENSE-NVAPI.txt COMPONENT TGNative)
    # And meshoptimizer's (MIT), whose clusterizer is linked in with it.
    install(FILES "${meshoptimizer_SOURCE_DIR}/LICENSE.md" DESTINATION TGPathTracing/Notices RENAME LICENSE-meshoptimizer.md COMPONENT TGNative)
  endif()
  if(TGPT_NRD AND NOT TGPT_NRD_PREBUILT_LIBRARY)
    FetchContent_GetProperties(mathlib)
    FetchContent_GetProperties(shadermake)
    install(FILES "${mathlib_SOURCE_DIR}/LICENSE.txt" DESTINATION TGPathTracing/Notices RENAME LICENSE-MathLib.txt COMPONENT TGNative)
    install(FILES "${shadermake_SOURCE_DIR}/LICENSE.txt" DESTINATION TGPathTracing/Notices RENAME LICENSE-ShaderMake.txt COMPONENT TGNative)
  endif()
endif()
