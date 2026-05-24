cmake_minimum_required(VERSION 3.28...3.30)

add_library(plugin-srccpy MODULE)
add_library(OBS::srccpy ALIAS plugin-srccpy)
find_package(Qt6 REQUIRED Widgets)
set(CORE_SOURCES
    src/device.cpp
    src/controller.cpp
    src/srccpy.cpp
    src/control_msg.cpp
    src/server/server.cpp
    src/server/sc_server_cmd_builder.cpp
    src/adb/adb.cpp
    src/adb/adb_tunnel.cpp
    src/adb/adb_device.cpp
    src/adb/adb_parser.cpp
    src/util/rand.cpp
    src/util/sc_intr.cpp
    src/util/tick.cpp
    src/util/str_util.cpp
    src/util/sc_file.cpp
    src/util/sc_log.cpp
    src/util/options.cpp
    src/util/sc_thread.cpp
    src/util/sc_process.cpp
    src/util/process_intr.cpp
    src/util/common.cpp
    src/util/net.cpp
    src/util/env.cpp
    src/event/sc_event.cpp
    src/codec/demuxer.cpp
    src/codec/packet_source.cpp
    src/codec/packet_sink.cpp
    src/codec/packet_merge.cpp
    src/srccpy-plugin-main.cpp
)

# 头文件列表
set(CORE_HEADERS
    src/device.h
    src/controller.h
    src/compat.h
    src/srccpy.hpp
    src/control_msg.h
    src/coords.h
    src/server/server.hpp
    src/server/sc_server_cmd_builder.hpp
    src/adb/adb.h
    src/adb/adb_tunnel.h
    src/adb/adb_device.h
    src/adb/adb_parser.h
    src/util/str_util.h
    src/util/rand.h
    src/util/sc_intr.h
    src/util/tick.h
    src/util/sc_file.h
    src/util/sc_log.h
    src/util/env.h
    src/util/common.h
    src/util/net.h
    src/util/options.h
    src/util/sc_thread.h
    src/util/sc_process.h
    src/util/process_intr.h
    src/event/sc_event.h
    src/codec/demuxer.h
    src/codec/packet_source.h
    src/codec/packet_sink.h
    src/codec/packet_merge.h
)

target_sources(
  plugin-srccpy
  PRIVATE ${CORE_SOURCES} ${CORE_HEADERS}
)

# 源文件分组
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${CORE_SOURCES})

# 头文件分组
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${CORE_HEADERS})
configure_file(cmake/windows/obs-module.rc.in win-wasapi.rc)
target_sources(plugin-srccpy PRIVATE win-wasapi.rc)
target_include_directories(plugin-srccpy PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util
)

target_link_libraries(plugin-srccpy PRIVATE
  OBS::libobs
  FFmpeg::avcodec
  FFmpeg::avfilter
  FFmpeg::avformat
  FFmpeg::avutil
  FFmpeg::swscale
  FFmpeg::swresample
  OBS::frontend-api
  Qt::Widgets
)

# Windows-specific libraries
if(WIN32)
  target_link_libraries(plugin-srccpy PRIVATE
    ws2_32
    iphlpapi
    winmm
  )
endif()

set_target_properties_obs(plugin-srccpy PROPERTIES FOLDER plugins PREFIX "")
