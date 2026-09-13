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
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------- 数据层（共享）
add_library(sango3_data STATIC
  src/pak.c
  src/shp.c)

target_include_directories(sango3_data PUBLIC src)

# ------------------------------------------------------- 渲染层（分辨率无关）
# 逻辑坐标 640×480 ↔ 物理分辨率（1080p/2K/4K）的变换、滤镜、素材分级选档。
# 纯计算，不依赖 SDL / OpenGL，便于离线出图做像素级校验。
add_library(sango3_render STATIC src/render.c)
target_include_directories(sango3_render PUBLIC src)

# ---------------------------------------------------------------- 工具与可执行
# PAK 容器检查器
add_executable(sango3pak src/pak_lister.c)
target_link_libraries(sango3pak PRIVATE sango3_data)

# SHP 精灵解码验证器
add_executable(sango3shp src/shp_probe.c)
target_link_libraries(sango3shp PRIVATE sango3_data)

# 渲染验证器（dump 出图 / show 开窗）
add_executable(sango3view src/sango3view.c)
target_link_libraries(sango3view PRIVATE sango3_data)

# 分辨率无关渲染演示（同一逻辑场景 → 1080p / 2K / 4K，含滤镜对比）
add_executable(sango3scene src/sango3scene.c)
target_link_libraries(sango3scene PRIVATE sango3_data sango3_render)

set_target_properties(sango3pak sango3shp sango3view sango3scene PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------- SDL2（可选）
# third_party/SDL2 由 tools/setup_sdl.py 从官方包部署（不入库）。
# 存在即启用 show 模式与后续窗口程序；缺失时 dump 模式仍可正常工作。
set(SANGO3_SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL2")
if(EXISTS "${SANGO3_SDL2_DIR}/include/SDL.h")
  message(STATUS "SDL2 found: ${SANGO3_SDL2_DIR}")
  add_library(sango3_sdl INTERFACE)
  target_include_directories(sango3_sdl INTERFACE "${SANGO3_SDL2_DIR}/include")
  # 只链接 SDL2.lib（不链接 SDL2main.lib）：SDL_MAIN_HANDLED 让 main 保持真 main，
  # 我们的程序还有一个不依赖视频子系统的 dump 模式，不需要 WinMain 包装。
  target_link_libraries(sango3_sdl INTERFACE "${SANGO3_SDL2_DIR}/lib/x64/SDL2.lib")
  target_compile_definitions(sango3_sdl INTERFACE SANGO3_HAVE_SDL=1 SDL_MAIN_HANDLED=1)
  set(SANGO3_HAVE_SDL TRUE)
else()
  message(STATUS "SDL2 not found at ${SANGO3_SDL2_DIR} — building without SDL (dump mode only)")
  set(SANGO3_HAVE_SDL FALSE)
endif()

if(SANGO3_HAVE_SDL)
  target_sources(sango3view PRIVATE src/sango3view_sdl.c)
  target_link_libraries(sango3view PRIVATE sango3_sdl)
  # 把 SDL2.dll 复制到产物目录，保证可执行文件能直接运行
  add_custom_command(TARGET sango3view POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SANGO3_SDL2_DIR}/lib/x64/SDL2.dll"
            "$<TARGET_FILE_DIR:sango3view>/SDL2.dll")
endif()

message(STATUS "Sango3Native: C=${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}  build=${CMAKE_BUILD_TYPE}  sdl=${SANGO3_HAVE_SDL}")
