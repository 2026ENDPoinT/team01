cmake_minimum_required(VERSION 3.16)
project(GradeManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(GradeManager ${SOURCES})
target_include_directories(GradeManager PRIVATE include)