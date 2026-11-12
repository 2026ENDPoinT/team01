// 담당: A (연준)
// 과목 한 개의 정보를 담는 데이터 클래스
#pragma once

#include <string>
#include <vector>

struct Course {
    std::string code;                      // 과목 코드: 숫자 6자리 (예: "109004")
    std::string name;                      // 과목명
    int credit = 3;                        // 학점 (1~4)
    std::string professor;                 // 담당 교수 (비어 있어도 됨)
    std::string category = "전공선택";     // 이수구분 (courseCategories() 중 하나)
    std::string area;                      // 교양영역 (예: "인문", 비어 있어도 됨)
    std::string schedule;                  // 강의시간 (예: "월(1~4)", "화(2~3) 목(2~3)", 비어 있어도 됨)
    std::string language = "한국어전용";   // 강의언어 (courseLanguages() 중 하나)
    int capacity = 0;                      // 학기당 정원 (0 이면 제한 없음, 최대 999)
    bool remote = false;                   // 원격강좌 여부

    // 과목코드 형식·과목명 필수, 학점 범위, 이수구분·강의언어 목록, 강의시간 형식, 정원 범위를 검사
    bool isValid() const;
};

// 과목코드 형식 검사: 숫자 6자리 (예: "100762", "109004"). Grade::isValid 도 같은 규칙을 씀
bool isValidCourseCode(const std::string& code);

// 이수구분 목록: 전공필수, 전공선택, 교양필수, 교양선택, 일반선택
const std::vector<std::string>& courseCategories();

// 강의언어 목록: 한국어전용, 영어전용, 모듈강좌(M)
const std::vector<std::string>& courseLanguages();

// 강의시간 형식 검사. 빈 문자열은 허용 (시간 미정·원격강좌)
//   한 칸: 요일(월~일) + "(교시)" 또는 "(시작~끝)", 교시는 1~30, 시작 <= 끝
//   여러 칸은 공백이나 쉼표로 구분. 예) "월(1~4)", "화(2~3) 목(2~3)", "금(5)"
bool isValidSchedule(const std::string& schedule);

// 두 강의시간이 겹치는지: 같은 요일에 교시 구간이 하나라도 겹치면 true
//   빈 문자열(시간 미정·원격강좌)이나 잘못된 칸은 무시 (겹침 없음). 예) "월(1~4)" 과 "월(3~5)" → true
bool schedulesOverlap(const std::string& a, const std::string& b);
