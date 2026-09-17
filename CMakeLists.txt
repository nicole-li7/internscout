cmake_minimum_required(VERSION 3.16)
project(InternScout VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # lets VS Code's IntelliSense see the include paths

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)         # parsing 12 MB of JSON is much faster optimised
endif()

# ---- shared core: fetching, matching, storage (no UI) -----------------------
find_package(CURL REQUIRED)             # ships with macOS

add_library(internscout_core STATIC
  src/eligibility.cpp
  src/http.cpp
  src/matcher.cpp
  src/options.cpp
  src/profile.cpp
  src/sources.cpp
  src/store.cpp
  src/text.cpp
  src/ui.cpp
)
target_include_directories(internscout_core PUBLIC src third_party)
target_link_libraries(internscout_core PUBLIC CURL::libcurl)
target_compile_options(internscout_core PRIVATE -Wall -Wextra)

# ---- command-line tool -------------------------------------------------------
add_executable(internscout src/main.cpp)
target_link_libraries(internscout PRIVATE internscout_core)
target_compile_options(internscout PRIVATE -Wall -Wextra)

# ---- windowed app (SDL2 + OpenGL + Dear ImGui) --------------------------------
find_package(SDL2 REQUIRED)             # brew install sdl2
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/imgui_impl_sdl2.cpp
  third_party/imgui/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)
target_compile_definitions(imgui PUBLIC GL_SILENCE_DEPRECATION)   # Apple deprecated OpenGL but it works fine

set(APP_NAME "InternScout")
add_executable(InternScoutApp MACOSX_BUNDLE
  src/gui/main_gui.cpp
  src/gui/app.cpp
)
target_link_libraries(InternScoutApp PRIVATE internscout_core imgui)
target_compile_options(InternScoutApp PRIVATE -Wall -Wextra)

if(APPLE)
  set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icns")
  set_target_properties(InternScoutApp PROPERTIES
    OUTPUT_NAME                        "${APP_NAME}"
    MACOSX_BUNDLE_INFO_PLIST           "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME          "${APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER       "com.nicole-li7.internscout"
    MACOSX_BUNDLE_ICON_FILE            "AppIcon"
    MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    INSTALL_RPATH                      "@executable_path/../Frameworks"
    BUILD_WITH_INSTALL_RPATH           TRUE
  )
  # Copy the icon in, then make the bundle self-contained (copies SDL into it
  # and re-signs). Icon first: signing must be the last thing that touches the app.
  add_custom_command(TARGET InternScoutApp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:InternScoutApp>/Resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${APP_ICON}" "$<TARGET_BUNDLE_CONTENT_DIR:InternScoutApp>/Resources/AppIcon.icns"
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/bundle_libs.sh" "$<TARGET_BUNDLE_DIR:InternScoutApp>" "${APP_NAME}"
    VERBATIM)
  # `cmake --install build` puts the app in /Applications and the CLI on your PATH.
  if(EXISTS /opt/homebrew/bin)
    set(CMAKE_INSTALL_PREFIX /opt/homebrew CACHE PATH "install prefix" FORCE)
  endif()
  install(TARGETS InternScoutApp BUNDLE DESTINATION "/Applications")
endif()
install(TARGETS internscout RUNTIME DESTINATION bin)
