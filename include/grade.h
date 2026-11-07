// 담당: A (연준)
// 학생 한 명이 한 학기에 한 과목에서 받은 성적
#pragma once

#include <string>

struct Grade {
    std::string studentId;   // Student::id
    std::string courseCode;  // Course::code
    std::string semester;    // 학기 (예: "2026-1", "2026-2")
    double score = 0.0;      // 점수 (0~100)

    // 학번·과목코드·학기 형식, 점수 범위 등을 검사
    bool isValid() const;

    // 이 성적의 등급 (scoreToLetter(score) 와 같음)
    std::string letter() const;
};

// 점수 → 등급 변환
//   95~ A+, 90~ A0, 85~ B+, 80~ B0, 75~ C+, 70~ C0, 65~ D+, 60~ D0, 그 외 F
std::string scoreToLetter(double score);

// 등급 → 평점 변환 (4.5 만점)
//   A+ 4.5, A0 4.0, B+ 3.5, B0 3.0, C+ 2.5, C0 2.0, D+ 1.5, D0 1.0, F 0.0
//   모르는 문자열이면 0.0
double letterToPoint(const std::string& letter);
