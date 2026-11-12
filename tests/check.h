// 담당: 조장 (Eno74)
// 외부 라이브러리 없이 쓰는 작은 테스트 도구
//   TEST_CASE(이름) { ... }   테스트 함수 등록 (test_main.cpp 의 main 이 차례로 실행)
//   CHECK(조건)               거짓이면 파일:줄 과 조건식을 출력하고 실패로 기록 (테스트는 계속 진행)
//   CHECK_EQ(실제, 기대)      == 로 비교, 다르면 두 값을 같이 출력
//   CHECK_NEAR(실제, 기대, 오차)  실수 비교
#pragma once

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct TestCase {
    const char* name;
    void (*fn)();
};

// 등록된 테스트 목록 (전역 초기화 순서와 상관없도록 함수 안의 static)
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

// 지금까지 실패한 CHECK 개수
inline int& failures() {
    static int count = 0;
    return count;
}

inline void reportFailure(const char* file, int line, const std::string& expr, const std::string& detail) {
    failures()++;
    std::cerr << file << ":" << line << ": 실패: " << expr;
    if (!detail.empty()) {
        std::cerr << " (" << detail << ")";
    }
    std::cerr << std::endl;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

}  // namespace check

#define TEST_CASE(name)                                                \
    static void name();                                                \
    static const check::Registrar name##_registrar(#name, &name);      \
    static void name()

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            check::reportFailure(__FILE__, __LINE__, #cond, "");       \
        }                                                              \
    } while (0)

// 문자열 리터럴끼리 비교하면 포인터 비교가 되므로 한쪽은 std::string 이어야 함
#define CHECK_EQ(actual, expected)                                                         \
    do {                                                                                   \
        const auto& checkActual_ = (actual);                                               \
        const auto& checkExpected_ = (expected);                                           \
        if (!(checkActual_ == checkExpected_)) {                                           \
            std::ostringstream checkOut_;                                                  \
            checkOut_ << "실제: " << checkActual_ << ", 기대: " << checkExpected_;         \
            check::reportFailure(__FILE__, __LINE__, #actual " == " #expected,             \
                                 checkOut_.str());                                         \
        }                                                                                  \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                            \
    do {                                                                                   \
        const double checkActual_ = (actual);                                              \
        const double checkExpected_ = (expected);                                          \
        if (!(std::fabs(checkActual_ - checkExpected_) <= (tolerance))) {                  \
            std::ostringstream checkOut_;                                                  \
            checkOut_ << "실제: " << checkActual_ << ", 기대: " << checkExpected_;         \
            check::reportFailure(__FILE__, __LINE__, #actual " ~= " #expected,             \
                                 checkOut_.str());                                         \
        }                                                                                  \
    } while (0)
