cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)
project("httpserver"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
# 使用libc++库
# add_compile_options("-fstandalone-debug")
find_package(fmt REQUIRED)

add_executable(main)
target_sources(main
  PRIVATE
  main.cpp
  ConnectionManager.cpp
)

# 链接
target_link_libraries(main
  PRIVATE
  fmt::fmt
)

