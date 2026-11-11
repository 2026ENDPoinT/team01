// 담당: A (연준)
#include "course.h"

#include <algorithm>
#include <cctype>

namespace {

// 공백(스페이스·탭·줄바꿈)만 있거나 비어있으면 true
bool isBlank(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// text[pos] 부터 숫자를 읽어 out 에 담고 pos 를 옮김. 숫자가 없거나 너무 길면 false
bool readNumber(const std::string& text, size_t& pos, int& out) {
    const size_t start = pos;
    out = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        if (pos - start >= 2) return false;  // 교시는 최대 두 자리
        out = out * 10 + (text[pos] - '0');
        pos++;
    }
    return pos > start;
}

// 한 칸 검사: 요일 + "(n)" 또는 "(n~m)"
bool isValidSlot(const std::string& slot) {
    static const std::vector<std::string> days = {"월", "화", "수", "목", "금", "토", "일"};
    for (const std::string& day : days) {
        if (slot.compare(0, day.size(), day) != 0) continue;

        size_t pos = day.size();
        if (pos >= slot.size() || slot[pos] != '(') return false;
        pos++;
        int first = 0;
        int last = 0;
        if (!readNumber(slot, pos, first)) return false;
        last = first;
        if (pos < slot.size() && slot[pos] == '~') {
            pos++;
            if (!readNumber(slot, pos, last)) return false;
        }
        if (pos + 1 != slot.size() || slot[pos] != ')') return false;
        return first >= 1 && last <= 30 && first <= last;
    }
    return false;
}

}  // namespace

const std::vector<std::string>& courseCategories() {
    static const std::vector<std::string> list = {"전공필수", "전공선택", "교양필수", "교양선택", "일반선택"};
    return list;
}

const std::vector<std::string>& courseLanguages() {
    static const std::vector<std::string> list = {"한국어전용", "영어전용", "모듈강좌(M)"};
    return list;
}

bool isValidCourseCode(const std::string& code) {
    if (code.size() != 6) return false;
    for (unsigned char c : code) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

bool isValidSchedule(const std::string& schedule) {
    std::string slot;
    for (size_t i = 0; i <= schedule.size(); i++) {
        const bool separator = i == schedule.size() || schedule[i] == ' ' || schedule[i] == ',';
        if (!separator) {
            slot += schedule[i];
            continue;
        }
        if (!slot.empty() && !isValidSlot(slot)) return false;
        slot.clear();
    }
    return true;
}

bool Course::isValid() const {
    if (!isValidCourseCode(code)) return false;            // 과목 코드: 숫자 6자리
    if (isBlank(name)) return false;                       // 과목명: 비어있으면 안 됨
    if (credit < 1 || credit > 4) return false;            // 학점: 1~4
    if (!contains(courseCategories(), category)) return false;
    if (!contains(courseLanguages(), language)) return false;
    if (!isValidSchedule(schedule)) return false;
    if (capacity < 0 || capacity > 999) return false;      // 정원: 0(제한 없음)~999
    return true;                                           // 담당 교수·교양영역은 비어 있어도 허용
}
