cmake_minimum_required(VERSION 3.28)
project(TaskbarThumbnailReorder VERSION 0.1.0 LANGUAGES C CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "Taskbar Thumbnail Reorder v1 supports only Windows")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Taskbar Thumbnail Reorder v1 supports only x64 Windows")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
include(CTest)

add_library(ttr_build_options INTERFACE)
target_compile_definitions(ttr_build_options INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ttr_build_options INTERFACE /permissive- /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor)
if(TTR_WARNINGS_AS_ERRORS)
  target_compile_options(ttr_build_options INTERFACE /WX)
endif()
if(MSVC_RUNTIME_LIBRARY)
  set_property(TARGET ttr_build_options PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

configure_file(include/ttr_version.h.in generated/ttr_version.h @ONLY)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/compat/manifest-public-key.bin" TTR_PUBLIC_KEY_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host/resources/icon-enabled.ico" TTR_ICON_ENABLED_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host/resources/icon-disabled.ico" TTR_ICON_DISABLED_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host/resources/icon-warning.ico" TTR_ICON_WARNING_PATH)
configure_file(host/resources/app.rc.in generated/app.rc @ONLY)

add_library(ttr_common STATIC
  common/manifest.cpp
  common/pe_identity.cpp
  common/sha256.cpp)
target_include_directories(ttr_common PUBLIC include common "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ttr_common PUBLIC ttr_build_options bcrypt version ole32)

set(MINHOOK_SOURCES
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c)
add_library(minhook STATIC ${MINHOOK_SOURCES})
target_include_directories(minhook PUBLIC third_party/minhook/include)
target_compile_definitions(minhook PRIVATE WIN32_LEAN_AND_MEAN)
set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_program(CPPWINRT_EXE cppwinrt.exe
  HINTS
    "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x86")
if(NOT CPPWINRT_EXE)
  message(FATAL_ERROR "cppwinrt.exe is required (install the Windows SDK C++/WinRT tools)")
endif()
set(MUX_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mux")
set(MUX_GENERATED_STAMP "${MUX_GENERATED_DIR}/projection.stamp")
set(WEBVIEW2_VERSION "1.0.2849.39")
set(WEBVIEW2_PACKAGE "${CMAKE_CURRENT_BINARY_DIR}/_deps/microsoft.web.webview2.${WEBVIEW2_VERSION}.nupkg")
set(WEBVIEW2_ROOT "${CMAKE_CURRENT_BINARY_DIR}/_deps/microsoft.web.webview2.${WEBVIEW2_VERSION}")
set(WEBVIEW2_WINMD "${WEBVIEW2_ROOT}/lib/Microsoft.Web.WebView2.Core.winmd")
if(NOT EXISTS "${WEBVIEW2_WINMD}")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps")
  file(DOWNLOAD
    "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${WEBVIEW2_VERSION}/microsoft.web.webview2.${WEBVIEW2_VERSION}.nupkg"
    "${WEBVIEW2_PACKAGE}"
    EXPECTED_HASH "SHA256=099ec21f62e0e29ef9d9a991d096421efb256bdfdb64984216395736043560fd"
    TLS_VERIFY ON)
  file(ARCHIVE_EXTRACT INPUT "${WEBVIEW2_PACKAGE}" DESTINATION "${WEBVIEW2_ROOT}")
endif()
add_custom_command(OUTPUT "${MUX_GENERATED_STAMP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${MUX_GENERATED_DIR}"
  COMMAND "${CPPWINRT_EXE}" -input "${CMAKE_CURRENT_SOURCE_DIR}/third_party/microsoft-ui-xaml/Microsoft.UI.Xaml.winmd" -input "${WEBVIEW2_WINMD}" -reference "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" -output "${MUX_GENERATED_DIR}" -overwrite
  COMMAND "${CMAKE_COMMAND}" -E touch "${MUX_GENERATED_STAMP}"
  DEPENDS third_party/microsoft-ui-xaml/Microsoft.UI.Xaml.winmd
  VERBATIM)
add_custom_target(mux_projection DEPENDS "${MUX_GENERATED_STAMP}")

add_library(TTRHook64 SHARED
  payload/dllmain.cpp
  payload/bootstrap_hook.cpp
  payload/payload_state.cpp
  payload/manifest_runtime.cpp
  payload/hook_transaction.cpp
  payload/task_model.cpp
  payload/classic_backend.cpp
  payload/xaml_backend.cpp
  payload/winrt_visual_tree.cpp
  payload/payload_log.cpp
  payload/exports.def)
target_include_directories(TTRHook64 PRIVATE include payload)
target_include_directories(TTRHook64 PRIVATE "${MUX_GENERATED_DIR}")
target_link_libraries(TTRHook64 PRIVATE ttr_build_options ttr_common minhook comctl32 ole32 runtimeobject)
add_dependencies(TTRHook64 mux_projection)
set_target_properties(TTRHook64 PROPERTIES OUTPUT_NAME TTRHook64 PREFIX "")

set(PAYLOAD_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/payload.rc")
add_custom_command(OUTPUT "${PAYLOAD_RC}"
  COMMAND "${CMAKE_COMMAND}" -DINPUT=$<TARGET_FILE:TTRHook64> -DOUTPUT=${PAYLOAD_RC}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WritePayloadResource.cmake"
  DEPENDS TTRHook64 cmake/WritePayloadResource.cmake
  VERBATIM)

add_executable(TaskbarThumbnailReorder WIN32
  host/main.cpp host/app.cpp host/tray_icon.cpp host/explorer_session.cpp
  host/hook_loader.cpp host/manifest_store.cpp host/manifest_update.cpp
  host/autostart.cpp host/settings.cpp host/diagnostics.cpp host/cleanup.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc" "${PAYLOAD_RC}")
target_include_directories(TaskbarThumbnailReorder PRIVATE include host "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(TaskbarThumbnailReorder PRIVATE ttr_build_options ttr_common
  comctl32 shell32 shlwapi userenv winhttp ole32 comdlg32 advapi32)

add_executable(moduleid tools/moduleid/main.cpp)
target_link_libraries(moduleid PRIVATE ttr_common)

add_executable(manifestc tools/manifestc/main.cpp)
target_include_directories(manifestc PRIVATE third_party/nlohmann)
target_link_libraries(manifestc PRIVATE ttr_common)

add_executable(manifestsign tools/manifestsign/main.cpp)
target_link_libraries(manifestsign PRIVATE ttr_common ncrypt)

add_executable(compatgen tools/compatgen/main.cpp)
target_include_directories(compatgen PRIVATE third_party/nlohmann)
target_include_directories(compatgen PRIVATE "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/include")
target_link_directories(compatgen PRIVATE "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/lib/amd64")
target_link_libraries(compatgen PRIVATE ttr_common diaguids)

if(BUILD_TESTING)
  add_executable(ttr_unit_tests tests/unit/main.cpp)
  target_link_libraries(ttr_unit_tests PRIVATE ttr_common)
  add_test(NAME ttr_unit_tests COMMAND ttr_unit_tests)
endif()
