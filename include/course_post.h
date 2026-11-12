// 담당: 조장 (Eno74)
// 과목 게시물: 수업자료 · 중간고사 · 기말고사 · 과제 (과목 페이지에 학기·주차별로 표시)
#pragma once

#include <string>
#include <vector>

struct CoursePost {
    int id = 0;               // 1부터. GradeManager::addPost 가 정함
    std::string courseCode;   // 숫자 6자리
    std::string semester;     // isValidSemester 형식 (예: 2026-1)
    std::string category;     // postCategories() 중 하나
    int week = 1;             // 주차 1~16
    std::string title;        // 필수(공백만 안 됨), 줄바꿈(\r \n) 없음, 200바이트 이하
    std::string body;         // 선택, 5000바이트 이하, 줄바꿈 가능
    std::string link;         // "" 또는 "http://"·"https://" 로 시작, 공백 문자 없음, 500바이트 이하
    std::string dueDate;      // "" 또는 YYYY-MM-DD 또는 YYYY-MM-DD HH:MM (월 01~12, 일 01~31, 시 00~23, 분 00~59)
    std::string author;       // 작성 계정 아이디 (서버가 채움, 검사 안 함)
    std::string createdAt;    // YYYY-MM-DD HH:MM (서버가 채움, 검사 안 함)

    // 위 규칙 전부 검사 (author·createdAt 제외)
    bool isValid() const;
};

// {"material", "midterm", "final", "assignment"} (화면 이름: 수업자료, 중간고사, 기말고사, 과제)
const std::vector<std::string>& postCategories();
