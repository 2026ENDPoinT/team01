cmake_minimum_required(VERSION 3.16)
project(GradeManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# cpp-httplib 은 스레드를 사용함 (Linux: pthread)
find_package(Threads REQUIRED)

# main.cpp 를 뺀 나머지 소스 → 실행 파일과 테스트가 같이 쓰는 라이브러리
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX "/main\\.cpp$")
add_library(grade_core STATIC ${CORE_SOURCES})
target_include_directories(grade_core PUBLIC include third_party)
target_link_libraries(grade_core PUBLIC Threads::Threads)

# Windows 소켓 라이브러리 (cpp-httplib 은 Windows 10 이상 API 사용 → MinGW 에서 버전 지정 필요)
#   PUBLIC 이라 grade_core 를 쓰는 GradeManager, GradeTests 에도 같이 적용됨
if(WIN32)
    target_compile_definitions(grade_core PUBLIC _WIN32_WINNT=0x0A00)
    target_link_libraries(grade_core PUBLIC ws2_32)
endif()

add_executable(GradeManager src/main.cpp)
target_link_libraries(GradeManager PRIVATE grade_core)

# 실행 위치(build/, cmake-build-debug/ 등)와 상관없이 data/, web/ 를 찾도록 프로젝트 경로를 넘겨줌
target_compile_definitions(GradeManager PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# 테스트: ctest --test-dir build --output-on-failure
#   외부 프레임워크 없이 tests/check.h 의 CHECK 매크로 사용, 저장/불러오기는 임시 폴더에서만 함
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(GradeTests ${TEST_SOURCES})
target_link_libraries(GradeTests PRIVATE grade_core)
add_test(NAME GradeTests COMMAND GradeTests)
