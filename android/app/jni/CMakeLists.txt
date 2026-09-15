# Android 构建入口（M4）——引擎编译为 libmain.so，SDL2 由源码一并编译。
#
# 与 PC 端共用同一套 engine/src（符合"一套代码，两端编译"的目标），
# 差异只在编译宏与资源路径。
#
# 说明：
#   · SDL2 源码来自 third_party/SDL2-src（与 PC 端同一份，Android 侧走 CMake 分支）
#   · 字体走 **FreeType 直连**（third_party/freetype-*，add_subdirectory 一起编）。
#     不用 SDL2_ttf：其 CMake 会拉 harfbuzz / plutosvg，NDK 交叉编译链路长；
#     font.c 里两个后端（SANGO3_HAVE_TTF / SANGO3_HAVE_FREETYPE）对外接口一致。
#   · SANGO3_ANDROID 让 sango3app.c 改用 Android 固定资源路径（/sdcard/.../Sango3）。
cmake_minimum_required(VERSION 3.20)
project(sango3android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# CMakeLists 位于 <root>/android/app/jni → 上溯三级即 <root>
set(SDL2_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/SDL2-src")
# FreeType 源码目录带版本号（freetype-2.13.3），用 glob 找、取版本号最大的那个，
# 这样升级 freetype 不用改本文件。
file(GLOB _ft_dirs "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/freetype-*")
if(_ft_dirs)
  list(SORT _ft_dirs)
  list(GET _ft_dirs -1 FT_SRC)
endif()
set(ENGINE   "${CMAKE_CURRENT_SOURCE_DIR}/../../../engine")

if(EXISTS "${SDL2_SRC}/CMakeLists.txt")
  add_subdirectory(${SDL2_SRC} ${CMAKE_BINARY_DIR}/sdl2)
endif()

# FreeType：只留纯 C 部分，禁掉所有可选外部依赖（NDK 下找不到也不该找）
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
if(EXISTS "${FT_SRC}/CMakeLists.txt")
  add_subdirectory(${FT_SRC} ${CMAKE_BINARY_DIR}/freetype)
endif()

add_library(main SHARED
  ${ENGINE}/src/pak.c
  ${ENGINE}/src/shp.c
  ${ENGINE}/src/render.c
  ${ENGINE}/src/text.c
  ${ENGINE}/src/ini.c
  ${ENGINE}/src/ui.c
  ${ENGINE}/src/rng.c
  ${ENGINE}/src/gamedata.c
  ${ENGINE}/src/menu_scene.c
  ${ENGINE}/src/presenter.c
  ${ENGINE}/src/font.c
  ${ENGINE}/src/sango3app.c)

target_include_directories(main PRIVATE ${ENGINE}/src ${FT_SRC}/include)
target_compile_definitions(main PRIVATE SANGO3_ANDROID=1 SANGO3_HAVE_FREETYPE=1)
# log：__android_log_print 在 liblog 中（Android NDK 提供）
# freetype：FreeType 源码一并编译（目标名 freetype）
target_link_libraries(main SDL2::SDL2 SDL2::SDL2main freetype log)
