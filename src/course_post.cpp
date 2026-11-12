#include "course_post.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include "grade.h"

namespace {

// 과목코드: 숫자 6자리
bool isSixDigits(const std::string& code) {
    return code.size() == 6 && std::all_of(code.begin(), code.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

// 공백 문자(스페이스, 탭, 줄바꿈 등)만으로 이루어졌거나 비어 있으면 true
bool isBlank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
}

bool hasLineBreak(const std::string& text) {
    return text.find_first_of("\r\n") != std::string::npos;
}

// "" 또는 http://, https:// 로 시작하고 공백 문자가 없는 500바이트 이하 문자열
bool isValidLink(const std::string& link) {
    if (link.empty()) {
        return true;
    }
    if (link.size() > 500) {
        return false;
    }
    if (link.rfind("http://", 0) != 0 && link.rfind("https://", 0) != 0) {
        return false;
    }
    return std::none_of(link.begin(), link.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
}

// "" 또는 YYYY-MM-DD 또는 YYYY-MM-DD HH:MM (월 01~12, 일 01~31, 시 00~23, 분 00~59)
bool isValidDueDate(const std::string& dueDate) {
    if (dueDate.empty()) {
        return true;
    }
    static const std::regex pattern("^(\\d{4})-(\\d{2})-(\\d{2})( (\\d{2}):(\\d{2}))?$");
    std::smatch m;
    if (!std::regex_match(dueDate, m, pattern)) {
        return false;
    }
    const int month = std::stoi(m[2]);
    const int day = std::stoi(m[3]);
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    if (m[4].matched) {
        const int hour = std::stoi(m[5]);
        const int minute = std::stoi(m[6]);
        if (hour > 23 || minute > 59) {
            return false;
        }
    }
    return true;
}

}  // namespace

const std::vector<std::string>& postCategories() {
    static const std::vector<std::string> categories = {"material", "midterm", "final", "assignment"};
    return categories;
}

bool CoursePost::isValid() const {
    const std::vector<std::string>& categories = postCategories();
    return isSixDigits(courseCode) && isValidSemester(semester) &&
           std::find(categories.begin(), categories.end(), category) != categories.end() &&
           week >= 1 && week <= 16 &&
           !isBlank(title) && !hasLineBreak(title) && title.size() <= 200 &&
           body.size() <= 5000 && isValidLink(link) && isValidDueDate(dueDate);
}
