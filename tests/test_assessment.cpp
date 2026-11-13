// 담당: A (연준)
// 평가 항목: finalScore 계산(완성·미완성), Assessment·AssessmentScore 값 검사
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "assessment.h"
#include "check.h"

namespace {

Assessment makeItem(const std::string& name, int weight) {
    Assessment a;
    a.courseCode = "109001";
    a.semester = "2026-2";
    a.name = name;
    a.weight = weight;
    return a;
}

AssessmentScore makeScore(const std::string& name, double score) {
    AssessmentScore s;
    s.studentId = "26101001";
    s.courseCode = "109001";
    s.semester = "2026-2";
    s.name = name;
    s.score = score;
    return s;
}

// 중간 30, 기말 40, 과제 20, 출석 10 (합계 100)
std::vector<Assessment> fourItems() {
    return {makeItem("중간", 30), makeItem("기말", 40), makeItem("과제", 20), makeItem("출석", 10)};
}

}  // namespace

TEST_CASE(finalScore_weighted_sum) {
    // 중간 90×0.3 + 기말 80×0.4 + 과제 100×0.2 + 출석 100×0.1 = 27 + 32 + 20 + 10 = 89
    const std::vector<AssessmentScore> scores = {makeScore("중간", 90), makeScore("기말", 80),
                                                 makeScore("과제", 100), makeScore("출석", 100)};
    CHECK_NEAR(finalScore(fourItems(), scores), 89.0, 1e-9);

    // 점수 순서는 상관없고 이름으로 맞춤
    const std::vector<AssessmentScore> reversed = {makeScore("출석", 100), makeScore("과제", 100),
                                                   makeScore("기말", 80), makeScore("중간", 90)};
    CHECK_NEAR(finalScore(fourItems(), reversed), 89.0, 1e-9);

    // 소수 둘째 자리 반올림: 33.333×0.3 + 0 = 9.9999 → 10.00
    const std::vector<AssessmentScore> fraction = {makeScore("중간", 33.333), makeScore("기말", 0),
                                                   makeScore("과제", 0), makeScore("출석", 0)};
    CHECK_NEAR(finalScore(fourItems(), fraction), 10.0, 1e-9);

    // 경계: 모두 0 → 0, 모두 100 → 100
    CHECK_NEAR(finalScore(fourItems(), {makeScore("중간", 0), makeScore("기말", 0), makeScore("과제", 0),
                                        makeScore("출석", 0)}),
               0.0, 1e-9);
    CHECK_NEAR(finalScore({makeItem("기말", 100)}, {makeScore("기말", 100)}), 100.0, 1e-9);
}

TEST_CASE(finalScore_incomplete_is_minus_one) {
    const std::vector<AssessmentScore> full = {makeScore("중간", 80), makeScore("기말", 90),
                                               makeScore("과제", 100), makeScore("출석", 100)};

    // 비율 합계 99
    std::vector<Assessment> sum99 = fourItems();
    sum99[3].weight = 9;
    CHECK_EQ(finalScore(sum99, full), -1.0);

    // 비율 합계 101 도 미완성
    std::vector<Assessment> sum101 = fourItems();
    sum101[3].weight = 11;
    CHECK_EQ(finalScore(sum101, full), -1.0);

    // 항목 하나(기말)의 점수가 없음
    const std::vector<AssessmentScore> missing = {makeScore("중간", 80), makeScore("과제", 100),
                                                  makeScore("출석", 100)};
    CHECK_EQ(finalScore(fourItems(), missing), -1.0);

    // 항목이 없음 (점수가 있어도)
    CHECK_EQ(finalScore({}, full), -1.0);
    CHECK_EQ(finalScore({}, {}), -1.0);
}

TEST_CASE(assessment_isValid_rules) {
    CHECK(makeItem("중간", 30).isValid());
    CHECK(makeItem("출석", 1).isValid());
    CHECK(makeItem("기말", 100).isValid());
    CHECK(makeItem("과제, 발표", 20).isValid());  // 쉼표는 허용 (CSV 에서 따옴표로 감쌈)

    CHECK(!makeItem("중간", 0).isValid());
    CHECK(!makeItem("중간", 101).isValid());
    CHECK(!makeItem("", 30).isValid());
    CHECK(!makeItem("   ", 30).isValid());   // 공백만
    CHECK(!makeItem(" \t", 30).isValid());
    CHECK(!makeItem("중간\n기말", 30).isValid());  // 줄바꿈은 CSV 한 줄에 못 넣음

    CHECK(makeItem(std::string(60, 'a'), 30).isValid());   // 60바이트까지
    CHECK(!makeItem(std::string(61, 'a'), 30).isValid());

    Assessment badCode = makeItem("중간", 30);
    badCode.courseCode = "10900";
    CHECK(!badCode.isValid());
    Assessment badSemester = makeItem("중간", 30);
    badSemester.semester = "2026-5";
    CHECK(!badSemester.isValid());
}

TEST_CASE(assessmentScore_isValid_rules) {
    CHECK(makeScore("중간", 0).isValid());
    CHECK(makeScore("중간", 100).isValid());
    CHECK(makeScore("중간", 87.5).isValid());

    CHECK(!makeScore("중간", -1).isValid());
    CHECK(!makeScore("중간", 100.5).isValid());
    CHECK(!makeScore("중간", std::numeric_limits<double>::quiet_NaN()).isValid());
    CHECK(!makeScore("", 50).isValid());

    AssessmentScore badId = makeScore("중간", 50);
    badId.studentId = "2610100a";
    CHECK(!badId.isValid());
    AssessmentScore shortId = makeScore("중간", 50);
    shortId.studentId = "2610100";
    CHECK(!shortId.isValid());
    AssessmentScore badCode = makeScore("중간", 50);
    badCode.courseCode = "abcdef";
    CHECK(!badCode.isValid());
    AssessmentScore badSemester = makeScore("중간", 50);
    badSemester.semester = "2026";
    CHECK(!badSemester.isValid());
}
