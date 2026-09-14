# Android 构建入口（M4）——引擎编译为 libmain.so，SDL2 由源码一并编译。
#
# 与 PC 端共用同一套 engine/src（符合"一套代码，两端编译"的目标），
# 差异只在编译宏与资源路径。
#
# 说明：
#   · SDL2 源码来自 third_party/SDL2-src（与 PC 端同一份，Android 侧走 CMake 分支）
#   · font.c 在未定义 SANGO3_HAVE_TTF 时是空实现 —— 首版 APK 不带字体（SDL2_ttf 的
#     Android 版需另编 FreeType，留作后续），故这里不定义该宏，文本层自动跳过。
#   · SANGO3_ANDROID 让 sango3app.c 改用 Android 固定资源路径（/sdcard/Sango3）。
cmake_minimum_required(VERSION 3.20)
project(sango3android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# CMakeLists 位于 <root>/android/app/jni → 上溯三级即 <root>
set(SDL2_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/SDL2-src")
set(ENGINE   "${CMAKE_CURRENT_SOURCE_DIR}/../../../engine")

if(EXISTS "${SDL2_SRC}/CMakeLists.txt")
  add_subdirectory(${SDL2_SRC} ${CMAKE_BINARY_DIR}/sdl2)
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

target_include_directories(main PRIVATE ${ENGINE}/src)
target_compile_definitions(main PRIVATE SANGO3_ANDROID=1)
# log：__android_log_print 在 liblog 中（Android NDK 提供）
target_link_libraries(main SDL2::SDL2 SDL2::SDL2main log)
