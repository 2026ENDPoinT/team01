// 담당: A (연준)
// 평가 항목: 6분류, finalScore 계산(완성·미완성·0% 항목), Assessment·AssessmentScore·GradingMode 값 검사
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

GradingMode makeMode(const std::string& mode) {
    GradingMode m;
    m.courseCode = "109001";
    m.semester = "2026-2";
    m.mode = mode;
    return m;
}

// 출석 10, 중간고사 30, 기말고사 40, 과제 20, 팀프로젝트 0, 기타 0 (합계 100)
std::vector<Assessment> sixItems() {
    return {makeItem("출석", 10),  makeItem("중간고사", 30),  makeItem("기말고사", 40),
            makeItem("과제", 20),  makeItem("팀프로젝트", 0), makeItem("기타", 0)};
}

// 출석 100, 중간 90, 기말 80, 과제 100 (팀·기타 점수는 없음)
std::vector<AssessmentScore> fourScores() {
    return {makeScore("출석", 100), makeScore("중간고사", 90), makeScore("기말고사", 80), makeScore("과제", 100)};
}

}  // namespace

TEST_CASE(assessmentCategories_six_in_order) {
    const std::vector<std::string>& c = assessmentCategories();
    CHECK_EQ(c.size(), static_cast<size_t>(6));
    CHECK(c.size() == 6 && c[0] == "출석" && c[1] == "중간고사" && c[2] == "기말고사" && c[3] == "과제" &&
          c[4] == "팀프로젝트" && c[5] == "기타");
    CHECK_EQ(std::string(kAttendanceCategory), std::string("출석"));
}

TEST_CASE(finalScore_weighted_sum) {
    // 출석 100×0.1 + 중간 90×0.3 + 기말 80×0.4 + 과제 100×0.2 = 10 + 27 + 32 + 20 = 89 (팀·기타 0% 는 점수 없어도 됨)
    CHECK_NEAR(finalScore(sixItems(), fourScores()), 89.0, 1e-9);

    // 점수 순서는 상관없고 이름으로 맞춤
    const std::vector<AssessmentScore> reversed = {makeScore("과제", 100), makeScore("기말고사", 80),
                                                   makeScore("중간고사", 90), makeScore("출석", 100)};
    CHECK_NEAR(finalScore(sixItems(), reversed), 89.0, 1e-9);

    // 0% 항목에 점수가 있어도 무시
    std::vector<AssessmentScore> withZeroWeight = fourScores();
    withZeroWeight.push_back(makeScore("팀프로젝트", 0));
    CHECK_NEAR(finalScore(sixItems(), withZeroWeight), 89.0, 1e-9);

    // 소수 둘째 자리 반올림: 33.333×0.3 = 9.9999 → 10.00
    const std::vector<AssessmentScore> fraction = {makeScore("출석", 0), makeScore("중간고사", 33.333),
                                                   makeScore("기말고사", 0), makeScore("과제", 0)};
    CHECK_NEAR(finalScore(sixItems(), fraction), 10.0, 1e-9);

    // 경계: 한 분류 100% → 그 점수 그대로
    CHECK_NEAR(finalScore({makeItem("기말고사", 100)}, {makeScore("기말고사", 100)}), 100.0, 1e-9);
}

TEST_CASE(finalScore_incomplete_is_minus_one) {
    // 비율이 0 보다 큰 기말고사 점수가 없음
    const std::vector<AssessmentScore> missing = {makeScore("출석", 100), makeScore("중간고사", 90),
                                                  makeScore("과제", 100)};
    CHECK_EQ(finalScore(sixItems(), missing), -1.0);

    // 비율 합계 99, 101
    std::vector<Assessment> sum99 = sixItems();
    sum99[0].weight = 9;
    CHECK_EQ(finalScore(sum99, fourScores()), -1.0);
    std::vector<Assessment> sum101 = sixItems();
    sum101[0].weight = 11;
    CHECK_EQ(finalScore(sum101, fourScores()), -1.0);

    // 항목이 없음 (점수가 있어도)
    CHECK_EQ(finalScore({}, fourScores()), -1.0);
    CHECK_EQ(finalScore({}, {}), -1.0);
}

TEST_CASE(assessment_isValid_rules) {
    for (const std::string& name : assessmentCategories()) {
        CHECK(makeItem(name, 10).isValid());
    }
    CHECK(makeItem("기말고사", 0).isValid());    // 0% 허용
    CHECK(makeItem("기말고사", 100).isValid());

    CHECK(!makeItem("중간", 30).isValid());      // 6분류가 아닌 이름
    CHECK(!makeItem("퀴즈", 30).isValid());
    CHECK(!makeItem("", 30).isValid());
    CHECK(!makeItem(" 과제", 30).isValid());     // 글자 그대로만
    CHECK(!makeItem("중간고사", 101).isValid());
    CHECK(!makeItem("중간고사", -1).isValid());

    Assessment badCode = makeItem("중간고사", 30);
    badCode.courseCode = "10900";
    CHECK(!badCode.isValid());
    Assessment badSemester = makeItem("중간고사", 30);
    badSemester.semester = "2026-5";
    CHECK(!badSemester.isValid());
}

TEST_CASE(assessmentScore_isValid_rules) {
    CHECK(makeScore("중간고사", 0).isValid());
    CHECK(makeScore("중간고사", 100).isValid());
    CHECK(makeScore("출석", 87.5).isValid());

    CHECK(!makeScore("중간", 50).isValid());      // 6분류가 아닌 이름
    CHECK(!makeScore("", 50).isValid());
    CHECK(!makeScore("중간고사", -1).isValid());
    CHECK(!makeScore("중간고사", 100.5).isValid());
    CHECK(!makeScore("중간고사", std::numeric_limits<double>::quiet_NaN()).isValid());

    AssessmentScore badId = makeScore("중간고사", 50);
    badId.studentId = "2610100a";
    CHECK(!badId.isValid());
    AssessmentScore shortId = makeScore("중간고사", 50);
    shortId.studentId = "2610100";
    CHECK(!shortId.isValid());
    AssessmentScore badCode = makeScore("중간고사", 50);
    badCode.courseCode = "abcdef";
    CHECK(!badCode.isValid());
    AssessmentScore badSemester = makeScore("중간고사", 50);
    badSemester.semester = "2026";
    CHECK(!badSemester.isValid());
}

TEST_CASE(gradingMode_isValid_rules) {
    CHECK(makeMode(kGradingAbsolute).isValid());
    CHECK(makeMode(kGradingRelative).isValid());
    CHECK(!makeMode("rel").isValid());
    CHECK(!makeMode("").isValid());
    CHECK(!makeMode("Absolute").isValid());

    GradingMode badCode = makeMode(kGradingAbsolute);
    badCode.courseCode = "1090";
    CHECK(!badCode.isValid());
    GradingMode badSemester = makeMode(kGradingAbsolute);
    badSemester.semester = "2026-0";
    CHECK(!badSemester.isValid());
}
