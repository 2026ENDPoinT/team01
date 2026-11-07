// 담당: A (연준)
// 학생 한 명의 정보를 담는 데이터 클래스
#pragma once

#include <string>

struct Student {
    std::string id;     // 학번 (예: "24101234"), 숫자 8자리
    std::string name;   // 이름
    std::string major;  // 학과
    int year = 1;       // 학년 (1~4)

    // 학번 형식, 이름 비어있지 않음, 학년 범위 등을 검사
    bool isValid() const;
};
