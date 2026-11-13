// 담당: B (현준)
// 서버 요청 값 검사: 어느 항목이 틀렸는지 짚는 오류 문구를 돌려줌 (과목 오류 문구 #49 와 같은 방식)
//   맞으면 "", 틀리면 이유 (예: "학점은 1~4 사이여야 합니다"). 서버는 앞에 "과목 정보가 올바르지 않습니다 (…)" 처럼 붙여 400 으로 보냄
#pragma once

#include <string>

#include "course.h"
#include "json.hpp"

namespace RequestValidation {

// 과목 등록·수정 요청 본문을 course 에 읽고 값까지 검사 (readCourse 실패 → courseError 순서)
//   defaultCode: 본문에 code 가 없을 때 쓸 과목코드 (수정은 주소의 과목코드, 등록은 "" → 과목코드 필수)
//   name·credit·professor 는 필수, category·area·schedule·language·capacity·remote·gradeType 은 없거나 null 이면 기본값
//   (gradeType 기본 "letter". 수정할 때 본문에 없으면 기존 값을 유지하는 것은 서버가 함)
//   타입이 틀리면 그 항목 이유 (예: 학점 "abc" → "학점은 숫자여야 합니다", 3.5 → "학점은 정수여야 합니다")
std::string readCourse(const nlohmann::json& body, const std::string& defaultCode, Course& course);

// 읽은 과목 값 검사: 과목코드 형식, 과목명 비움, 학점 1~4, 이수구분·강의언어 목록, 강의시간 형식, 정원 0~999, 성적 방식
std::string courseError(const Course& course);

// 새 과목 등록에만 더 보는 항목: 담당 교수·강의시간 필수 (공백만 있어도 비어 있는 것으로 봄)
//   수정은 검사하지 않음 (강의시간이 빈 기존 과목·원격강좌를 그대로 고칠 수 있게)
std::string newCourseError(const Course& course);

// 계정 값 검사 (만들기·수정 공용)
//   checkUsername: 만들 때만 아이디 형식 검사 (수정은 아이디를 바꿀 수 없음)
//   password: 만들 때는 필수, 수정할 때는 "" 이면 바꾸지 않으므로 검사 안 함 (passwordRequired 로 구분)
//   검사 순서: 아이디 → 역할 → 비밀번호 → (교수·관리자) 이름 → (학생) 학번
std::string accountError(const std::string& username, bool checkUsername, const std::string& role,
                         const std::string& name, const std::string& studentId, const std::string& password,
                         bool passwordRequired);

}  // namespace RequestValidation
