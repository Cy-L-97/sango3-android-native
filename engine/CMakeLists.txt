cmake_minimum_required(VERSION 3.20)
project(Sango3Native LANGUAGES C CXX)

# 三国群英传3 原生引擎 —— 同一套代码编译 Windows 调试版与 Android 原生版
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  # /utf-8: 源码含中文，必须让 MSVC 按 UTF-8 解析
  # /W4  : 高警告等级
  add_compile_options(/utf-8 /W4 /permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()

# ---- 工具：PAK 资源包检查器（数据层第 1 块砖） ----
add_executable(sango3pak src/pak_lister.c)

set_target_properties(sango3pak PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

message(STATUS "Sango3Native: C=${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}  build=${CMAKE_BUILD_TYPE}")
