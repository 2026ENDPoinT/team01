cmake_minimum_required(VERSION 3.16)
project(GradeManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(GradeManager ${SOURCES})
target_include_directories(GradeManager PRIVATE include third_party)

# 실행 위치(build/, cmake-build-debug/ 등)와 상관없이 data/, web/ 를 찾도록 프로젝트 경로를 넘겨줌
target_compile_definitions(GradeManager PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# cpp-httplib 은 스레드를 사용함 (Linux: pthread)
find_package(Threads REQUIRED)
target_link_libraries(GradeManager PRIVATE Threads::Threads)

# Windows 소켓 라이브러리 (cpp-httplib 은 Windows 10 이상 API 사용 → MinGW 에서 버전 지정 필요)
if(WIN32)
    target_compile_definitions(GradeManager PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(GradeManager PRIVATE ws2_32)
endif()

# FileIO 테스트 (GradeManager 와 별개 실행파일, 서버 없이 FileIO 만 실행)
#   CLion 실행 대상에서 FileIOTest 선택 → Run / Debug,  터미널에서는 ctest
add_executable(FileIOTest tests/file_io_test.cpp src/file_io.cpp)
target_include_directories(FileIOTest PRIVATE include)
target_compile_definitions(FileIOTest PRIVATE TEST_DATA_DIR="${CMAKE_BINARY_DIR}/test_data")

enable_testing()
add_test(NAME FileIOTest COMMAND FileIOTest)
