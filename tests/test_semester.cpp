// 담당: 조장 (Eno74)
// 학기 유틸 (semester.h): 형식 검사, 시간순 비교, 날짜 → 학기, 현재 학기
#include <ctime>
#include <string>

#include "check.h"
#include "semester.h"

namespace {

// 로컬 시간 기준 고정 시각 (mktime 으로 만들므로 타임존과 무관)
std::time_t localTime(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

}  // namespace

TEST_CASE(semester_isValid_format) {
    CHECK(semester::isValid("2026-1"));
    CHECK(semester::isValid("2026-2"));
    CHECK(semester::isValid("2026-3"));
    CHECK(semester::isValid("2026-4"));
    CHECK(!semester::isValid(""));
    CHECK(!semester::isValid("2026-0"));
    CHECK(!semester::isValid("2026-5"));
    CHECK(!semester::isValid("2026"));
    CHECK(!semester::isValid("2026-"));
    CHECK(!semester::isValid("2026-12"));
    CHECK(!semester::isValid("026-1"));
    CHECK(!semester::isValid("20261-1"));
    CHECK(!semester::isValid("2026/1"));
    CHECK(!semester::isValid("abcd-1"));
    CHECK(!semester::isValid("2026-a"));
    CHECK(!semester::isValid(" 2026-1"));
    CHECK(!semester::isValid("2026-1 "));
}

TEST_CASE(semester_orderKey_chronological) {
    CHECK(semester::orderKey("2026-1") < semester::orderKey("2026-3"));
    CHECK(semester::orderKey("2026-3") < semester::orderKey("2026-2"));
    CHECK(semester::orderKey("2026-2") < semester::orderKey("2026-4"));
    CHECK(semester::orderKey("2026-4") < semester::orderKey("2027-1"));
    CHECK_EQ(semester::orderKey("2026-1"), 2026 * 4);
    CHECK_EQ(semester::orderKey("2026-3"), 2026 * 4 + 1);
    CHECK_EQ(semester::orderKey("2026-2"), 2026 * 4 + 2);
    CHECK_EQ(semester::orderKey("2026-4"), 2026 * 4 + 3);
    CHECK_EQ(semester::orderKey("2027-1"), 2027 * 4);
}

TEST_CASE(semester_orderKey_invalid_is_minus_one) {
    CHECK_EQ(semester::orderKey(""), -1);
    CHECK_EQ(semester::orderKey("2026-5"), -1);
    CHECK_EQ(semester::orderKey("abcd-1"), -1);
    CHECK_EQ(semester::orderKey("2026-12"), -1);
}

TEST_CASE(semester_isPast_boundaries) {
    CHECK(semester::isPast("2025-2", "2026-1"));
    CHECK(semester::isPast("2026-1", "2026-3"));
    CHECK(semester::isPast("2026-3", "2026-2"));
    CHECK(semester::isPast("2026-2", "2026-4"));
    CHECK(semester::isPast("2026-4", "2027-1"));
    // 같은 학기는 지난 학기가 아님
    CHECK(!semester::isPast("2026-1", "2026-1"));
    CHECK(!semester::isPast("2026-4", "2026-4"));
    // 미래 학기
    CHECK(!semester::isPast("2026-2", "2026-1"));
    CHECK(!semester::isPast("2027-1", "2026-4"));
}

TEST_CASE(semester_isPast_invalid_input_is_false) {
    CHECK(!semester::isPast("", "2026-1"));
    CHECK(!semester::isPast("2026-1", ""));
    CHECK(!semester::isPast("", ""));
    CHECK(!semester::isPast("2026-9", "2026-1"));
    CHECK(!semester::isPast("2025-1", "abcd-1"));
    CHECK(!semester::isPast("garbage", "also garbage"));
}

TEST_CASE(semester_fromDate_boundaries) {
    CHECK_EQ(semester::fromDate(2026, 1), std::string("2025-2"));
    CHECK_EQ(semester::fromDate(2026, 2), std::string("2025-2"));
    CHECK_EQ(semester::fromDate(2026, 3), std::string("2026-1"));
    CHECK_EQ(semester::fromDate(2026, 6), std::string("2026-1"));
    CHECK_EQ(semester::fromDate(2026, 8), std::string("2026-1"));
    CHECK_EQ(semester::fromDate(2026, 9), std::string("2026-2"));
    CHECK_EQ(semester::fromDate(2026, 12), std::string("2026-2"));
}

TEST_CASE(semester_current_prefers_configured_value) {
    const std::time_t now = localTime(2026, 5, 10);
    CHECK_EQ(semester::current("2025-2", now), std::string("2025-2"));
    CHECK_EQ(semester::current("2026-3", now), std::string("2026-3"));
}

TEST_CASE(semester_current_falls_back_to_date_when_empty) {
    CHECK_EQ(semester::current("", localTime(2026, 5, 10)), std::string("2026-1"));
    CHECK_EQ(semester::current("", localTime(2026, 2, 28)), std::string("2025-2"));
    CHECK_EQ(semester::current("", localTime(2026, 3, 1)), std::string("2026-1"));
    CHECK_EQ(semester::current("", localTime(2026, 8, 31)), std::string("2026-1"));
    CHECK_EQ(semester::current("", localTime(2026, 9, 1)), std::string("2026-2"));
    CHECK_EQ(semester::current("", localTime(2026, 12, 31)), std::string("2026-2"));
    CHECK_EQ(semester::current("", localTime(2027, 1, 1)), std::string("2026-2"));
}
