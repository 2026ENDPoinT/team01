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

// 강의시간 한 칸: 요일 + 교시 구간
struct Slot {
    std::string day;
    int first = 0;
    int last = 0;
};

// 한 칸 읽기: 요일 + "(n)" 또는 "(n~m)". 형식이 틀리거나 범위(1~30, 시작 <= 끝)를 벗어나면 false
bool parseSlot(const std::string& text, Slot& slot) {
    static const std::vector<std::string> days = {"월", "화", "수", "목", "금", "토", "일"};
    for (const std::string& day : days) {
        if (text.compare(0, day.size(), day) != 0) continue;

        size_t pos = day.size();
        if (pos >= text.size() || text[pos] != '(') return false;
        pos++;
        int first = 0;
        int last = 0;
        if (!readNumber(text, pos, first)) return false;
        last = first;
        if (pos < text.size() && text[pos] == '~') {
            pos++;
            if (!readNumber(text, pos, last)) return false;
        }
        if (pos + 1 != text.size() || text[pos] != ')') return false;
        if (first < 1 || last > 30 || first > last) return false;
        slot.day = day;
        slot.first = first;
        slot.last = last;
        return true;
    }
    return false;
}

// 한 칸 검사: 요일 + "(n)" 또는 "(n~m)"
bool isValidSlot(const std::string& text) {
    Slot slot;
    return parseSlot(text, slot);
}

// 강의시간 문자열을 공백·쉼표로 나눈 칸들 (빈 칸은 뺌)
std::vector<std::string> splitSlots(const std::string& schedule) {
    std::vector<std::string> result;
    std::string slot;
    for (size_t i = 0; i <= schedule.size(); i++) {
        const bool separator = i == schedule.size() || schedule[i] == ' ' || schedule[i] == ',';
        if (!separator) {
            slot += schedule[i];
            continue;
        }
        if (!slot.empty()) result.push_back(slot);
        slot.clear();
    }
    return result;
}

// 올바른 칸만 꺼냄 (잘못된 칸은 무시)
std::vector<Slot> validSlots(const std::string& schedule) {
    std::vector<Slot> result;
    for (const std::string& text : splitSlots(schedule)) {
        Slot slot;
        if (parseSlot(text, slot)) result.push_back(slot);
    }
    return result;
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
    for (const std::string& text : splitSlots(schedule)) {
        if (!isValidSlot(text)) return false;
    }
    return true;
}

bool schedulesOverlap(const std::string& a, const std::string& b) {
    const std::vector<Slot> slotsB = validSlots(b);
    for (const Slot& s1 : validSlots(a)) {
        for (const Slot& s2 : slotsB) {
            if (s1.day == s2.day && s1.first <= s2.last && s2.first <= s1.last) return true;
        }
    }
    return false;
}

bool Course::isValid() const {
    if (!isValidCourseCode(code)) return false;            // 과목 코드: 숫자 6자리
    if (isBlank(name)) return false;                       // 과목명: 비어있으면 안 됨
    if (credit < 1 || credit > 4) return false;            // 학점: 1~4
    if (!contains(courseCategories(), category)) return false;
    if (!contains(courseLanguages(), language)) return false;
    if (!isValidSchedule(schedule)) return false;
    if (capacity < 0 || capacity > 999) return false;      // 정원: 0(제한 없음)~999
    if (!isValidGradeType(gradeType)) return false;        // 성적 방식: letter / pf
    return true;                                           // 담당 교수·교양영역은 비어 있어도 허용
}

bool isValidGradeType(const std::string& gradeType) {
    return gradeType == kGradeTypeLetter || gradeType == kGradeTypePassFail;
}

bool isPassFailLetter(const std::string& letter) {
    return letter == kLetterPass || letter == kLetterNonPass;
}
