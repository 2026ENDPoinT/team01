// 담당: B (현준)
#include "request_validation.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "account.h"

using json = nlohmann::json;

namespace {

// 공백(스페이스·탭·줄바꿈)만 있거나 비어 있으면 true
bool isBlank(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// 문자열 항목 읽기. 없거나 null 이면 required 일 때 missing 문구, 아니면 기본값 그대로 (out 을 건드리지 않음)
//   문자열이 아니면 wrongType 문구
std::string readString(const json& body, const char* key, bool required, const char* missing,
                       const char* wrongType, std::string& out) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) return required ? missing : "";
    if (!it->is_string()) return wrongType;
    out = it->get<std::string>();
    return "";
}

// 정수 항목 읽기. 숫자가 아니면 notNumber, 소수가 있으면 notInteger 문구
//   int 범위를 크게 벗어나는 값은 -1 로 읽어 값 검사(범위)에서 걸리게 함
std::string readInt(const json& body, const char* key, bool required, const char* missing,
                    const char* notNumber, const char* notInteger, int& out) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) return required ? missing : "";
    if (!it->is_number()) return notNumber;
    const double value = it->get<double>();
    if (!std::isfinite(value) || value != std::floor(value)) return notInteger;
    out = std::fabs(value) <= 1e9 ? static_cast<int>(value) : -1;
    return "";
}

}  // namespace

namespace RequestValidation {

std::string readCourse(const json& body, const std::string& defaultCode, Course& course) {
    if (!body.is_object()) return "요청 본문은 JSON 객체여야 합니다";
    Course c;
    c.code = defaultCode;
    std::string reason;
    if (!(reason = readString(body, "code", defaultCode.empty(), "과목코드는 숫자 6자리여야 합니다",
                              "과목코드는 숫자 6자리여야 합니다", c.code)).empty() ||
        !(reason = readString(body, "name", true, "과목명이 비어 있습니다", "과목명은 문자열이어야 합니다", c.name)).empty() ||
        !(reason = readInt(body, "credit", true, "학점이 없습니다", "학점은 숫자여야 합니다", "학점은 정수여야 합니다",
                           c.credit)).empty() ||
        !(reason = readString(body, "professor", true, "담당 교수 항목이 없습니다", "담당 교수는 문자열이어야 합니다",
                              c.professor)).empty() ||
        !(reason = readString(body, "category", false, "", "이수구분은 문자열이어야 합니다", c.category)).empty() ||
        !(reason = readString(body, "area", false, "", "교양영역은 문자열이어야 합니다", c.area)).empty() ||
        !(reason = readString(body, "schedule", false, "", "강의시간은 문자열이어야 합니다", c.schedule)).empty() ||
        !(reason = readString(body, "language", false, "", "강의언어는 문자열이어야 합니다", c.language)).empty() ||
        !(reason = readInt(body, "capacity", false, "", "정원은 숫자여야 합니다", "정원은 정수여야 합니다",
                           c.capacity)).empty() ||
        !(reason = readString(body, "gradeType", false, "", "성적 방식은 문자열이어야 합니다", c.gradeType)).empty()) {
        return reason;
    }
    const auto remote = body.find("remote");
    if (remote != body.end() && !remote->is_null()) {
        if (!remote->is_boolean()) return "원격강좌 여부는 true 또는 false 여야 합니다";
        c.remote = remote->get<bool>();
    }
    reason = courseError(c);
    if (reason.empty()) course = c;
    return reason;
}

std::string courseError(const Course& c) {
    if (!isValidCourseCode(c.code)) return "과목코드는 숫자 6자리여야 합니다";
    if (isBlank(c.name)) return "과목명이 비어 있습니다";
    if (c.credit < 1 || c.credit > 4) return "학점은 1~4 사이여야 합니다";
    if (!contains(courseCategories(), c.category)) return "이수구분이 올바르지 않습니다";
    if (!contains(courseLanguages(), c.language)) return "강의언어가 올바르지 않습니다";
    if (!isValidSchedule(c.schedule)) return "강의시간 형식이 올바르지 않습니다 (예: 월(1~2) 수(3))";
    if (c.capacity < 0 || c.capacity > 999) return "정원은 0~999 사이여야 합니다";
    if (!isValidGradeType(c.gradeType)) return "성적 방식은 letter(등급) 또는 pf(P/F) 여야 합니다";
    if (!c.isValid()) return "값을 확인해 주세요";
    return "";
}

std::string newCourseError(const Course& c) {
    if (isBlank(c.professor)) return "담당 교수가 비어 있습니다";
    if (isBlank(c.schedule)) return "강의시간이 비어 있습니다";
    return "";
}

std::string accountError(const std::string& username, bool checkUsername, const std::string& role,
                         const std::string& name, const std::string& studentId, const std::string& password,
                         bool passwordRequired) {
    if (checkUsername) {
        const bool ok = username.size() >= 3 && username.size() <= 20 &&
                        std::all_of(username.begin(), username.end(), [](unsigned char c) {
                            return (c < 0x80 && std::isalnum(c)) || c == '_';
                        });
        if (!ok) return "아이디는 영문·숫자·_ 3~20자여야 합니다";
    }
    if (!isValidRole(role)) return "역할은 admin·professor·student 중 하나여야 합니다";
    if ((passwordRequired || !password.empty()) && (password.size() < 8 || password.size() > 64)) {
        return "비밀번호는 8~64자여야 합니다";
    }
    if (role != "student" && isBlank(name)) return "교수·관리자 계정은 이름이 필요합니다";
    if (role == "student" && studentId.empty()) return "학생 계정은 학번이 필요합니다";
    return "";
}

}  // namespace RequestValidation
