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

# ------------------------------------------------------------ 规则数据层（共享）
# INI 解析（Big5 → UTF-8）、编码表、随机源、武将/物品/兵种/技表 + 需求③④的运行时规则。
# 不依赖 SDL / OpenGL，可离线跑回归（sango3data）。
add_library(sango3_game STATIC
  src/text.c
  src/ini.c
  src/ui.c
  src/rng.c
  src/gamedata.c)
target_include_directories(sango3_game PUBLIC src)

# ------------------------------------------------------------ 场景层（共享）
# 控件树运行时 + 场景渲染（M2-5）：遍历 Menu.ini 控件树，按 Range 逐控件贴素材。
# 依赖 ui（布局）+ data（SHP 解码）+ render（画布），供 sango3menu 与后续游戏主程序复用。
add_library(sango3_scene STATIC src/menu_scene.c)
target_link_libraries(sango3_scene PUBLIC sango3_game sango3_data sango3_render)

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

# 数据层 / 必杀技规则验证器（C 结果导出，供 Python 基准比对）
add_executable(sango3data src/gamedata_probe.c)
target_link_libraries(sango3data PRIVATE sango3_game)

# UI 布局解析验证器（M2：Menu.ini → 控件/图标/配色，含 #include 展开）
add_executable(sango3ui src/ui_probe.c)
target_link_libraries(sango3ui PRIVATE sango3_game sango3_data)

# 主菜单场景渲染验证器（M2-5：控件树 → 640×480 逻辑画布 → raw/PNG）
add_executable(sango3menu src/menu_probe.c)
target_link_libraries(sango3menu PRIVATE sango3_scene)

set_target_properties(sango3pak sango3shp sango3view sango3scene sango3data sango3ui sango3menu PROPERTIES
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
  # GPU 呈现器（M1-b 路径 A）：逻辑画布作为静态纹理上传，开物理分辨率窗口，
  # 由 GPU 按宽高比/滤镜缩放显示，并支持拖动缩放。依赖 SDL + 分辨率无关渲染层。
  # font.c 为 TTF 字体渲染层（M1-b 字体显示），随下面 SDL2_ttf 检测结果编译真实/降级实现。
  add_library(sango3_present STATIC src/presenter.c src/font.c)
  target_link_libraries(sango3_present PUBLIC sango3_sdl sango3_render)

  # SDL2_ttf（字体渲染，独立扩展库）：存在即启用字体显示层（含像素/高清双模式 + 回退链）。
  set(SANGO3_SDL2_TTF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL2_ttf")
  if(EXISTS "${SANGO3_SDL2_TTF_DIR}/include/SDL_ttf.h")
    message(STATUS "SDL2_ttf found: ${SANGO3_SDL2_TTF_DIR}")
    target_include_directories(sango3_present PUBLIC "${SANGO3_SDL2_TTF_DIR}/include")
    target_link_libraries(sango3_present PUBLIC "${SANGO3_SDL2_TTF_DIR}/lib/x64/SDL2_ttf.lib")
    target_compile_definitions(sango3_present PUBLIC SANGO3_HAVE_TTF=1)
    set(SANGO3_HAVE_TTF TRUE)
    # 复制 SDL2_ttf.dll 到产物目录
    add_custom_command(TARGET sango3view POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${SANGO3_SDL2_TTF_DIR}/lib/x64/SDL2_ttf.dll"
              "$<TARGET_FILE_DIR:sango3view>/SDL2_ttf.dll")
  else()
    message(STATUS "SDL2_ttf not found at ${SANGO3_SDL2_TTF_DIR} — building without font rendering")
    set(SANGO3_HAVE_TTF FALSE)
  endif()

  target_sources(sango3view PRIVATE src/sango3view_sdl.c)
  target_link_libraries(sango3view PRIVATE sango3_sdl sango3_present)
  # 主菜单渲染验证器接入字体层（有 SDL_ttf 时启用文本绘制，否则自动跳过）
  target_link_libraries(sango3menu PRIVATE sango3_present)

  # 游戏主程序雏形（M2-6 交互 + M2-7 场景切换）：需要 SDL 窗口与字体层
  add_executable(sango3app src/sango3app.c src/editor_scene.c src/kingdom_scene.c src/strategy_scene.c src/admin_menu.c src/roster.c src/gen_picker.c src/lord_picker.c src/officer_ui.c)
  target_link_libraries(sango3app PRIVATE sango3_scene sango3_present)
  set_target_properties(sango3app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  # 把 SDL2.dll 复制到产物目录，保证可执行文件能直接运行
  add_custom_command(TARGET sango3view POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SANGO3_SDL2_DIR}/lib/x64/SDL2.dll"
            "$<TARGET_FILE_DIR:sango3view>/SDL2.dll")
endif()

message(STATUS "Sango3Native: C=${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}  build=${CMAKE_BUILD_TYPE}  sdl=${SANGO3_HAVE_SDL}")
