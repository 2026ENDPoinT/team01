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

TEST_CASE(letterToScore_table) {
    CHECK_EQ(letterToScore("A+"), 95.0);
    CHECK_EQ(letterToScore("A0"), 90.0);
    CHECK_EQ(letterToScore("B+"), 85.0);
    CHECK_EQ(letterToScore("B0"), 80.0);
    CHECK_EQ(letterToScore("C+"), 75.0);
    CHECK_EQ(letterToScore("C0"), 70.0);
    CHECK_EQ(letterToScore("D+"), 65.0);
    CHECK_EQ(letterToScore("D0"), 60.0);
    CHECK_EQ(letterToScore("F"), 0.0);
}

TEST_CASE(letterToScore_roundtrip_with_scoreToLetter) {
    for (const std::string letter : {"A+", "A0", "B+", "B0", "C+", "C0", "D+", "D0", "F"}) {
        CHECK_EQ(scoreToLetter(letterToScore(letter)), letter);
    }
}

TEST_CASE(letterToScore_rejects_unknown) {
    CHECK_EQ(letterToScore("A"), -1.0);
    CHECK_EQ(letterToScore(""), -1.0);
    CHECK_EQ(letterToScore("a+"), -1.0);   // 대소문자 구분
    CHECK_EQ(letterToScore(" A+"), -1.0);  // 공백 허용 안 함
    CHECK_EQ(letterToScore("A+ "), -1.0);
    CHECK_EQ(letterToScore("R"), -1.0);
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

// 학기 형식 "YYYY-N": 1·2 정규학기, 3 여름·4 겨울 계절학기
TEST_CASE(semester_regular_and_validity) {
    CHECK(isValidSemester("2026-1"));
    CHECK(isValidSemester("2026-4"));
    CHECK(!isValidSemester("2026-5"));
    CHECK(!isValidSemester("2026-0"));
    CHECK(!isValidSemester("26-1"));
    CHECK(!isValidSemester(""));
    CHECK(isRegularSemester("2026-1"));
    CHECK(isRegularSemester("2026-2"));
    CHECK(!isRegularSemester("2026-3"));
    CHECK(!isRegularSemester("2026-4"));
    CHECK(!isRegularSemester("2026-5"));
}

TEST_CASE(admissionYear_from_id_prefix) {
    CHECK_EQ(admissionYear("23101001"), 2023);
    CHECK_EQ(admissionYear("17101009"), 2017);
    CHECK_EQ(admissionYear("2310100"), 0);   // 7자리
    CHECK_EQ(admissionYear("2310100a"), 0);  // 숫자가 아님
    CHECK_EQ(admissionYear(""), 0);
}

// 허용 범위: 입학년도-1 ~ (올해+1)-4
TEST_CASE(isSemesterInRange_admission_to_next_year) {
    CHECK(isSemesterInRange("23101001", "2023-1", 2026));
    CHECK(!isSemesterInRange("23101001", "2022-2", 2026));
    CHECK(isSemesterInRange("23101001", "2027-4", 2026));
    CHECK(!isSemesterInRange("23101001", "2028-1", 2026));
    CHECK(!isSemesterInRange("23101001", "2026-5", 2026));  // 형식 오류
    CHECK(!isSemesterInRange("abc", "2026-1", 2026));       // 잘못된 학번
}
