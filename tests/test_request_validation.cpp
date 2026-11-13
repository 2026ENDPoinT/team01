// 담당: B (현준)
// 서버 요청 값 검사: 과목·계정 요청이 틀린 항목마다 다른 문구를 돌려주는지, 올바른 입력은 그대로 통과하는지
#include <string>

#include "check.h"
#include "request_validation.h"

using json = nlohmann::json;
using RequestValidation::accountError;
using RequestValidation::readCourse;

namespace {

// 올바른 과목 등록 본문
json courseBody() {
    return {{"code", "109999"}, {"name", "테스트과목"}, {"credit", 3}, {"professor", "김철수"}, {"category", "전공선택"}};
}

json with(json body, const char* key, const json& value) {
    body[key] = value;
    return body;
}

json without(json body, const char* key) {
    body.erase(key);
    return body;
}

std::string courseReason(const json& body, const std::string& defaultCode = "") {
    Course c;
    return readCourse(body, defaultCode, c);
}

}  // namespace

TEST_CASE(readCourse_valid_body_and_defaults) {
    Course c;
    CHECK_EQ(readCourse(courseBody(), "", c), std::string(""));
    CHECK(c.code == "109999" && c.name == "테스트과목" && c.credit == 3 && c.professor == "김철수");
    CHECK(c.category == "전공선택" && c.language == "한국어전용" && c.capacity == 0 && !c.remote);

    // 선택 항목: 없거나 null 이면 기본값, 있으면 그 값. 3.0 처럼 소수점이 없는 실수도 정수로 받음
    json full = courseBody();
    full["credit"] = 2.0;
    full["capacity"] = 40;
    full["remote"] = true;
    full["language"] = nullptr;
    full["schedule"] = "월(1~2) 수(3)";
    CHECK_EQ(readCourse(full, "", c), std::string(""));
    CHECK(c.credit == 2 && c.capacity == 40 && c.remote && c.language == "한국어전용" && c.schedule == "월(1~2) 수(3)");

    // 수정: 본문에 code 가 없으면 주소의 과목코드
    CHECK_EQ(readCourse(without(courseBody(), "code"), "109001", c), std::string(""));
    CHECK_EQ(c.code, std::string("109001"));
}

TEST_CASE(readCourse_each_wrong_field_has_its_own_message) {
    // 이슈 완료 기준: 이름 비움 / 학점 0 / 학점 "abc" / 이수구분 오류 가 서로 다른 문구
    const std::string blankName = courseReason(with(courseBody(), "name", "  "));
    const std::string zeroCredit = courseReason(with(courseBody(), "credit", 0));
    const std::string textCredit = courseReason(with(courseBody(), "credit", "abc"));
    const std::string badCategory = courseReason(with(courseBody(), "category", "전공"));
    CHECK_EQ(blankName, std::string("과목명이 비어 있습니다"));
    CHECK_EQ(zeroCredit, std::string("학점은 1~4 사이여야 합니다"));
    CHECK_EQ(textCredit, std::string("학점은 숫자여야 합니다"));
    CHECK_EQ(badCategory, std::string("이수구분이 올바르지 않습니다"));

    CHECK_EQ(courseReason(with(courseBody(), "credit", 3.5)), std::string("학점은 정수여야 합니다"));  // 잘라서 받지 않음
    CHECK_EQ(courseReason(with(courseBody(), "credit", 5)), std::string("학점은 1~4 사이여야 합니다"));
    CHECK_EQ(courseReason(with(courseBody(), "credit", 1e12)), std::string("학점은 1~4 사이여야 합니다"));
    CHECK_EQ(courseReason(without(courseBody(), "credit")), std::string("학점이 없습니다"));
    CHECK_EQ(courseReason(without(courseBody(), "name")), std::string("과목명이 비어 있습니다"));
    CHECK_EQ(courseReason(with(courseBody(), "name", 12)), std::string("과목명은 문자열이어야 합니다"));
    CHECK_EQ(courseReason(without(courseBody(), "code")), std::string("과목코드는 숫자 6자리여야 합니다"));
    CHECK_EQ(courseReason(with(courseBody(), "code", "10999")), std::string("과목코드는 숫자 6자리여야 합니다"));
    CHECK_EQ(courseReason(without(courseBody(), "professor")), std::string("담당 교수 항목이 없습니다"));
    CHECK_EQ(courseReason(with(courseBody(), "capacity", "많이")), std::string("정원은 숫자여야 합니다"));
    CHECK_EQ(courseReason(with(courseBody(), "capacity", 1000)), std::string("정원은 0~999 사이여야 합니다"));
    CHECK_EQ(courseReason(with(courseBody(), "remote", "Y")), std::string("원격강좌 여부는 true 또는 false 여야 합니다"));
    CHECK_EQ(courseReason(with(courseBody(), "language", "중국어")), std::string("강의언어가 올바르지 않습니다"));
    CHECK(courseReason(with(courseBody(), "schedule", "월요일")).rfind("강의시간 형식이 올바르지 않습니다", 0) == 0);
    CHECK_EQ(courseReason(json::array()), std::string("요청 본문은 JSON 객체여야 합니다"));

    // 실패하면 course 를 바꾸지 않음
    Course c;
    c.name = "그대로";
    CHECK(!readCourse(with(courseBody(), "credit", "abc"), "", c).empty());
    CHECK_EQ(c.name, std::string("그대로"));
}

TEST_CASE(accountError_each_wrong_field_has_its_own_message) {
    // 만들기: 올바른 입력
    CHECK_EQ(accountError("newprof", true, "professor", "새교수", "", "password123", true), std::string(""));
    CHECK_EQ(accountError("stu_01", true, "student", "", "25103008", "password123", true), std::string(""));

    // 이슈 완료 기준: 짧은 비밀번호 / 교수 이름 누락 이 서로 다른 문구
    CHECK_EQ(accountError("newprof", true, "professor", "새교수", "", "short", true),
             std::string("비밀번호는 8~64자여야 합니다"));
    CHECK_EQ(accountError("newprof", true, "professor", "  ", "", "password123", true),
             std::string("교수·관리자 계정은 이름이 필요합니다"));

    CHECK_EQ(accountError("a!", true, "professor", "새교수", "", "password123", true),
             std::string("아이디는 영문·숫자·_ 3~20자여야 합니다"));
    CHECK_EQ(accountError("한글아이디", true, "professor", "새교수", "", "password123", true),
             std::string("아이디는 영문·숫자·_ 3~20자여야 합니다"));
    CHECK_EQ(accountError("newprof", true, "boss", "새교수", "", "password123", true),
             std::string("역할은 admin·professor·student 중 하나여야 합니다"));
    CHECK_EQ(accountError("newstu", true, "student", "", "", "password123", true),
             std::string("학생 계정은 학번이 필요합니다"));
    CHECK_EQ(accountError("newprof", true, "professor", "새교수", "", std::string(65, 'a'), true),
             std::string("비밀번호는 8~64자여야 합니다"));
    CHECK_EQ(accountError("newprof", true, "professor", "새교수", "", "", true),
             std::string("비밀번호는 8~64자여야 합니다"));  // 만들 때는 필수

    // 수정: 아이디는 검사하지 않고, 비밀번호는 "" 이면 그대로 둠 (넣었을 때만 검사)
    CHECK_EQ(accountError("x", false, "professor", "김철수", "", "", false), std::string(""));
    CHECK_EQ(accountError("x", false, "professor", "김철수", "", "short", false),
             std::string("비밀번호는 8~64자여야 합니다"));
    CHECK_EQ(accountError("x", false, "professor", "", "", "", false),
             std::string("교수·관리자 계정은 이름이 필요합니다"));
}

TEST_CASE(readCourse_gradeType) {
    Course c;
    CHECK_EQ(readCourse(with(courseBody(), "gradeType", "pf"), "", c), std::string(""));
    CHECK(c.isPassFail());
    CHECK_EQ(readCourse(courseBody(), "", c), std::string(""));
    CHECK_EQ(c.gradeType, std::string(kGradeTypeLetter));  // 없으면 기본 letter
    CHECK_EQ(courseReason(with(courseBody(), "gradeType", "pass")),
             std::string("성적 방식은 letter(등급) 또는 pf(P/F) 여야 합니다"));
    CHECK_EQ(courseReason(with(courseBody(), "gradeType", 1)), std::string("성적 방식은 문자열이어야 합니다"));
}
