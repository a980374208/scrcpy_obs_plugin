cmake_minimum_required(VERSION 3.28...3.30)

add_library(plugin-srccpy MODULE)
add_library(OBS::srccpy ALIAS plugin-srccpy)
set(CORE_SOURCES
    
   
    device.cpp
    controller.cpp
    srccpy.cpp
    server/server.cpp
    server/sc_server_cmd_builder.cpp
    adb/adb.cpp
    adb/adb_tunnel.cpp
    adb/adb_device.cpp
    adb/adb_parser.cpp
    util/rand.cpp
    util/sc_intr.cpp
    util/tick.cpp
    util/str_util.cpp
    util/sc_file.cpp
    util/sc_log.cpp
    util/options.cpp
    util/sc_thread.cpp
    util/sc_process.cpp
    util/process_intr.cpp
    util/common.cpp
    util/net.cpp
    util/env.cpp
    event/sc_event.cpp
    codec/demuxer.cpp
    codec/packet_source.cpp
    codec/packet_sink.cpp
    codec/packet_merge.cpp
    srccpy-plugin-main.cpp
)

# 头文件列表
set(CORE_HEADERS

    device.h
    controller.h
    compat.h
    srccpy.hpp
    server/server.hpp
    server/sc_server_cmd_builder.hpp
    adb/adb.h
    adb/adb_tunnel.h
    adb/adb_device.h
    adb/adb_parser.h
    util/str_util.h
    util/rand.h
    util/sc_intr.h
    util/tick.h
    util/sc_file.h
    util/sc_log.h
    util/env.h
    util/common.h
    util/net.h
    util/options.h
    util/sc_thread.h
    util/sc_process.h
    util/process_intr.h
    event/sc_event.h
    codec/demuxer.h
    codec/packet_source.h
    codec/packet_sink.h
    codec/packet_merge.h
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

target_link_libraries(plugin-srccpy PRIVATE
  OBS::libobs
  FFmpeg::avcodec
  FFmpeg::avfilter
  FFmpeg::avformat
  FFmpeg::avutil
  FFmpeg::swscale
  FFmpeg::swresample
  OBS::frontend-api
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
