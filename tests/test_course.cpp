// 담당: B (현준)
// Course: 성적 방식(gradeType) 기본값·검사, P/F 등급 판정
#include <string>

#include "check.h"
#include "course.h"
#include "test_support.h"

TEST_CASE(course_gradeType_default_is_letter) {
    Course c = makeCourse("109001", "프로그래밍기초");
    CHECK_EQ(c.gradeType, std::string(kGradeTypeLetter));
    CHECK(!c.isPassFail());
    CHECK(c.isValid());
}

TEST_CASE(course_gradeType_pf_and_invalid) {
    Course c = makeCourse("109001", "사회봉사");
    c.gradeType = kGradeTypePassFail;
    CHECK(c.isPassFail());
    CHECK(c.isValid());

    c.gradeType = "PF";      // 대소문자 다름
    CHECK(!c.isValid());
    c.gradeType = "";
    CHECK(!c.isValid());

    CHECK(isValidGradeType("letter"));
    CHECK(isValidGradeType("pf"));
    CHECK(!isValidGradeType("pass"));
}

TEST_CASE(isPassFailLetter_only_P_and_NP) {
    CHECK(isPassFailLetter("P"));
    CHECK(isPassFailLetter("NP"));
    CHECK(!isPassFailLetter("p"));
    CHECK(!isPassFailLetter("F"));
    CHECK(!isPassFailLetter("A+"));
    CHECK(!isPassFailLetter(""));
}
