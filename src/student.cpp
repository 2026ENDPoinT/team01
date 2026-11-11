// 담당: A (연준)
#include "student.h"

#include <cctype>

namespace {

// 공백(스페이스·탭·줄바꿈)만 있거나 비어있으면 true
bool isBlank(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

// 숫자 8자리인지
bool isEightDigits(const std::string& s) {
    if (s.size() != 8) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

}  // namespace

bool Student::isValid() const {
    if (!isEightDigits(id)) return false;  // 학번: 숫자 8자리
    if (isBlank(name)) return false;       // 이름: 비어있으면 안 됨
    if (year < 1 || year > 4) return false;  // 학년: 1~4
    return true;                           // 학과(major)는 비어 있어도 허용
}
