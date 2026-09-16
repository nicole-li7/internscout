cmake_minimum_required(VERSION 3.16)
project(InternScout VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # lets VS Code's IntelliSense see the include paths

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)         # parsing 12 MB of JSON is much faster optimised
endif()

# libcurl ships with macOS (and most Linux distros); we only need to link against it.
find_package(CURL REQUIRED)

add_executable(internscout
  src/main.cpp
  src/http.cpp
  src/matcher.cpp
  src/profile.cpp
  src/sources.cpp
  src/store.cpp
  src/text.cpp
  src/ui.cpp
)

target_include_directories(internscout PRIVATE src third_party)
target_link_libraries(internscout PRIVATE CURL::libcurl)
target_compile_options(internscout PRIVATE -Wall -Wextra)

# `cmake --install build` copies the binary onto your PATH.
if(APPLE AND EXISTS /opt/homebrew/bin)
  set(CMAKE_INSTALL_PREFIX /opt/homebrew CACHE PATH "install prefix" FORCE)
endif()
install(TARGETS internscout RUNTIME DESTINATION bin)
