// 담당: A (연준)
// 과목 한 개의 정보를 담는 데이터 클래스
#pragma once

#include <string>

struct Course {
    std::string code;       // 과목 코드 (예: "CS101")
    std::string name;       // 과목명
    int credit = 3;         // 학점 (1~4)
    std::string professor;  // 담당 교수

    // 코드·과목명 비어있지 않음, 학점 범위 등을 검사
    bool isValid() const;
};
