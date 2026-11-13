// 담당: A (연준)
// 학생 한 명이 한 학기에 한 과목에서 받은 성적
#pragma once

#include <string>

struct Grade {
    std::string studentId;   // Student::id
    std::string courseCode;  // Course::code
    std::string semester;    // 학기 (예: "2026-1", "2026-2")
    double score = 0.0;      // 점수 (0~100)
    std::string assignedLetter;   // 상대평가로 정해진 등급, 또는 P/F 과목의 "P"·"NP" ("" 이면 점수 기준)

    // 학번·과목코드·학기 형식, 점수 범위 등을 검사
    bool isValid() const;

    // 원래 등급 (재수강 규칙 적용 전): assignedLetter 가 있으면 그것, 없으면 scoreToLetter(score)
    //   (지금은 scoreToLetter(score) 만 돌려줌. 구현: #67)
    //   화면·성적표·GPA 에는 재수강 규칙을 적용한 Retake::officialLetter 를 씀 (retake.h)
    std::string letter() const;
};

// 점수 → 등급 변환
//   95~ A+, 90~ A0, 85~ B+, 80~ B0, 75~ C+, 70~ C0, 65~ D+, 60~ D0, 그 외 F
std::string scoreToLetter(double score);

// 등급 → 평점 변환 (4.5 만점)
//   A+ 4.5, A0 4.0, B+ 3.5, B0 3.0, C+ 2.5, C0 2.0, D+ 1.5, D0 1.0, F 0.0
//   모르는 문자열이면 0.0
double letterToPoint(const std::string& letter);

// 등급 → 직접 입력용 대표 점수
//   A+ 95, A0 90, B+ 85, B0 80, C+ 75, C0 70, D+ 65, D0 60, F 0
//   대소문자를 구분하고 공백은 허용하지 않음. 모르는 문자열이면 -1
//   scoreToLetter(letterToScore(x)) == x 가 성립
double letterToScore(const std::string& letter);

// 학기 형식 검사: "YYYY-N", N: 1·2 정규학기, 3 여름 계절학기, 4 겨울 계절학기 (예: "2026-1")
bool isValidSemester(const std::string& semester);

// 형식이 맞고 정규학기(N 이 1 또는 2)이면 true
bool isRegularSemester(const std::string& semester);

// 학번 앞 2자리로 본 입학년도 (예: "23101001" → 2023). 학번이 숫자 8자리가 아니면 0
int admissionYear(const std::string& studentId);

// 이 학생이 등록할 수 있는 학기인지: 형식이 맞고 연도가 입학년도 ~ currentYear+1 이면 true (N 은 1~4 모두)
//   currentYear 는 올해 (테스트하기 쉽게 인자로 받음)
bool isSemesterInRange(const std::string& studentId, const std::string& semester, int currentYear);
