// 담당: A (연준)
#include "course.h"

#include <cctype>

namespace {

// 공백(스페이스·탭·줄바꿈)만 있거나 비어있으면 true
bool isBlank(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

}  // namespace

bool Course::isValid() const {
    if (isBlank(code)) return false;        // 과목 코드: 비어있으면 안 됨
    if (isBlank(name)) return false;        // 과목명: 비어있으면 안 됨
    if (credit < 1 || credit > 4) return false;  // 학점: 1~4
    return true;                            // 담당 교수(professor)는 비어 있어도 허용
}
