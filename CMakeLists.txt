cmake_minimum_required(VERSION 3.28)
project(TaskbarThumbnailReorder VERSION 0.1.0 LANGUAGES C CXX RC)

option(TTR_WARNINGS_AS_ERRORS "Treat first-party warnings as errors" ON)
option(TTR_ENABLE_ANALYZE "Enable MSVC native static analysis" OFF)
option(TTR_ENABLE_ASAN "Enable AddressSanitizer for offline code" OFF)
set(TTR_PUBLIC_KEY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/compat/manifest-public-key.bin"
    CACHE FILEPATH "ECDSA P-256 public key blob embedded in the host")
set(TTR_MANIFEST_URL "" CACHE STRING "Optional HTTPS compatibility-manifest URL")

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
target_compile_options(ttr_build_options INTERFACE /permissive- /W4 /utf-8 /Zc:__cplusplus
  /Zc:preprocessor /experimental:deterministic /Brepro "/pathmap:${CMAKE_BINARY_DIR}=.")
target_link_options(ttr_build_options INTERFACE /Brepro)
if(TTR_WARNINGS_AS_ERRORS)
  target_compile_options(ttr_build_options INTERFACE /WX)
endif()
if(TTR_ENABLE_ANALYZE)
  target_compile_options(ttr_build_options INTERFACE /analyze)
endif()
if(TTR_ENABLE_ASAN)
  target_compile_options(ttr_build_options INTERFACE /fsanitize=address)
  target_link_options(ttr_build_options INTERFACE /INCREMENTAL:NO)
endif()

configure_file(include/ttr_version.h.in generated/ttr_version.h @ONLY)
if(NOT EXISTS "${TTR_PUBLIC_KEY_FILE}")
  message(FATAL_ERROR "TTR_PUBLIC_KEY_FILE does not exist: ${TTR_PUBLIC_KEY_FILE}")
endif()
file(SIZE "${TTR_PUBLIC_KEY_FILE}" TTR_PUBLIC_KEY_SIZE)
if(TTR_PUBLIC_KEY_SIZE LESS 72)
  message(WARNING "The embedded compatibility public key is a development placeholder; signed updates will fail closed")
  set(TTR_DEVELOPMENT_KEY 1)
else()
  set(TTR_DEVELOPMENT_KEY 0)
endif()
file(TO_CMAKE_PATH "${TTR_PUBLIC_KEY_FILE}" TTR_PUBLIC_KEY_PATH)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TTR_PUBLIC_KEY_FILE}")
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host/resources/icon-enabled.ico" TTR_ICON_ENABLED_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host/resources/icon-disabled.ico" TTR_ICON_DISABLED_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host/resources/icon-warning.ico" TTR_ICON_WARNING_PATH)
configure_file(host/resources/app.rc.in generated/app.rc @ONLY)

add_library(ttr_common STATIC
  common/crypto.cpp
  common/manifest.cpp
  common/pe_identity.cpp
  common/sha256.cpp)
target_include_directories(ttr_common PUBLIC include common "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ttr_common PUBLIC ttr_build_options bcrypt ole32)
if(TTR_ENABLE_ASAN)
  get_filename_component(TTR_COMPILER_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_file(TTR_ASAN_RUNTIME clang_rt.asan_dynamic-x86_64.dll
    HINTS "${TTR_COMPILER_DIRECTORY}" REQUIRED NO_DEFAULT_PATH)
  add_custom_command(TARGET ttr_common POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${TTR_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:ttr_common>"
    VERBATIM)
endif()

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
    TLS_VERIFY ON
    STATUS TTR_WEBVIEW2_DOWNLOAD_STATUS)
  list(GET TTR_WEBVIEW2_DOWNLOAD_STATUS 0 TTR_WEBVIEW2_DOWNLOAD_CODE)
  list(GET TTR_WEBVIEW2_DOWNLOAD_STATUS 1 TTR_WEBVIEW2_DOWNLOAD_MESSAGE)
  if(NOT TTR_WEBVIEW2_DOWNLOAD_CODE EQUAL 0)
    message(FATAL_ERROR
      "Unable to download pinned WebView2 metadata ${WEBVIEW2_VERSION}: "
      "${TTR_WEBVIEW2_DOWNLOAD_MESSAGE}. For an offline build, pre-populate ${WEBVIEW2_PACKAGE}.")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${WEBVIEW2_PACKAGE}" DESTINATION "${WEBVIEW2_ROOT}")
endif()
add_custom_command(OUTPUT "${MUX_GENERATED_STAMP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${MUX_GENERATED_DIR}"
  COMMAND "${CPPWINRT_EXE}" -input "${CMAKE_CURRENT_SOURCE_DIR}/third_party/microsoft-ui-xaml/Microsoft.UI.Xaml.winmd" -input "${WEBVIEW2_WINMD}" -reference "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" -output "${MUX_GENERATED_DIR}" -overwrite
  COMMAND "${CMAKE_COMMAND}" -E touch "${MUX_GENERATED_STAMP}"
  DEPENDS third_party/microsoft-ui-xaml/Microsoft.UI.Xaml.winmd "${WEBVIEW2_WINMD}"
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
  host/autostart.cpp host/settings.cpp host/diagnostics.cpp host/offline_diagnostics.cpp host/cleanup.cpp
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
target_link_libraries(manifestsign PRIVATE ttr_common)

add_executable(compatgen tools/compatgen/main.cpp)
target_include_directories(compatgen PRIVATE third_party/nlohmann)
target_include_directories(compatgen PRIVATE "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/include")
target_link_directories(compatgen PRIVATE "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/lib/amd64")
target_link_libraries(compatgen PRIVATE ttr_common diaguids)
file(TO_CMAKE_PATH "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/bin/amd64/msdia140.dll" TTR_DIA_DLL_PATH)
target_compile_definitions(compatgen PRIVATE TTR_DIA_DLL_PATH_W=L"${TTR_DIA_DLL_PATH}")

add_executable(resourcecheck tools/resourcecheck/main.cpp)
target_link_libraries(resourcecheck PRIVATE ttr_common version)

if(BUILD_TESTING)
  add_executable(ttr_unit_tests tests/unit/main.cpp)
  target_link_libraries(ttr_unit_tests PRIVATE ttr_common)
  add_test(NAME ttr_unit_tests COMMAND ttr_unit_tests)
  add_executable(ttr_pe_tests tests/pe/main.cpp)
  target_link_libraries(ttr_pe_tests PRIVATE ttr_common)
  add_test(NAME pe_parser_regressions COMMAND ttr_pe_tests)
  add_executable(ttr_manifest_tests tests/manifest/main.cpp)
  target_link_libraries(ttr_manifest_tests PRIVATE ttr_common)
  add_test(NAME manifest_parser_regressions COMMAND ttr_manifest_tests)
  add_executable(ttr_crypto_tests tests/crypto/main.cpp)
  target_link_libraries(ttr_crypto_tests PRIVATE ttr_common)
  add_test(NAME crypto_signing_regressions COMMAND ttr_crypto_tests)
  add_library(ttr_compat_fixture SHARED tests/tools/compat_fixture.cpp)
  target_link_libraries(ttr_compat_fixture PRIVATE ttr_build_options)
  # The compatibility fixture intentionally carries an RSDS identity in every
  # configuration so compatgen can prove exact DLL/PDB matching in Release tests.
  target_link_options(ttr_compat_fixture PRIVATE /DEBUG:FULL)
  set_target_properties(ttr_compat_fixture PROPERTIES PREFIX "")
  add_test(NAME tool_end_to_end_regressions
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/run_tool_tests.ps1"
      -Bin "$<TARGET_FILE_DIR:moduleid>"
      -Source "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools"
      -Scratch "$<TARGET_FILE_DIR:moduleid>/tool-test-scratch")
  add_test(NAME offline_diagnostic_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/run_diagnostic_test.ps1"
      -HostExe "$<TARGET_FILE:TaskbarThumbnailReorder>"
      -DevelopmentKey ${TTR_DEVELOPMENT_KEY}
      -Scratch "$<TARGET_FILE_DIR:TaskbarThumbnailReorder>/diagnostic-test-scratch")
endif()
