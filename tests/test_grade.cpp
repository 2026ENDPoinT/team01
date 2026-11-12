// 담당: 조장 (Eno74)
// 점수 → 등급 → 평점 변환 (grade.h: "이상" 기준, 반올림 없음)
#include <string>

#include "check.h"
#include "grade.h"

TEST_CASE(scoreToLetter_boundaries) {
    CHECK_EQ(scoreToLetter(100), std::string("A+"));
    CHECK_EQ(scoreToLetter(95), std::string("A+"));
    CHECK_EQ(scoreToLetter(94.9), std::string("A0"));
    CHECK_EQ(scoreToLetter(90), std::string("A0"));
    CHECK_EQ(scoreToLetter(89.99), std::string("B+"));
    CHECK_EQ(scoreToLetter(85), std::string("B+"));
    CHECK_EQ(scoreToLetter(80), std::string("B0"));
    CHECK_EQ(scoreToLetter(75), std::string("C+"));
    CHECK_EQ(scoreToLetter(70), std::string("C0"));
    CHECK_EQ(scoreToLetter(65), std::string("D+"));
    CHECK_EQ(scoreToLetter(60), std::string("D0"));
    CHECK_EQ(scoreToLetter(59.5), std::string("F"));  // 반올림하지 않음
    CHECK_EQ(scoreToLetter(0), std::string("F"));
}

TEST_CASE(gradeLetter_matches_scoreToLetter) {
    Grade g;
    g.score = 94.9;
    CHECK_EQ(g.letter(), scoreToLetter(94.9));
}

TEST_CASE(letterToPoint_table) {
    CHECK_EQ(letterToPoint("A+"), 4.5);
    CHECK_EQ(letterToPoint("A0"), 4.0);
    CHECK_EQ(letterToPoint("B+"), 3.5);
    CHECK_EQ(letterToPoint("B0"), 3.0);
    CHECK_EQ(letterToPoint("C+"), 2.5);
    CHECK_EQ(letterToPoint("C0"), 2.0);
    CHECK_EQ(letterToPoint("D+"), 1.5);
    CHECK_EQ(letterToPoint("D0"), 1.0);
    CHECK_EQ(letterToPoint("F"), 0.0);
    CHECK_EQ(letterToPoint("R"), 0.0);
    CHECK_EQ(letterToPoint(""), 0.0);
}

TEST_CASE(gradeIsValid_score_range) {
    Grade g;
    g.studentId = "24101234";
    g.courseCode = "109004";
    g.semester = "2026-1";
    g.score = 0;
    CHECK(g.isValid());
    g.score = 100;
    CHECK(g.isValid());
    g.score = 100.01;
    CHECK(!g.isValid());
    g.score = -0.01;
    CHECK(!g.isValid());
}
