cmake_minimum_required(VERSION 3.20)
project(compacs_desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(SOURCES main.cpp config.cpp)

# Optional: embed vectors.bin at compile time (place vectors.bin next to CMakeLists.txt).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vectors.bin")
  set(VECTORS_EMBED "${CMAKE_CURRENT_BINARY_DIR}/vectors_embedded.cpp")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vectors.bin" _vectors_hex HEX)
  string(LENGTH "${_vectors_hex}" _hex_len)
  math(EXPR _byte_count "${_hex_len} / 2")
  set(_embed_src "namespace compacs_embed {\n")
  string(APPEND _embed_src "alignas(8) const unsigned char kVectorsData[] = {\n")
  math(EXPR _last_idx "${_byte_count} - 1")
  foreach(i RANGE 0 ${_last_idx})
    math(EXPR off "${i} * 2")
    string(SUBSTRING "${_vectors_hex}" ${off} 2 _byte)
    string(APPEND _embed_src "0x${_byte}")
    if(i LESS _last_idx)
      string(APPEND _embed_src ",")
    endif()
    math(EXPR mod "${i} % 16")
    if(mod EQUAL 15)
      string(APPEND _embed_src "\n")
    else()
      string(APPEND _embed_src " ")
    endif()
  endforeach()
  string(APPEND _embed_src "\n};\n")
  string(APPEND _embed_src "const std::size_t kVectorsSize = ${_byte_count};\n")
  string(APPEND _embed_src "}  // namespace compacs_embed\n")
  file(WRITE "${VECTORS_EMBED}" "${_embed_src}")
  list(APPEND SOURCES "${VECTORS_EMBED}")
  add_compile_definitions(COMPACS_EMBEDDED_VECTORS=1)
  message(STATUS "Embedding vectors.bin (${_byte_count} bytes) into main.exe")
else()
  message(STATUS "vectors.bin not found — exe will load vectors.bin from its directory at runtime")
endif()

add_executable(main ${SOURCES})

target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(export_vectors tools/export_vectors/main.cpp)
target_include_directories(export_vectors PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# WebView2 SDK (required by webview.h on Windows).
set(_webview2_search "")
if(DEFINED ENV{COMPACS_WEBVIEW2_DIR})
  list(APPEND _webview2_search "$ENV{COMPACS_WEBVIEW2_DIR}")
endif()
get_filename_component(_sibling_engine "${CMAKE_CURRENT_SOURCE_DIR}/../compacs-ai-engine-develop" ABSOLUTE)
list(APPEND _webview2_search
  "${_sibling_engine}/.tools/mswebview2"
  "${CMAKE_CURRENT_SOURCE_DIR}/.tools/mswebview2"
)
foreach(_root IN LISTS _webview2_search)
  if(NOT _root OR NOT EXISTS "${_root}")
    continue()
  endif()
  file(GLOB _wv2_pkgs RELATIVE "${_root}" "${_root}/Microsoft.Web.WebView2.*")
  foreach(_pkg IN LISTS _wv2_pkgs)
    set(_candidate "${_root}/${_pkg}/build/native/include")
    if(EXISTS "${_candidate}/WebView2.h")
      set(WEBVIEW2_INCLUDE_DIR "${_candidate}" CACHE PATH "WebView2 SDK include dir")
      break()
    endif()
  endforeach()
  if(WEBVIEW2_INCLUDE_DIR)
    break()
  endif()
endforeach()
if(NOT WEBVIEW2_INCLUDE_DIR)
  message(FATAL_ERROR
    "WebView2 SDK not found. Install NuGet package Microsoft.Web.WebView2 to "
    ".tools/mswebview2 or set COMPACS_WEBVIEW2_DIR.")
endif()
target_include_directories(main PRIVATE "${WEBVIEW2_INCLUDE_DIR}")

if(WIN32)
  target_compile_definitions(main PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(main PRIVATE
    ole32 shell32 shlwapi user32 version advapi32 ws2_32
  )
endif()

# Copy UI assets next to the executable.
add_custom_command(TARGET main POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:main>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/index.html"
    "$<TARGET_FILE_DIR:main>/assets/index.html"
)

install(TARGETS main RUNTIME DESTINATION bin)
install(FILES assets/index.html DESTINATION bin/assets)
