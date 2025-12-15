cmake_minimum_required(VERSION 3.15)
project(OpusAll C)

# 全局编译选项
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

## 默认使用 Release 构建（如果未指定）
#if(NOT CMAKE_BUILD_TYPE)
#    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
#endif()
#
## 优化选项
#if(MSVC)
#    # MSVC 优化
#    add_compile_options(/O2 /Oi /Ot /GL)
#    add_link_options(/LTCG)
#    # 启用 SSE2（现代 CPU 都支持）
#    add_compile_options(/arch:SSE2)
#else()
#    # GCC/Clang 优化
#    add_compile_options(-O3 -ffast-math -march=native)
#endif()

# 启用 QEXT 支持
add_definitions(-DENABLE_QEXT)

# QEXT 自适应码率分配 (48kHz时减少QEXT比特分配，因为只有2个扩展频段)
option(QEXT_ADAPTIVE_BITRATE "Adaptive QEXT bitrate allocation based on sample rate" ON)
if(QEXT_ADAPTIVE_BITRATE)
    add_definitions(-DQEXT_ADAPTIVE_BITRATE)
    message(STATUS "QEXT Adaptive Bitrate: ENABLED (48kHz will use less QEXT bits)")
endif()

# 平台特定设置
if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DWIN32)
endif()

# ============ 1. libogg ============
# 生成 ogg/config_types.h（必须在 add_library 之前）
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ogg)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ogg/config_types.h
"#ifndef _OGG_CONFIG_TYPES_H
#define _OGG_CONFIG_TYPES_H
#include <stdint.h>
typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
typedef uint64_t ogg_uint64_t;
#endif
")

set(OGG_SOURCES
    libogg-1.3.6/src/bitwise.c
    libogg-1.3.6/src/framing.c
)
add_library(ogg STATIC ${OGG_SOURCES})
target_include_directories(ogg PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libogg-1.3.6/include
)

# ============ 2. opus（启用 QEXT） ============
# 检查 opus-main 是否有 CMakeLists.txt
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/opus-main/CMakeLists.txt)
    add_subdirectory(opus-main)
else()
    message(FATAL_ERROR "opus-main/CMakeLists.txt not found. Please ensure opus source is complete.")
endif()

# ============ 3. opusfile ============
set(OPUSFILE_SOURCES
    opusfile-0.12/src/opusfile.c
    opusfile-0.12/src/info.c
    opusfile-0.12/src/internal.c
    opusfile-0.12/src/stream.c
)
add_library(opusfile STATIC ${OPUSFILE_SOURCES})
target_include_directories(opusfile PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/opusfile-0.12/include
)
target_link_libraries(opusfile PUBLIC ogg opus)
target_compile_definitions(opusfile PRIVATE OP_DISABLE_HTTP)

# ============ 4. libopusenc ============
set(OPUSENC_SOURCES
    libopusenc-0.2/src/ogg_packer.c
    libopusenc-0.2/src/opus_header.c
    libopusenc-0.2/src/opusenc.c
    libopusenc-0.2/src/picture.c
    libopusenc-0.2/src/resample.c
    libopusenc-0.2/src/unicode_support.c
)
add_library(opusenc STATIC ${OPUSENC_SOURCES})
target_include_directories(opusenc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libopusenc-0.2/include
)
target_include_directories(opusenc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libopusenc-0.2/src
    ${CMAKE_CURRENT_SOURCE_DIR}/win32_compat
)
target_compile_definitions(opusenc PRIVATE
    OUTSIDE_SPEEX
    RANDOM_PREFIX=opusenc
    PACKAGE_NAME="libopusenc"
    PACKAGE_VERSION="0.2"
)
# Force include opus_check.h for __opus_check_* macros
if(MSVC)
    target_compile_options(opusenc PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/win32_compat/opus_check.h)
else()
    target_compile_options(opusenc PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/win32_compat/opus_check.h)
endif()
target_link_libraries(opusenc PUBLIC ogg opus)
if(UNIX)
    target_link_libraries(opusenc PUBLIC m)
endif()

# ============ 5. opus-tools（opusenc/opusdec 可执行文件） ============

# Windows compatibility
if(WIN32)
    set(WIN32_COMPAT_SOURCES
        win32_compat/getopt.c
        win32_compat/opus_compat.c
    )
endif()

# opusfile stub (needed on all platforms when HTTP is disabled)
set(OPUSFILE_STUB_SOURCES
    win32_compat/opusfile_stub.c
)

# opusenc 可执行文件
# Note: picture.c is NOT included here because libopusenc provides picture functions
# Including both would cause symbol conflicts
set(OPUSENC_EXE_SOURCES
    opus-tools-0.2/src/opusenc.c
    opus-tools-0.2/src/audio-in.c
    opus-tools-0.2/src/diag_range.c
    opus-tools-0.2/src/flac.c
    opus-tools-0.2/src/opus_header.c
    ${WIN32_COMPAT_SOURCES}
)
if(WIN32)
    list(APPEND OPUSENC_EXE_SOURCES opus-tools-0.2/win32/unicode_support.c)
endif()

add_executable(opusenc_exe ${OPUSENC_EXE_SOURCES})
set_target_properties(opusenc_exe PROPERTIES OUTPUT_NAME opusenc)
target_include_directories(opusenc_exe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/opus-tools-0.2/src
    ${CMAKE_CURRENT_SOURCE_DIR}/opus-tools-0.2/win32
    ${CMAKE_CURRENT_SOURCE_DIR}/win32_compat
)
target_link_libraries(opusenc_exe PRIVATE opusenc opus ogg)
target_compile_definitions(opusenc_exe PRIVATE 
    HAVE_STDINT_H=1
    PACKAGE_VERSION="0.2-qext"
    PACKAGE_NAME="opus-tools"
    OUTSIDE_SPEEX
    RANDOM_PREFIX=opusenc_tool
)
# Force include opus_check.h for __opus_check_* macros
if(MSVC)
    target_compile_options(opusenc_exe PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/win32_compat/opus_check.h)
else()
    target_compile_options(opusenc_exe PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/win32_compat/opus_check.h)
endif()
if(UNIX)
    target_link_libraries(opusenc_exe PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(opusenc_exe PRIVATE ws2_32)
endif()

# opusdec 可执行文件
set(OPUSDEC_EXE_SOURCES
    opus-tools-0.2/src/opusdec.c
    opus-tools-0.2/src/wav_io.c
    opus-tools-0.2/src/wave_out.c
    opus-tools-0.2/src/diag_range.c
    opus-tools-0.2/src/resample.c
    opus-tools-0.2/src/opus_header.c
    ${WIN32_COMPAT_SOURCES}
    ${OPUSFILE_STUB_SOURCES}
)
if(WIN32)
    list(APPEND OPUSDEC_EXE_SOURCES opus-tools-0.2/win32/unicode_support.c)
endif()

add_executable(opusdec_exe ${OPUSDEC_EXE_SOURCES})
set_target_properties(opusdec_exe PROPERTIES OUTPUT_NAME opusdec)
target_include_directories(opusdec_exe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/opus-tools-0.2/src
    ${CMAKE_CURRENT_SOURCE_DIR}/opus-tools-0.2/win32
    ${CMAKE_CURRENT_SOURCE_DIR}/win32_compat
)
target_link_libraries(opusdec_exe PRIVATE opusfile opus ogg)
target_compile_definitions(opusdec_exe PRIVATE 
    HAVE_STDINT_H=1
    PACKAGE_VERSION="0.2-qext"
    PACKAGE_NAME="opus-tools"
    OUTSIDE_SPEEX
    RANDOM_PREFIX=opustools
    RESAMPLE_FULL_SINC_TABLE
    OP_DISABLE_HTTP
)
if(UNIX)
    target_link_libraries(opusdec_exe PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(opusdec_exe PRIVATE ws2_32 winmm)
endif()

# 安装规则
install(TARGETS opusenc_exe opusdec_exe
    RUNTIME DESTINATION bin
)

message(STATUS "==============================================")
message(STATUS "Opus All-in-One Build Configuration Complete")
message(STATUS "QEXT: ENABLED")
if(QEXT_ADAPTIVE_BITRATE)
    message(STATUS "QEXT Adaptive Bitrate: ON (48kHz=25%, 96kHz=80%)")
else()
    message(STATUS "QEXT Adaptive Bitrate: OFF (always 80%)")
endif()
message(STATUS "Targets: opusenc, opusdec")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "==============================================")

