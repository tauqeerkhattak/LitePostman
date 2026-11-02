cmake_minimum_required(VERSION 3.20)
project(LitePostman VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
  add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
  add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
  endif()
endif()

set(CORE_SOURCES
  src/core/json.cpp
  src/core/url.cpp
  src/core/variables.cpp
  src/core/models.cpp
  src/core/http_client.cpp
  src/core/curl_parser.cpp
  src/core/curl_generator.cpp
  src/core/storage.cpp
)

add_library(litepostman_core STATIC ${CORE_SOURCES})
target_include_directories(litepostman_core PUBLIC src)
target_link_libraries(litepostman_core PUBLIC winhttp shell32 ole32 uuid)

set(UI_SOURCES
  src/ui/util.cpp
  src/ui/theme.cpp
  src/ui/controls.cpp
  src/ui/rich_edit.cpp
  src/ui/key_value_table.cpp
  src/ui/sidebar.cpp
  src/ui/request_view.cpp
  src/ui/response_view.cpp
  src/ui/env_dialog.cpp
  src/ui/curl_dialogs.cpp
  src/ui/main_window.cpp
)

add_executable(LitePostman WIN32 src/main.cpp ${UI_SOURCES})
if(MSVC)
  target_sources(LitePostman PRIVATE src/app.rc)
endif()
target_include_directories(LitePostman PRIVATE src)
target_link_libraries(LitePostman PRIVATE litepostman_core comctl32 comdlg32 riched20 dwmapi uxtheme shlwapi winhttp)
set_target_properties(LitePostman PROPERTIES OUTPUT_NAME "LitePostman")
if(MINGW)
  # MinGW always injects its own default-manifest.o. Build our manifest as a
  # replacement with the same name in the build tree and point -B at it so the
  # toolchain one is shadowed (avoids ld's duplicate-manifest error).
  set(MANIFEST_OBJECT "${CMAKE_BINARY_DIR}/default-manifest.o")
  add_custom_command(
    OUTPUT "${MANIFEST_OBJECT}"
    COMMAND "${CMAKE_RC_COMPILER}" -i "${CMAKE_SOURCE_DIR}/src/app.rc" -o "${MANIFEST_OBJECT}" --output-format=coff
    DEPENDS "${CMAKE_SOURCE_DIR}/src/app.rc" "${CMAKE_SOURCE_DIR}/src/app.manifest"
    VERBATIM)
  add_custom_target(LitePostmanManifest DEPENDS "${MANIFEST_OBJECT}")
  add_dependencies(LitePostman LitePostmanManifest)
  target_compile_options(LitePostman PRIVATE -municode)
  target_link_options(LitePostman PRIVATE -municode -static -static-libgcc -static-libstdc++)
  target_link_options(LitePostman PRIVATE "-B${CMAKE_BINARY_DIR}/")
endif()

add_executable(core_tests tests/test_main.cpp)
target_link_libraries(core_tests PRIVATE litepostman_core)
set_target_properties(core_tests PROPERTIES OUTPUT_NAME "core_tests")

if(MSVC)
  target_compile_options(LitePostman PRIVATE /W4 /WX)
  target_compile_options(core_tests PRIVATE /W4 /WX)
else()
  target_compile_options(LitePostman PRIVATE -Wall -Wextra -Werror)
  target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
