cmake_minimum_required(VERSION 3.15)
project(gta6menu CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")   # static CRT: no runtime install needed

add_library(gta6menu MODULE src/main.cpp)
target_link_libraries(gta6menu PRIVATE user32 gdi32)
set_target_properties(gta6menu PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "gta6menu")
if(MSVC)
  target_compile_options(gta6menu PRIVATE /utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS)
endif()
