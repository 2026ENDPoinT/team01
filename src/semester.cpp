#include "semester.h"

#include <cctype>

namespace semester {

bool isValid(const std::string& semester) {
    if (semester.size() != 6 || semester[4] != '-') {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(semester[i]))) {
            return false;
        }
    }
    return semester[5] >= '1' && semester[5] <= '4';
}

int orderKey(const std::string& semester) {
    if (!isValid(semester)) {
        return -1;
    }
    const int year = std::stoi(semester.substr(0, 4));
    // 학기 번호 → 같은 해 안에서의 순서 (1학기 0, 여름 1, 2학기 2, 겨울 3)
    static const int kOrderInYear[] = {0, 0, 2, 1, 3};  // 인덱스: 학기 번호 (0 은 사용 안 함)
    return year * 4 + kOrderInYear[semester[5] - '0'];
}

bool isPast(const std::string& semester, const std::string& current) {
    const int a = orderKey(semester);
    const int b = orderKey(current);
    if (a < 0 || b < 0) {
        return false;
    }
    return a < b;
}

std::string fromDate(int year, int month) {
    if (month >= 3 && month <= 8) {
        return std::to_string(year) + "-1";
    }
    if (month >= 9) {
        return std::to_string(year) + "-2";
    }
    return std::to_string(year - 1) + "-2";  // 1~2월: 전 해 2학기 (겨울방학)
}

std::string current(const std::string& enrollmentSemester, std::time_t now) {
    if (!enrollmentSemester.empty()) {
        return enrollmentSemester;
    }
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return fromDate(local.tm_year + 1900, local.tm_mon + 1);
}

}  // namespace semester
