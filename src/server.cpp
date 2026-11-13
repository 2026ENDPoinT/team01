#include "server.h"
#include "statistics.h"
#include "course_post.h"
#include "retake.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <map>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <unordered_set>

#include "httplib.h"
#include "json.hpp"
#include "report_exporter.h"

using json = nlohmann::json;

namespace {

constexpr const char* kGradeRules =
    "성적 정보가 올바르지 않습니다 (학번 숫자 8자리, 과목코드 숫자 6자리, "
    "학기 예: 2026-1 (1·2 정규학기, 3 여름·4 겨울 계절학기), 점수 0~100)";

constexpr const char* kSemesterRules =
    "학기 형식이 올바르지 않습니다 (예: 2026-1, 1·2 정규학기, 3 여름·4 겨울 계절학기)";

constexpr const char* kAccountRules =
    "계정 정보가 올바르지 않습니다 (아이디 영문·숫자·_ 3~20자, 비밀번호 8~64자, 교수·관리자는 이름 필수, 학생은 학번 필수)";

constexpr const char* kBadBody = "요청 본문이 올바르지 않습니다";
constexpr const char* kSaveFailed = "저장에 실패했습니다";
constexpr const char* kStudentRules =
    "학생 정보가 올바르지 않습니다 (학번 숫자 8자리, 이름 필수, 학년 1~4)";
constexpr const char* kScoreRules = "점수는 0~100 사이 숫자여야 합니다";
constexpr const char* kLetterRules = "지원하지 않는 성적 등급입니다 (A+ A0 B+ B0 C+ C0 D+ D0 F)";
constexpr const char* kAssessmentRules =
    "평가 항목이 올바르지 않습니다 (평가 항목 출석·중간고사·기말고사·과제·팀프로젝트·기타, 비율 0~100 정수)";
constexpr const char* kGradingSchemeRules =
    "평가 기준이 올바르지 않습니다 (분류별 비율 0~100 정수, 합계 100, 방식 absolute·relative)";
constexpr const char* kAttendanceCategory = "출석";
const std::vector<std::string> kAssessmentCategories = {
    "출석", "중간고사", "기말고사", "과제", "팀프로젝트", "기타"
};
constexpr const char* kAssessedCourse =
    "평가 항목으로 성적을 계산하는 과목입니다 (평가 항목 화면에서 점수를 입력하세요)";
constexpr const char* kNotEnrolled = "수강신청하지 않은 학생입니다 (학번·과목·학기를 확인하세요)";

void setError(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(json{{"error", message}}.dump(), "application/json; charset=utf-8");
}

void setJson(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

bool hasRole(const Account& account, const char* role) {
    return account.role == role;
}

std::string randomHex(std::size_t byteCount) {
    std::random_device rd;
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(byteCount * 2);
    for (std::size_t i = 0; i < byteCount; ++i) {
        unsigned int value = rd() & 0xffu;
        result.push_back(digits[(value >> 4) & 0x0f]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

int thisYear() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    return local ? local->tm_year + 1900 : 0;
}

bool validUsername(const std::string& username) {
    static const std::regex pattern("^[A-Za-z0-9_]{3,20}$");
    return std::regex_match(username, pattern);
}

bool validPassword(const std::string& password) {
    return password.size() >= 8 && password.size() <= 64;
}

bool nonWhitespace(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return !std::isspace(c);
    });
}

bool isAdmin(const Account& account) {
    return account.role == "admin";
}

bool isProfessorOf(const Account& account, const GradeManager& gm, const std::string& courseCode) {
    const Course* course = gm.findCourse(courseCode);
    return course && teaches(account, *course);
}

std::unordered_set<std::string> studentsForProfessor(const Account& account,
                                                     const GradeManager& gm) {
    std::unordered_set<std::string> ids;
    for (const Enrollment& enrollment : gm.getEnrollments()) {
        if (isProfessorOf(account, gm, enrollment.courseCode)) {
            ids.insert(enrollment.studentId);
        }
    }
    return ids;
}

const Grade* findGrade(const GradeManager& gm, const std::string& studentId,
                       const std::string& courseCode, const std::string& semester) {
    for (const Grade& grade : gm.getGrades()) {
        if (grade.studentId == studentId && grade.courseCode == courseCode &&
            grade.semester == semester) {
            return &grade;
        }
    }
    return nullptr;
}

std::string enrollmentKey(const std::string& studentId, const std::string& courseCode,
                          const std::string& semester) {
    return studentId + '\n' + courseCode + '\n' + semester;
}

// 성적 또는 항목 점수가 있는 (학번, 과목, 학기) 의 enrollmentKey 집합
//   여기 든 수강은 취소할 수 없고, 수강신청 응답의 hasGrade 가 true
std::unordered_set<std::string> gradedEnrollments(const GradeManager& gm) {
    std::unordered_set<std::string> keys;
    for (const Grade& grade : gm.getGrades()) {
        keys.insert(enrollmentKey(grade.studentId, grade.courseCode, grade.semester));
    }
    std::unordered_set<std::string> assessed;  // 항목이 있는 (과목, 학기)
    for (const Assessment& item : gm.getAllAssessments()) {
        if (!assessed.insert(item.courseCode + '\n' + item.semester).second) continue;
        for (const AssessmentScore& score : gm.getAssessmentScores(item.courseCode, item.semester)) {
            if (score.name == kAttendanceCategory) continue;
            keys.insert(enrollmentKey(score.studentId, score.courseCode, score.semester));
        }
    }
    return keys;
}

json studentJson(const Student& s) {
    return {{"id", s.id}, {"name", s.name}, {"major", s.major}, {"year", s.year}};
}

json courseJson(const Course& c) {
    return {
        {"code", c.code}, {"name", c.name}, {"credit", c.credit},
        {"professor", c.professor}, {"category", c.category}, {"area", c.area},
        {"schedule", c.schedule}, {"language", c.language},
        {"capacity", c.capacity}, {"remote", c.remote}
    };
}

// letter 는 재수강 규칙을 적용한 등급 (grades 에는 gm.getGrades() 를 넘김)
json gradeJson(const Grade& grade, const std::vector<Grade>& grades) {
    return {
        {"studentId", grade.studentId}, {"courseCode", grade.courseCode},
        {"semester", grade.semester}, {"score", grade.score},
        {"letter", Retake::officialLetter(grade, grades)}
    };
}

json assessmentJson(const Assessment& item) {
    return {
        {"courseCode", item.courseCode}, {"semester", item.semester},
        {"name", item.name}, {"weight", item.weight}
    };
}

json assessmentScoreJson(const AssessmentScore& score) {
    return {
        {"studentId", score.studentId}, {"courseCode", score.courseCode},
        {"semester", score.semester}, {"name", score.name}, {"score", score.score}
    };
}

int weightSum(const std::vector<Assessment>& items) {
    int sum = 0;
    for (const Assessment& item : items) sum += item.weight;
    return sum;
}

json gradingSchemeItemsJson(const GradeManager& gm, const std::string& courseCode,
                            const std::string& semester) {
    const std::vector<Assessment> current = gm.getAssessments(courseCode, semester);
    if (current.empty()) return json::array();
    json result = json::array();
    for (const std::string& category : kAssessmentCategories) {
        int weight = 0;
        for (const Assessment& item : current) {
            if (item.name == category) {
                weight = item.weight;
                break;
            }
        }
        result.push_back({{"courseCode", courseCode}, {"semester", semester},
                          {"name", category}, {"weight", weight}});
    }
    return result;
}

// 본문의 문자열 칸: 없거나 문자열이 아니면 400 "<칸> 값이 올바르지 않습니다" 를 채우고 false
bool readString(const json& body, const char* key, std::string& out, httplib::Response& res) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string()) {
        setError(res, 400, std::string(key) + " 값이 올바르지 않습니다");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

// 생략할 수 있는 문자열 칸: 없거나 null 이면 fallback, 문자열이 아니면 400 "<칸> 값이 올바르지 않습니다" 를 채우고 false
bool readOptString(const json& body, const char* key, const std::string& fallback,
                   std::string& out, httplib::Response& res) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        out = fallback;
        return true;
    }
    return readString(body, key, out, res);
}

// 평가 항목 본문: nameKey 칸의 이름과 weight(정수 1~100) 를 item 에 넣음. 형식이 틀리면 400 을 채우고 false
bool readAssessmentFields(const json& body, const char* nameKey, Assessment& item,
                          httplib::Response& res) {
    const auto name = body.find(nameKey);
    const auto weight = body.find("weight");
    if (name == body.end() || !name->is_string() ||
        weight == body.end() || !weight->is_number_integer()) {
        setError(res, 400, kAssessmentRules);
        return false;
    }
    const double value = weight->get<double>();  // 아주 큰 정수도 넘치지 않게 double 로 범위 확인
    if (value < 1.0 || value > 100.0) {
        setError(res, 400, kAssessmentRules);
        return false;
    }
    item.name = name->get<std::string>();
    item.weight = static_cast<int>(value);
    return true;
}

// 점수 값: 숫자이고 0~100 이면 out 에 넣고 true
bool readScore(const json& value, double& out) {
    if (!value.is_number()) return false;
    out = value.get<double>();
    return out >= 0.0 && out <= 100.0;
}

// 성적 본문: studentId·courseCode·semester 와 score 또는 letter (둘 다 있으면 score, null 은 없는 것으로 봄)
//   letter 는 letterToScore 로 점수로 바꿈. 문제가 있으면 400 을 채우고 false
bool readGradeBody(const json& body, Grade& grade, httplib::Response& res) {
    if (!readString(body, "studentId", grade.studentId, res) ||
        !readString(body, "courseCode", grade.courseCode, res) ||
        !readString(body, "semester", grade.semester, res)) {
        return false;
    }
    const auto score = body.find("score");
    const auto letter = body.find("letter");
    if (score != body.end() && !score->is_null()) {
        if (!readScore(*score, grade.score)) {
            setError(res, 400, kScoreRules);
            return false;
        }
    } else if (letter != body.end() && !letter->is_null()) {
        grade.score = letter->is_string() ? letterToScore(letter->get<std::string>()) : -1.0;
        if (grade.score < 0.0) {
            setError(res, 400, kLetterRules);
            return false;
        }
    } else {
        setError(res, 400, "점수(score) 또는 등급(letter)을 입력하세요");
        return false;
    }
    if (!isValidSemester(grade.semester)) {
        setError(res, 400, kSemesterRules);
        return false;
    }
    if (!grade.isValid()) {
        setError(res, 400, kGradeRules);
        return false;
    }
    return true;
}

// 평가 항목·항목 점수 라우트: 과목이 없으면 404, 담당 과목이 아닌 교수면 403 을 채우고 false
bool checkCourseAccess(const Account& account, const GradeManager& gm,
                       const std::string& courseCode, httplib::Response& res) {
    const Course* course = gm.findCourse(courseCode);
    if (!course) {
        setError(res, 404, "없는 과목입니다: " + courseCode);
        return false;
    }
    if (account.role == "professor" && !teaches(account, *course)) {
        setError(res, 403, "권한이 없습니다");
        return false;
    }
    return true;
}

std::string escapeCSV(const std::string& value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += "\"\"";
        else result += c;
    }
    result += "\"";
    return result;
}

bool parseJsonBody(const httplib::Request& req, json& body, httplib::Response& res) {
    try {
        body = json::parse(req.body);
        if (!body.is_object()) {
            setError(res, 400, kBadBody);
            return false;
        }
        return true;
    } catch (...) {
        setError(res, 400, kBadBody);
        return false;
    }
}

// 과목 값 검사: 문제가 있으면 사유 문구, 없으면 빈 문자열
std::string courseInvalidReason(const Course& c) {
    auto has = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    if (!isValidCourseCode(c.code)) return "과목코드는 숫자 6자리여야 합니다";
    if (std::all_of(c.name.begin(), c.name.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }))
        return "과목명이 비어 있습니다";
    if (c.credit < 1 || c.credit > 4) return "학점은 1~4 사이여야 합니다";
    if (!has(courseCategories(), c.category)) return "이수구분이 올바르지 않습니다";
    if (!has(courseLanguages(), c.language)) return "강의언어가 올바르지 않습니다";
    if (!isValidSchedule(c.schedule)) return "강의시간 형식이 올바르지 않습니다 (예: 월(1~2) 수(3))";
    if (c.capacity < 0 || c.capacity > 999) return "정원은 0~999 사이여야 합니다";
    if (!c.isValid()) return "값을 확인해 주세요";
    return "";
}

constexpr const char* kPostRules =
    "게시물 정보가 올바르지 않습니다 (분류, 학기, 주차 1~16, 제목 필수, "
    "링크는 http:// 또는 https://, 날짜 예: 2026-10-20 또는 2026-10-20 13:00)";

json postJson(const CoursePost& p) {
    return {
        {"id", p.id}, {"courseCode", p.courseCode}, {"semester", p.semester},
        {"category", p.category}, {"week", p.week}, {"title", p.title},
        {"body", p.body}, {"link", p.link}, {"dueDate", p.dueDate},
        {"author", p.author}, {"createdAt", p.createdAt},
    };
}

// 지금 시각 "YYYY-MM-DD HH:MM" (mtx 안에서만 호출)
std::string nowText() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) return "";
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", local);
    return buffer;
}

// 요청 본문에서 semester·category·week·title·body·link·dueDate 를 읽음 (형식이 틀리면 false)
bool readPostBody(const json& body, CoursePost& post) {
    try {
        post.semester = body.at("semester").get<std::string>();
        post.category = body.at("category").get<std::string>();
        post.title = body.at("title").get<std::string>();
        if (!body.at("week").is_number_integer()) return false;
        post.week = body.at("week").get<int>();
        post.body = body.value("body", "");
        post.link = body.value("link", "");
        post.dueDate = body.value("dueDate", "");
        return true;
    } catch (...) {
        return false;
    }
}

// 경로의 게시물 id: 숫자만 있으면 그 값, 아니면 0
int parsePostId(const std::string& text) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return std::isdigit(c);
        })) {
        return 0;
    }
    try {
        std::size_t used = 0;
        const int id = std::stoi(text, &used);
        return used == text.size() ? id : 0;
    } catch (...) {
        return 0;
    }
}

}  // namespace

Server::Server(GradeManager& gm, AccountStore& accounts,
               const std::string& webDir, int port)
    : gm(gm), webDir(webDir), port(port), accounts(&accounts) {}

std::string Server::sessionToken(const httplib::Request& req) const {
    if (!req.has_header("Cookie")) return "";

    const std::string cookie = req.get_header_value("Cookie");
    std::istringstream stream(cookie);
    std::string item;
    while (std::getline(stream, item, ';')) {
        const auto first = item.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        item.erase(0, first);
        const auto equal = item.find('=');
        if (equal == std::string::npos) continue;
        if (item.substr(0, equal) == "sid") return item.substr(equal + 1);
    }
    return "";
}

const Account* Server::currentAccount(const httplib::Request& req) {
    const std::string token = sessionToken(req);
    if (token.empty()) return nullptr;

    auto session = sessions.find(token);
    if (session == sessions.end()) return nullptr;

    if (std::chrono::steady_clock::now() >= session->second.expires) {
        sessions.erase(session);
        return nullptr;
    }

    const Account* account = accounts ? accounts->find(session->second.username) : nullptr;
    if (!account) {
        sessions.erase(session);
        return nullptr;
    }
    return account;
}

const Account* Server::requireRole(const httplib::Request& req, httplib::Response& res,
                                   std::initializer_list<const char*> roles) {
    const Account* account = currentAccount(req);
    if (!account) {
        setError(res, 401, "로그인이 필요합니다");
        return nullptr;
    }

    for (const char* role : roles) {
        if (account->role == role) return account;
    }
    setError(res, 403, "권한이 없습니다");
    return nullptr;
}

std::string Server::accountJson(const Account& account) const {
    std::string name = account.name;
    if (account.role == "student") {
        const Student* student = gm.findStudent(account.studentId);
        name = student ? student->name : "";
    }
    return json{
        {"username", account.username},
        {"role", account.role},
        {"name", name},
        {"studentId", account.studentId}
    }.dump();
}

void Server::run() {
    httplib::Server svr;
    svr.set_mount_point("/", webDir);

    // 로그인: 유일하게 공개된 API
    svr.Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        json body;
        if (!parseJsonBody(req, body, res)) return;
        std::string username, password;
        if (!readOptString(body, "username", "", username, res) ||
            !readOptString(body, "password", "", password, res)) return;
        if (!accounts || !accounts->verify(username, password)) {
            setError(res, 401, "아이디 또는 비밀번호가 올바르지 않습니다");
            return;
        }

        const Account* account = accounts->find(username);
        if (!account) {
            setError(res, 401, "아이디 또는 비밀번호가 올바르지 않습니다");
            return;
        }

        const std::string token = randomHex(32);
        sessions[token] = Session{
            username, std::chrono::steady_clock::now() + std::chrono::hours(8)
        };
        res.set_header("Set-Cookie",
                       "sid=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800");
        res.set_content(accountJson(*account), "application/json; charset=utf-8");
    });

    svr.Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        const std::string token = sessionToken(req);
        sessions.erase(token);
        res.set_header("Set-Cookie", "sid=; Path=/; Max-Age=0");
        res.set_content(json{{"ok", true}}.dump(), "application/json; charset=utf-8");
    });

    svr.Get("/api/me", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;
        res.set_content(accountJson(*account), "application/json; charset=utf-8");
    });

    svr.Put("/api/me/password", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;

        json body;
        if (!parseJsonBody(req, body, res)) return;
        std::string currentPassword, newPassword;
        if (!readOptString(body, "currentPassword", "", currentPassword, res) ||
            !readOptString(body, "newPassword", "", newPassword, res)) return;
        if (!accounts->verify(account->username, currentPassword)) {
            setError(res, 400, "현재 비밀번호가 올바르지 않습니다");
            return;
        }
        if (!validPassword(newPassword)) {
            setError(res, 400, "비밀번호는 8~64자여야 합니다");
            return;
        }
        if (!accounts->changePassword(account->username, newPassword) || !accounts->save()) {
            setError(res, 500, "계정 저장에 실패했습니다");
            return;
        }
        res.set_content(json{{"ok", true}}.dump(), "application/json; charset=utf-8");
    });

    // 계정 관리
    svr.Get("/api/accounts", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json arr = json::array();
        for (const Account& account : accounts->getAccounts()) {
            arr.push_back(json::parse(accountJson(account)));
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    svr.Post("/api/accounts", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        Account account;
        std::string password;
        if (!readOptString(body, "username", "", account.username, res) ||
            !readOptString(body, "role", "", account.role, res) ||
            !readOptString(body, "name", "", account.name, res) ||
            !readOptString(body, "studentId", "", account.studentId, res) ||
            !readOptString(body, "password", "", password, res)) return;

        if (!validUsername(account.username) || !isValidRole(account.role) ||
            !validPassword(password) ||
            (account.role != "student" && !nonWhitespace(account.name)) ||
            (account.role == "student" && account.studentId.empty())) {
            setError(res, 400, kAccountRules);
            return;
        }
        if (accounts->find(account.username)) {
            setError(res, 409, "이미 있는 아이디입니다: " + account.username);
            return;
        }
        if (account.role == "student") {
            if (!gm.findStudent(account.studentId)) {
                setError(res, 404, "없는 학생입니다: " + account.studentId);
                return;
            }
            if (accounts->findByStudentId(account.studentId)) {
                setError(res, 409, "이미 계정이 있는 학생입니다: " + account.studentId);
                return;
            }
            account.name.clear();
        }

        if (!accounts->add(account, password)) {
            setError(res, 400, kAccountRules);
            return;
        }
        if (!accounts->save()) {
            setError(res, 500, "계정 저장에 실패했습니다");
            return;
        }
        res.status = 201;
        res.set_content(accountJson(*accounts->find(account.username)),
                        "application/json; charset=utf-8");
    });

    svr.Put("/api/accounts/:username", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        const std::string username = req.path_params.at("username");
        const Account* oldAccount = accounts->find(username);
        if (!oldAccount) {
            setError(res, 404, "계정을 찾을 수 없습니다");
            return;
        }

        json body;
        if (!parseJsonBody(req, body, res)) return;
        std::string role, name, studentId, password;
        if (!readOptString(body, "role", oldAccount->role, role, res) ||
            !readOptString(body, "name", oldAccount->name, name, res) ||
            !readOptString(body, "studentId", oldAccount->studentId, studentId, res) ||
            !readOptString(body, "password", "", password, res)) return;

        int adminCount = 0;
        for (const Account& a : accounts->getAccounts()) if (a.role == "admin") ++adminCount;
        if (oldAccount->role == "admin" && role != "admin" && adminCount <= 1) {
            setError(res, 400, "마지막 관리자 계정은 삭제하거나 바꿀 수 없습니다");
            return;
        }
        if (!isValidRole(role) ||
            (role != "student" && !nonWhitespace(name)) ||
            (role == "student" && studentId.empty()) ||
            (!password.empty() && !validPassword(password))) {
            setError(res, 400, kAccountRules);
            return;
        }
        if (role == "student") {
            if (!gm.findStudent(studentId)) {
                setError(res, 404, "없는 학생입니다: " + studentId);
                return;
            }
            const Account* linked = accounts->findByStudentId(studentId);
            if (linked && linked->username != username) {
                setError(res, 409, "이미 계정이 있는 학생입니다: " + studentId);
                return;
            }
            name.clear();
        } else {
            studentId.clear();
        }

        if (!accounts->update(username, role, name, studentId, password)) {
            setError(res, 400, kAccountRules);
            return;
        }
        if (!accounts->save()) {
            setError(res, 500, "계정 저장에 실패했습니다");
            return;
        }

        // 관리자 본인의 계정을 수정한 경우에는 현재 세션을 유지한다.
        const std::string callerToken = sessionToken(req);
        const std::string callerName = [&]() {
            auto it = sessions.find(callerToken);
            return it == sessions.end() ? std::string() : it->second.username;
        }();
        if (callerName != username) {
            for (auto it = sessions.begin(); it != sessions.end();) {
                if (it->second.username == username) it = sessions.erase(it);
                else ++it;
            }
        }
        const Account* updated = accounts->find(username);
        res.set_content(accountJson(*updated), "application/json; charset=utf-8");
    });

    svr.Delete("/api/accounts/:username", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        const std::string username = req.path_params.at("username");
        const std::string callerToken = sessionToken(req);
        auto caller = sessions.find(callerToken);
        if (caller != sessions.end() && caller->second.username == username) {
            setError(res, 400, "자기 계정은 삭제할 수 없습니다");
            return;
        }
        const Account* target = accounts->find(username);
        if (!target) {
            setError(res, 404, "계정을 찾을 수 없습니다");
            return;
        }
        if (target->role == "admin") {
            int adminCount = 0;
            for (const Account& a : accounts->getAccounts()) if (a.role == "admin") ++adminCount;
            if (adminCount <= 1) {
                setError(res, 400, "마지막 관리자 계정은 삭제하거나 바꿀 수 없습니다");
                return;
            }
        }
        if (!accounts->remove(username)) {
            setError(res, 404, "계정을 찾을 수 없습니다");
            return;
        }
        for (auto it = sessions.begin(); it != sessions.end();) {
            if (it->second.username == username) it = sessions.erase(it);
            else ++it;
        }
        if (!accounts->save()) {
            setError(res, 500, "계정 저장에 실패했습니다");
            return;
        }
        res.set_content(json{{"deleted", true}}.dump(), "application/json; charset=utf-8");
    });

    // 학생 목록 / 검색
    svr.Get("/api/students/search", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;
        const std::string query = req.get_param_value("q");
        if (query.empty()) {
            setError(res, 400, "검색 키워드(학번 or 이름)를 입력해주세요");
            return;
        }

        auto students = gm.searchStudents(query);
        const auto allowed = account->role == "professor"
            ? studentsForProfessor(*account, gm) : std::unordered_set<std::string>();
        json arr = json::array();
        for (const Student& student : students) {
            bool include = account->role == "admin" ||
                (account->role == "student" && student.id == account->studentId) ||
                (account->role == "professor" && allowed.count(student.id));
            if (include) arr.push_back(studentJson(student));
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    svr.Get("/api/students", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;
        const auto allowed = account->role == "professor"
            ? studentsForProfessor(*account, gm) : std::unordered_set<std::string>();
        json arr = json::array();
        for (const Student& student : gm.getStudents()) {
            bool include = account->role == "admin" ||
                (account->role == "student" && student.id == account->studentId) ||
                (account->role == "professor" && allowed.count(student.id));
            if (include) arr.push_back(studentJson(student));
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    // 과목 목록
    svr.Get("/api/courses", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        json arr = json::array();
        for (const Course& c : gm.getCourses()) arr.push_back(courseJson(c));
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    // 성적 목록
    svr.Get("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;
        const std::string requestedStudent = req.get_param_value("studentId");
        const std::string requestedCourse = req.get_param_value("courseCode");
        const std::string requestedSemester = req.get_param_value("semester");
        const auto grades = gm.getGrades();
        json arr = json::array();

        for (const Grade& grade : grades) {
            if (!requestedStudent.empty() && grade.studentId != requestedStudent) continue;
            if (!requestedCourse.empty() && grade.courseCode != requestedCourse) continue;
            if (!requestedSemester.empty() && grade.semester != requestedSemester) continue;
            if (account->role == "student" && grade.studentId != account->studentId) continue;
            if (account->role == "professor" &&
                !isProfessorOf(*account, gm, grade.courseCode)) continue;
            arr.push_back(gradeJson(grade, grades));
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    // GPA
    svr.Get("/api/stats/gpa/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "student"});
        if (!account) return;
        const std::string id = req.path_params.at("id");
        if (account->role == "student" && id != account->studentId) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (!gm.findStudent(id)) {
            setError(res, 404, "학생을 찾을 수 없습니다");
            return;
        }
        // 평점 계산에 들어간 학점이 0 이면 gpa 는 null (?semester= 가 있으면 그 학기 기준)
        const std::string semester = req.get_param_value("semester");
        const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, id);
        int credits = semester.empty() ? summary.credits : 0;
        for (const Statistics::SemesterSummary& s : summary.semesters) {
            if (!semester.empty() && s.semester == semester) credits = s.credits;
        }
        const json gpa = credits > 0 ? json(Statistics::calcGPA(gm, id, semester)) : json(nullptr);
        setJson(res, 200, json{{"studentId", id}, {"semester", semester}, {"gpa", gpa}});
    });

    // 학기별·누적 평점과 학점 (누적 학점이 0 이면 gpa 는 null)
    svr.Get("/api/stats/summary/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "student"});
        if (!account) return;
        const std::string id = req.path_params.at("id");
        if (account->role == "student" && id != account->studentId) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (!gm.findStudent(id)) {
            setError(res, 404, "없는 학생입니다: " + id);
            return;
        }
        const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, id);
        json semesters = json::array();
        for (const Statistics::SemesterSummary& s : summary.semesters) {
            semesters.push_back({{"semester", s.semester}, {"gpa", s.gpa}, {"credits", s.credits}});
        }
        setJson(res, 200, json{
            {"semesters", semesters},
            {"gpa", summary.credits > 0 ? json(summary.gpa) : json(nullptr)},
            {"credits", summary.credits}
        });
    });

    // 과목 통계
    svr.Get("/api/stats/course/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        const auto stats = Statistics::courseStats(gm, req.path_params.at("code"));
        json result = {
            {"courseCode", stats.courseCode}, {"count", stats.count},
            {"average", stats.average}, {"max", stats.max}, {"min", stats.min},
            {"latestSemester", stats.latestSemester}, {"latestEnrolled", stats.latestEnrolled}
        };
        res.set_content(result.dump(), "application/json; charset=utf-8");
    });

    // 고정 경로를 :id 경로보다 먼저 등록
    svr.Get("/api/stats/warnings", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json arr = json::array();
        for (const Student& student : gm.getStudents()) {
            for (const auto& warning : Statistics::academicWarnings(gm, student.id)) {
                arr.push_back({
                    {"studentId", student.id}, {"name", student.name},
                    {"semester", warning.semester}, {"reason", warning.reason},
                    {"gpa", warning.hasGpa ? json(warning.gpa) : json(nullptr)},
                    {"credits", warning.credits}
                });
            }
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    svr.Get("/api/stats/warnings/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "student"});
        if (!account) return;
        const std::string id = req.path_params.at("id");
        if (account->role == "student" && id != account->studentId) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (!gm.findStudent(id)) {
            setError(res, 404, "학생을 찾을 수 없습니다");
            return;
        }
        json arr = json::array();
        for (const auto& warning : Statistics::academicWarnings(gm, id)) {
            arr.push_back({
                {"semester", warning.semester}, {"reason", warning.reason},
                {"gpa", warning.hasGpa ? json(warning.gpa) : json(nullptr)},
                {"credits", warning.credits}
            });
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    // 석차
    svr.Get("/api/stats/ranking", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json arr = json::array();
        for (const auto& entry : Statistics::ranking(gm, req.get_param_value("semester"))) {
            arr.push_back({
                {"rank", entry.rank}, {"studentId", entry.studentId},
                {"name", entry.name}, {"gpa", entry.gpa}
            });
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    // 학생 성적표 CSV
    svr.Get("/api/export/student/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "student"});
        if (!account) return;
        const std::string id = req.path_params.at("id");
        if (account->role == "student" && id != account->studentId) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        const Student* student = gm.findStudent(id);
        if (!student) {
            setError(res, 404, "해당 학번의 학생을 찾지 못했습니다");
            return;
        }

        std::string csv = "\xEF\xBB\xBF";
        csv += "학생ID,이름,과목코드,학기,점수,등급\r\n";
        const auto grades = gm.getGrades();
        for (const auto& grade : grades) {
            if (grade.studentId != id) continue;
            const std::string letter = Retake::officialLetter(grade, grades);
            if (letter == "R" && !Retake::admittedSince2018(id)) continue;
            csv += escapeCSV(id) + "," + escapeCSV(student->name) + "," +
                   escapeCSV(grade.courseCode) + "," + escapeCSV(grade.semester) + "," +
                   std::to_string(grade.score) + "," + escapeCSV(letter) + "\r\n";
        }
        res.set_header("Content-Disposition", "attachment; filename=\"student_grades.csv\"");
        res.set_content(csv, "text/csv; charset=utf-8");
    });

    // 과목 성적표 CSV
    svr.Get("/api/export/course/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string code = req.path_params.at("code");
        if (account->role == "professor" && !isProfessorOf(*account, gm, code)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (!gm.findCourse(code)) {
            setError(res, 404, "과목을 찾을 수 없습니다");
            return;
        }
        std::string csv = "\xEF\xBB\xBF";
        csv += "학생ID,과목코드,학기,점수,등급\r\n";
        const auto grades = gm.getGrades();
        const auto activeGrades = Retake::activeGrades(grades);
        for (const auto& grade : activeGrades) {
            if (grade.courseCode != code) continue;
            csv += escapeCSV(grade.studentId) + "," + escapeCSV(grade.courseCode) + "," +
                   escapeCSV(grade.semester) + "," + std::to_string(grade.score) + "," +
                   escapeCSV(Retake::officialLetter(grade, grades)) + "\r\n";
        }
        res.set_header("Content-Disposition", "attachment; filename=\"course_grades.csv\"");
        res.set_content(csv, "text/csv; charset=utf-8");
    });

    // 수강신청 학기 설정 조회 / 변경
    svr.Get("/api/enrollment/settings", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        res.set_content(json{{"semester", gm.getEnrollmentSemester()}}.dump(),
                        "application/json; charset=utf-8");
    });

    svr.Put("/api/enrollment/settings", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;
        std::string semester;
        if (!readOptString(body, "semester", "", semester, res)) return;
        if (!semester.empty() && !isValidSemester(semester)) {
            setError(res, 400, std::string(kSemesterRules) + ", 비우면 신청 마감");
            return;
        }
        if (!gm.setEnrollmentSemester(semester) || !gm.save()) {
            setError(res, 500, "수강신청 학기 저장에 실패했습니다");
            return;
        }
        res.set_content(json{{"semester", gm.getEnrollmentSemester()}}.dump(),
                        "application/json; charset=utf-8");
    });

    // 고정 경로를 /api/enrollments보다 먼저 등록
    svr.Get("/api/enrollments/counts", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        const std::string semester = req.get_param_value("semester");
        if (semester.empty()) {
            setError(res, 400, "semester 를 지정해야 합니다");
            return;
        }
        json arr = json::array();
        for (const Course& course : gm.getCourses()) {
            int count = 0;
            for (const Enrollment& enrollment : gm.getEnrollments()) {
                if (enrollment.courseCode == course.code && enrollment.semester == semester) ++count;
            }
            arr.push_back({{"courseCode", course.code}, {"count", count}});
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    svr.Get("/api/enrollments", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;
        const std::string requestedStudent = req.get_param_value("studentId");
        const std::string requestedCourse = req.get_param_value("courseCode");
        const std::string requestedSemester = req.get_param_value("semester");
        const auto graded = gradedEnrollments(gm);
        json arr = json::array();
        for (const Enrollment& enrollment : gm.getEnrollments()) {
            if (!requestedStudent.empty() && enrollment.studentId != requestedStudent) continue;
            if (!requestedCourse.empty() && enrollment.courseCode != requestedCourse) continue;
            if (!requestedSemester.empty() && enrollment.semester != requestedSemester) continue;
            if (account->role == "student" && enrollment.studentId != account->studentId) continue;
            if (account->role == "professor" &&
                !isProfessorOf(*account, gm, enrollment.courseCode)) continue;
            arr.push_back({
                {"studentId", enrollment.studentId}, {"courseCode", enrollment.courseCode},
                {"semester", enrollment.semester},
                {"hasGrade", graded.count(enrollmentKey(enrollment.studentId, enrollment.courseCode,
                                                        enrollment.semester)) > 0}
            });
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    svr.Post("/api/enrollments", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "student"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        Enrollment enrollment;
        if (!readOptString(body, "studentId", "", enrollment.studentId, res) ||
            !readOptString(body, "courseCode", "", enrollment.courseCode, res) ||
            !readOptString(body, "semester", "", enrollment.semester, res)) return;

        if (account->role == "student" && enrollment.studentId != account->studentId) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (account->role == "student") {
            const std::string openSemester = gm.getEnrollmentSemester();
            if (openSemester.empty()) {
                setError(res, 409, "수강신청 기간이 아닙니다");
                return;
            }
            if (enrollment.semester != openSemester) {
                setError(res, 400, "수강신청 학기는 " + openSemester + " 입니다");
                return;
            }
        }
        if (!isValidSemester(enrollment.semester)) {
            setError(res, 400, kSemesterRules);
            return;
        }
        const Student* student = gm.findStudent(enrollment.studentId);
        if (!student) {
            setError(res, 404, "없는 학생입니다: " + enrollment.studentId);
            return;
        }
        const Course* course = gm.findCourse(enrollment.courseCode);
        if (!course) {
            setError(res, 404, "없는 과목입니다: " + enrollment.courseCode);
            return;
        }
        const int year = thisYear();
        if (!isSemesterInRange(enrollment.studentId, enrollment.semester, year)) {
            const int admission = admissionYear(enrollment.studentId);
            setError(res, 400, enrollment.semester + " 학기는 등록할 수 없습니다 (이 학생은 " +
                std::to_string(admission) + "-1 ~ " + std::to_string(year + 1) +
                "-4 학기만 가능)");
            return;
        }
        if (gm.findEnrollment(enrollment.studentId, enrollment.courseCode, enrollment.semester)) {
            setError(res, 409, "이미 신청한 과목입니다");
            return;
        }

        if (course->capacity > 0) {
            int count = 0;
            for (const Enrollment& existing : gm.getEnrollments()) {
                if (existing.courseCode == enrollment.courseCode &&
                    existing.semester == enrollment.semester) ++count;
            }
            if (count >= course->capacity) {
                setError(res, 409, enrollment.semester + " " + course->name +
                    " 정원(" + std::to_string(course->capacity) + "명)이 찼습니다");
                return;
            }
        }

        for (const Enrollment& existing : gm.getEnrollments()) {
            if (existing.studentId != enrollment.studentId ||
                existing.semester != enrollment.semester) continue;
            const Course* other = gm.findCourse(existing.courseCode);
            if (other && schedulesOverlap(course->schedule, other->schedule)) {
                setError(res, 409, other->name + " 과목과 강의시간이 겹칩니다");
                return;
            }
        }

        if (!gm.addEnrollment(enrollment) || !gm.save()) {
            setError(res, 500, "수강신청 저장에 실패했습니다");
            return;
        }
        res.status = 201;
        res.set_content(json{
            {"studentId", enrollment.studentId}, {"courseCode", enrollment.courseCode},
            {"semester", enrollment.semester},
            {"hasGrade", gradedEnrollments(gm).count(enrollmentKey(
                enrollment.studentId, enrollment.courseCode, enrollment.semester)) > 0}
        }.dump(), "application/json; charset=utf-8");
    });

    svr.Delete("/api/enrollments", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "student"});
        if (!account) return;
        const std::string studentId = req.get_param_value("studentId");
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        if (studentId.empty() || courseCode.empty() || semester.empty()) {
            setError(res, 400, "studentId, courseCode, semester 를 모두 지정해야 합니다");
            return;
        }
        if (account->role == "student") {
            if (studentId != account->studentId) {
                setError(res, 403, "권한이 없습니다");
                return;
            }
            const std::string openSemester = gm.getEnrollmentSemester();
            if (openSemester.empty()) {
                setError(res, 409, "수강신청 기간이 아닙니다");
                return;
            }
            if (semester != openSemester) {
                setError(res, 400, "수강신청 학기는 " + openSemester + " 입니다");
                return;
            }
        }
        if (!gm.findEnrollment(studentId, courseCode, semester)) {
            setError(res, 404, "수강신청 기록이 없습니다");
            return;
        }
        if (gradedEnrollments(gm).count(enrollmentKey(studentId, courseCode, semester))) {
            setError(res, 409, "성적이 있는 수강은 취소할 수 없습니다");
            return;
        }
        if (!gm.removeEnrollment(studentId, courseCode, semester) || !gm.save()) {
            setError(res, 500, "수강신청 취소 저장에 실패했습니다");
            return;
        }
        res.set_content(json{{"deleted", true}}.dump(), "application/json; charset=utf-8");
    });

    // 학생 등록 / 수정 / 삭제
    svr.Post("/api/students", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;
        Student student;
        try {
            student.id = body.at("id").get<std::string>();
            student.name = body.at("name").get<std::string>();
            student.major = body.at("major").get<std::string>();
            student.year = body.at("year").get<decltype(student.year)>();
        } catch (...) {
            setError(res, 400, kStudentRules);
            return;
        }
        if (!student.isValid()) {
            setError(res, 400, kStudentRules);
            return;
        }
        if (!gm.addStudent(student)) {
            setError(res, 409, "학생 등록 실패 (이미 등록된 학번입니다)");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "학생 정보 저장에 실패했습니다");
            return;
        }
        setJson(res, 201, studentJson(*gm.findStudent(student.id)));
    });

    svr.Put("/api/students/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;
        const std::string oldId = req.path_params.at("id");
        Student student;
        try {
            student.id = body.value("id", oldId);
            student.name = body.at("name").get<std::string>();
            student.major = body.at("major").get<std::string>();
            student.year = body.at("year").get<decltype(student.year)>();
        } catch (...) {
            setError(res, 400, kStudentRules);
            return;
        }
        if (!gm.findStudent(oldId)) {
            setError(res, 404, "수정할 학생을 찾을 수 없습니다.");
            return;
        }
        if (!student.isValid()) {
            setError(res, 400, kStudentRules);
            return;
        }
        if (student.id != oldId && gm.findStudent(student.id)) {
            setError(res, 409, "이미 등록된 학번입니다: " + student.id);
            return;
        }
        if (!gm.updateStudent(oldId, student)) {
            setError(res, 400, kStudentRules);
            return;
        }
        if (oldId != student.id) {
            accounts->relinkStudent(oldId, student.id);
            if (!accounts->save()) {
                setError(res, 500, "계정 저장에 실패했습니다");
                return;
            }
        }
        if (!gm.save()) {
            setError(res, 500, "학생 정보 저장에 실패했습니다");
            return;
        }
        setJson(res, 200, studentJson(*gm.findStudent(student.id)));
    });

    svr.Delete("/api/students/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        const std::string id = req.path_params.at("id");
        if (!gm.removeStudent(id)) {
            setError(res, 404, "삭제할 학생을 찾을 수 없습니다.");
            return;
        }
        const std::vector<std::string> removedUsernames = accounts->removeStudentAccounts(id);
        for (auto it = sessions.begin(); it != sessions.end();) {
            if (std::find(removedUsernames.begin(), removedUsernames.end(),
                          it->second.username) != removedUsernames.end()) {
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
        if (!accounts->save() || !gm.save()) {
            setError(res, 500, "학생 정보 저장에 실패했습니다");
            return;
        }
        setJson(res, 200, json{{"deleted", true}});
    });

    // 과목 등록 / 수정 / 삭제
    svr.Post("/api/courses", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;
        Course course;
        try {
            course.code = body.at("code").get<std::string>();
            course.name = body.at("name").get<std::string>();
            course.credit = body.at("credit").get<decltype(course.credit)>();
            course.professor = body.at("professor").get<std::string>();
            course.category = body.value("category", "전공선택");
            course.area = body.value("area", "");
            course.schedule = body.value("schedule", "");
            course.language = body.value("language", "한국어전용");
            course.capacity = body.value("capacity", 0);
            course.remote = body.value("remote", false);
        } catch (...) {
            setError(res, 400, "과목 정보가 올바르지 않습니다");
            return;
        }
        const std::string reason = courseInvalidReason(course);
        if (!reason.empty()) {
            setError(res, 400, "과목 정보가 올바르지 않습니다 (" + reason + ")");
            return;
        }
        if (!gm.addCourse(course)) {
            setError(res, 409, "이미 등록된 과목입니다 (과목코드 " + course.code + ")");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "과목 정보 저장에 실패했습니다");
            return;
        }
        setJson(res, 201, courseJson(*gm.findCourse(course.code)));
    });

    svr.Put("/api/courses/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;
        Course course;
        try {
            course.code = body.value("code", req.path_params.at("code"));
            course.name = body.at("name").get<std::string>();
            course.credit = body.at("credit").get<decltype(course.credit)>();
            course.professor = body.at("professor").get<std::string>();
            course.category = body.value("category", "전공선택");
            course.area = body.value("area", "");
            course.schedule = body.value("schedule", "");
            course.language = body.value("language", "한국어전용");
            course.capacity = body.value("capacity", 0);
            course.remote = body.value("remote", false);
        } catch (...) {
            setError(res, 400, "과목 정보가 올바르지 않습니다");
            return;
        }
        const std::string reason = courseInvalidReason(course);
        if (!reason.empty()) {
            setError(res, 400, "과목 정보가 올바르지 않습니다 (" + reason + ")");
            return;
        }
        const std::string oldCode = req.path_params.at("code");
        if (gm.findCourse(oldCode) == nullptr) {
            setError(res, 404, "수정할 과목을 찾을 수 없습니다.");
            return;
        }
        if (course.code != oldCode && gm.findCourse(course.code) != nullptr) {
            setError(res, 409, "이미 등록된 과목입니다 (과목코드 " + course.code + ")");
            return;
        }
        if (!gm.updateCourse(oldCode, course)) {
            setError(res, 400, "과목 정보를 수정할 수 없습니다");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "과목 정보 저장에 실패했습니다");
            return;
        }
        setJson(res, 200, courseJson(*gm.findCourse(course.code)));
    });

    svr.Delete("/api/courses/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;
        if (!gm.removeCourse(req.path_params.at("code"))) {
            setError(res, 404, "삭제할 과목을 찾을 수 없습니다.");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "과목 정보 저장에 실패했습니다");
            return;
        }
        setJson(res, 200, json{{"deleted", true}});
    });

    // 과목 게시물 (수업자료·중간고사·기말고사·과제)
    svr.Get("/api/courses/:code/posts", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;
        const std::string code = req.path_params.at("code");
        const Course* course = gm.findCourse(code);
        if (!course) {
            setError(res, 404, "없는 과목입니다: " + code);
            return;
        }

        // 학생은 자기가 수강신청한 학기의 게시물만 본다
        std::unordered_set<std::string> semesters;
        if (account->role == "professor" && !teaches(*account, *course)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (account->role == "student") {
            for (const Enrollment& enrollment : gm.getEnrollments()) {
                if (enrollment.studentId == account->studentId && enrollment.courseCode == code) {
                    semesters.insert(enrollment.semester);
                }
            }
            if (semesters.empty()) {
                setError(res, 403, "권한이 없습니다");
                return;
            }
        }

        std::vector<CoursePost> posts;
        for (const CoursePost& post : gm.getPosts()) {
            if (post.courseCode != code) continue;
            if (account->role == "student" && !semesters.count(post.semester)) continue;
            posts.push_back(post);
        }
        std::sort(posts.begin(), posts.end(), [](const CoursePost& a, const CoursePost& b) {
            if (a.semester != b.semester) return a.semester < b.semester;
            if (a.week != b.week) return a.week < b.week;
            return a.id < b.id;
        });
        json arr = json::array();
        for (const CoursePost& post : posts) arr.push_back(postJson(post));
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    svr.Post("/api/courses/:code/posts", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string code = req.path_params.at("code");
        const Course* course = gm.findCourse(code);
        if (!course) {
            setError(res, 404, "없는 과목입니다: " + code);
            return;
        }
        if (account->role == "professor" && !teaches(*account, *course)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        json body;
        if (!parseJsonBody(req, body, res)) return;

        CoursePost post;
        if (!readPostBody(body, post)) {
            setError(res, 400, kPostRules);
            return;
        }
        post.courseCode = code;
        post.author = account->username;
        post.createdAt = nowText();
        if (!post.isValid()) {
            setError(res, 400, kPostRules);
            return;
        }
        const int id = gm.addPost(post);
        if (id == 0) {
            setError(res, 400, kPostRules);
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "게시물 저장에 실패했습니다");
            return;
        }
        res.status = 201;
        res.set_content(postJson(*gm.findPost(id)).dump(), "application/json; charset=utf-8");
    });

    svr.Put("/api/courses/:code/posts/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string code = req.path_params.at("code");
        const Course* course = gm.findCourse(code);
        if (!course) {
            setError(res, 404, "없는 과목입니다: " + code);
            return;
        }
        if (account->role == "professor" && !teaches(*account, *course)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        const std::string idText = req.path_params.at("id");
        const int id = parsePostId(idText);
        const CoursePost* existing = id == 0 ? nullptr : gm.findPost(id);
        if (!existing || existing->courseCode != code) {
            setError(res, 404, "없는 게시물입니다: " + idText);
            return;
        }
        json body;
        if (!parseJsonBody(req, body, res)) return;

        // id·courseCode·author·createdAt 은 기존 값을 유지
        CoursePost input;
        if (!readPostBody(body, input)) {
            setError(res, 400, kPostRules);
            return;
        }
        CoursePost post = *existing;
        post.semester = input.semester;
        post.category = input.category;
        post.week = input.week;
        post.title = input.title;
        post.body = input.body;
        post.link = input.link;
        post.dueDate = input.dueDate;
        if (!post.isValid() || !gm.updatePost(post)) {
            setError(res, 400, kPostRules);
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "게시물 저장에 실패했습니다");
            return;
        }
        res.set_content(postJson(*gm.findPost(id)).dump(), "application/json; charset=utf-8");
    });

    svr.Delete("/api/courses/:code/posts/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string code = req.path_params.at("code");
        const Course* course = gm.findCourse(code);
        if (!course) {
            setError(res, 404, "없는 과목입니다: " + code);
            return;
        }
        if (account->role == "professor" && !teaches(*account, *course)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        const std::string idText = req.path_params.at("id");
        const int id = parsePostId(idText);
        const CoursePost* existing = id == 0 ? nullptr : gm.findPost(id);
        if (!existing || existing->courseCode != code) {
            setError(res, 404, "없는 게시물입니다: " + idText);
            return;
        }
        if (!gm.removePost(id) || !gm.save()) {
            setError(res, 500, "게시물 저장에 실패했습니다");
            return;
        }
        res.set_content(json{{"deleted", true}}.dump(), "application/json; charset=utf-8");
    });

    // 성적 등록: 수강신청 여부 확인, 과목 정원은 여기서 검사하지 않음
    //   본문 {studentId, courseCode, semester, score} 또는 score 대신 letter (둘 다 있으면 score)
    svr.Post("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        Grade grade;
        if (!readGradeBody(body, grade, res)) return;
        if (!gm.findStudent(grade.studentId)) {
            setError(res, 404, "없는 학생입니다: " + grade.studentId);
            return;
        }
        if (!gm.findCourse(grade.courseCode)) {
            setError(res, 404, "없는 과목입니다: " + grade.courseCode);
            return;
        }
        if (account->role == "professor" &&
            !isProfessorOf(*account, gm, grade.courseCode)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        const int year = thisYear();
        if (!isSemesterInRange(grade.studentId, grade.semester, year)) {
            const int admission = admissionYear(grade.studentId);
            setError(res, 400, grade.semester + " 학기는 등록할 수 없습니다 (이 학생은 " +
                std::to_string(admission) + "-1 ~ " + std::to_string(year + 1) +
                "-4 학기만 가능)");
            return;
        }
        if (gm.hasAssessments(grade.courseCode, grade.semester)) {
            setError(res, 409, kAssessedCourse);
            return;
        }
        if (findGrade(gm, grade.studentId, grade.courseCode, grade.semester)) {
            setError(res, 409, "이미 등록된 성적입니다");
            return;
        }
        if (!gm.findEnrollment(grade.studentId, grade.courseCode, grade.semester)) {
            setError(res, 409, kNotEnrolled);
            return;
        }
        if (!gm.addGrade(grade)) {
            setError(res, 400, kGradeRules);
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "성적 저장에 실패했습니다");
            return;
        }
        setJson(res, 201, gradeJson(*findGrade(gm, grade.studentId, grade.courseCode, grade.semester),
                                    gm.getGrades()));
    });


    // 빠른 성적 등록: 수강신청이 없으면 자동으로 생성
    // 본문: {studentId, courseCode, semester, score} 또는 letter
    svr.Post("/api/grades/quick", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);

        // 1. 관리자 또는 담당 교수 권한 확인
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;

        // 2. 요청 본문 파싱 (score 또는 letter 모두 지원)
        json body;
        if (!parseJsonBody(req, body, res)) return;

        Grade grade;
        if (!readGradeBody(body, grade, res)) return;

        // 3. 학생 및 과목 존재 여부 확인
        if (!gm.findStudent(grade.studentId)) {
            setError(res, 404, "없는 학생입니다: " + grade.studentId);
            return;
        }

        const Course* course = gm.findCourse(grade.courseCode);
        if (!course) {
            setError(res, 404, "없는 과목입니다: " + grade.courseCode);
            return;
        }

        // 4. 교수는 본인이 담당하는 과목에만 성적을 등록할 수 있음
        if (account->role == "professor" &&
            !isProfessorOf(*account, gm, grade.courseCode)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }

        // 5. 학생의 입학년도에 맞는 학기인지 확인
        const int year = thisYear();
        if (!isSemesterInRange(grade.studentId, grade.semester, year)) {
            const int admission = admissionYear(grade.studentId);
            setError(res, 400,
                grade.semester + " 학기는 등록할 수 없습니다 (이 학생은 " +
                std::to_string(admission) + "-1 ~ " +
                std::to_string(year + 1) + "-4 학기만 가능)");
            return;
        }

        // 6. 평가 항목으로 성적을 계산하는 과목인지 확인
        if (gm.hasAssessments(grade.courseCode, grade.semester)) {
            setError(res, 409, kAssessedCourse);
            return;
        }

        // 7. 동일 학생·과목·학기의 중복 성적 확인
        if (findGrade(gm, grade.studentId, grade.courseCode, grade.semester) != nullptr) {
            setError(res, 409, "이미 등록된 성적입니다");
            return;
        }

        // 8. 수강신청이 없으면 정원·시간표를 검사한 뒤 자동 생성
        bool createdEnrollment = false;
        if (!gm.findEnrollment(grade.studentId, grade.courseCode, grade.semester)) {
            Enrollment enrollment;
            enrollment.studentId = grade.studentId;
            enrollment.courseCode = grade.courseCode;
            enrollment.semester = grade.semester;

            // 과목 정원 검사 (capacity가 0이면 정원 제한 없음)
            if (course->capacity > 0) {
                int count = 0;
                for (const Enrollment& existing : gm.getEnrollments()) {
                    if (existing.courseCode == enrollment.courseCode &&
                        existing.semester == enrollment.semester) {
                        ++count;
                    }
                }

                if (count >= course->capacity) {
                    setError(res, 409,
                        enrollment.semester + " " + course->name +
                        " 과목의 정원(" + std::to_string(course->capacity) +
                        "명)이 가득 찼습니다");
                    return;
                }
            }

            // 같은 학생·학기의 기존 수강 과목과 시간표가 겹치는지 검사
            for (const Enrollment& existing : gm.getEnrollments()) {
                if (existing.studentId != enrollment.studentId ||
                    existing.semester != enrollment.semester) {
                    continue;
                }

                const Course* other = gm.findCourse(existing.courseCode);
                if (other && schedulesOverlap(course->schedule, other->schedule)) {
                    setError(res, 409, other->name + " 과목과 강의시간이 겹칩니다");
                    return;
                }
            }

            if (!gm.addEnrollment(enrollment)) {
                setError(res, 409, "수강신청 생성에 실패했습니다");
                return;
            }
            createdEnrollment = true;
        }

        // 9. 성적 등록
        if (!gm.addGrade(grade)) {
            if (createdEnrollment) {
                gm.removeEnrollment(grade.studentId, grade.courseCode, grade.semester);
            }
            setError(res, 409, "성적 등록에 실패했습니다");
            return;
        }

        // 10. 저장 실패 시 이번 요청에서 추가한 데이터 되돌리기
        if (!gm.save()) {
            gm.removeGrade(grade.studentId, grade.courseCode, grade.semester);
            if (createdEnrollment) {
                gm.removeEnrollment(grade.studentId, grade.courseCode, grade.semester);
            }
            setError(res, 500, "성적 저장에 실패했습니다");
            return;
        }

        // 11. 성공 응답
        const auto grades = gm.getGrades();
        const Grade* savedGrade = findGrade(gm, grade.studentId, grade.courseCode, grade.semester);
        if (!savedGrade) {
            setError(res, 500, "성적 등록 결과를 확인할 수 없습니다");
            return;
        }

        json response = gradeJson(*savedGrade, grades);
        response["message"] = "성적이 등록되었습니다";
        response["enrollmentCreated"] = createdEnrollment;
        setJson(res, 201, response);
    });


    svr.Put("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;
        Grade grade;
        if (!readGradeBody(body, grade, res)) return;
        if (account->role == "professor" &&
            !isProfessorOf(*account, gm, grade.courseCode)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (gm.hasAssessments(grade.courseCode, grade.semester)) {
            setError(res, 409, kAssessedCourse);
            return;
        }
        if (!gm.updateGrade(grade)) {
            setError(res, 404, "수정할 성적을 찾을 수 없습니다.");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "성적 저장에 실패했습니다");
            return;
        }
        setJson(res, 200, gradeJson(*findGrade(gm, grade.studentId, grade.courseCode, grade.semester),
                                    gm.getGrades()));
    });

    svr.Delete("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string studentId = req.get_param_value("studentId");
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        if (account->role == "professor" &&
            !isProfessorOf(*account, gm, courseCode)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (gm.hasAssessments(courseCode, semester)) {
            setError(res, 409, kAssessedCourse);
            return;
        }
        if (!gm.removeGrade(studentId, courseCode, semester)) {
            setError(res, 404, "삭제할 성적을 찾을 수 없습니다.");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "성적 삭제 저장에 실패했습니다");
            return;
        }
        setJson(res, 200, json{{"deleted", true}});
    });

    // 평가 기준 조회 (과목·학기별 6개 분류 비율과 평가 방식)
    svr.Get("/api/assessments", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        if (courseCode.empty() || semester.empty()) {
            setError(res, 400, "courseCode, semester 를 모두 지정해야 합니다");
            return;
        }
        if (!isValidSemester(semester)) {
            setError(res, 400, kSemesterRules);
            return;
        }
        if (!gm.findCourse(courseCode)) {
            setError(res, 404, "없는 과목입니다: " + courseCode);
            return;
        }
        const json items = gradingSchemeItemsJson(gm, courseCode, semester);
        setJson(res, 200, json{
            {"courseCode", courseCode},
            {"semester", semester},
            {"mode", gm.getGradingMode(courseCode, semester)},
            {"items", items}
        });
    });

    // 평가 기준 전체 등록·수정: {courseCode,semester,mode,weights:{출석:0,...}}. POST 는 사용하지 않는다.
    svr.Put("/api/assessments", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        std::string courseCode, semester, mode;
        if (!readString(body, "courseCode", courseCode, res) ||
            !readString(body, "semester", semester, res) ||
            !readString(body, "mode", mode, res)) return;
        const auto weightsIt = body.find("weights");
        if (weightsIt == body.end() || !weightsIt->is_object()) {
            setError(res, 400, kGradingSchemeRules);
            return;
        }
        if (!isValidSemester(semester)) {
            setError(res, 400, kSemesterRules);
            return;
        }
        if (!checkCourseAccess(*account, gm, courseCode, res)) return;

        std::map<std::string, int> weights;
        for (auto it = weightsIt->begin(); it != weightsIt->end(); ++it) {
            if (std::find(kAssessmentCategories.begin(), kAssessmentCategories.end(), it.key()) ==
                    kAssessmentCategories.end() || !it.value().is_number_integer()) {
                setError(res, 400, kGradingSchemeRules);
                return;
            }
            const long long value = it.value().get<long long>();
            if (value < 0 || value > 100) {
                setError(res, 400, kGradingSchemeRules);
                return;
            }
            weights[it.key()] = static_cast<int>(value);
        }
        for (const std::string& category : kAssessmentCategories) {
            if (weights.find(category) == weights.end()) weights[category] = 0;
        }
        int total = 0;
        for (const auto& entry : weights) total += entry.second;
        if (total != 100) {
            setError(res, 400, "비율 합계가 100이어야 합니다 (지금 " + std::to_string(total) + "%)");
            return;
        }
        if (mode != "absolute" && mode != "relative") {
            setError(res, 400, kGradingSchemeRules);
            return;
        }
        if (gm.getAssessments(courseCode, semester).empty()) {
            for (const Grade& grade : gm.getGrades()) {
                if (grade.courseCode == courseCode && grade.semester == semester) {
                    setError(res, 409, "직접 입력한 성적이 있는 과목·학기에는 평가 기준을 만들 수 없습니다");
                    return;
                }
            }
        }
        if (!gm.setGradingScheme(courseCode, semester, mode, weights)) {
            setError(res, 400, kGradingSchemeRules);
            return;
        }
        if (!gm.save()) {
            setError(res, 500, kSaveFailed);
            return;
        }
        const json items = gradingSchemeItemsJson(gm, courseCode, semester);
        setJson(res, 200, json{
            {"courseCode", courseCode},
            {"semester", semester},
            {"mode", gm.getGradingMode(courseCode, semester)},
            {"items", items}
        });
    });

    // 평가 기준 전체 삭제
    svr.Delete("/api/assessments", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        if (courseCode.empty() || semester.empty()) {
            setError(res, 400, "courseCode, semester 를 모두 지정해야 합니다");
            return;
        }
        if (!isValidSemester(semester)) {
            setError(res, 400, kSemesterRules);
            return;
        }
        if (!checkCourseAccess(*account, gm, courseCode, res)) return;
        if (!gm.removeGradingScheme(courseCode, semester)) {
            setError(res, 404, "평가 기준이 없습니다");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, kSaveFailed);
            return;
        }
        setJson(res, 200, json{{"deleted", true}});
    });

    // 항목 점수: student 는 쿼리와 상관없이 자기 것 전부, admin·담당 교수는 courseCode·semester 필수
    svr.Get("/api/assessment-scores", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;
        json arr = json::array();
        if (account->role == "student") {
            for (const AssessmentScore& score : gm.getAssessmentScoresByStudent(account->studentId)) {
                arr.push_back(assessmentScoreJson(score));
            }
            setJson(res, 200, arr);
            return;
        }
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        if (courseCode.empty() || semester.empty()) {
            setError(res, 400, "courseCode, semester 를 모두 지정해야 합니다");
            return;
        }
        if (!checkCourseAccess(*account, gm, courseCode, res)) return;
        for (const AssessmentScore& score : gm.getAssessmentScores(courseCode, semester)) {
            arr.push_back(assessmentScoreJson(score));
        }
        setJson(res, 200, arr);
    });

    // 본문 {studentId, courseCode, semester, name, score}: 있으면 수정, 없으면 추가 (성적 다시 계산)
    svr.Put("/api/assessment-scores", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        AssessmentScore score;
        if (!readString(body, "studentId", score.studentId, res) ||
            !readString(body, "courseCode", score.courseCode, res) ||
            !readString(body, "semester", score.semester, res) ||
            !readString(body, "name", score.name, res)) return;
        const auto value = body.find("score");
        if (value == body.end() || !readScore(*value, score.score)) {
            setError(res, 400, kScoreRules);
            return;
        }
        if (!isValidSemester(score.semester)) {
            setError(res, 400, kSemesterRules);
            return;
        }
        if (!checkCourseAccess(*account, gm, score.courseCode, res)) return;
        if (!gm.findStudent(score.studentId)) {
            setError(res, 404, "없는 학생입니다: " + score.studentId);
            return;
        }
        if (std::find(kAssessmentCategories.begin(), kAssessmentCategories.end(), score.name) ==
            kAssessmentCategories.end()) {
            setError(res, 400, kAssessmentRules);
            return;
        }
        if (score.name == kAttendanceCategory) {
            setError(res, 409, "출석 점수는 출석부에서 자동 계산됩니다");
            return;
        }
        const std::vector<Assessment> items = gm.getAssessments(score.courseCode, score.semester);
        const auto item = std::find_if(items.begin(), items.end(), [&](const Assessment& current) {
            return current.name == score.name;
        });
        if (item == items.end()) {
            setError(res, 409, "비율이 0%인 항목에는 점수를 입력할 수 없습니다");
            return;
        }
        if (!gm.findEnrollment(score.studentId, score.courseCode, score.semester)) {
            setError(res, 409, kNotEnrolled);
            return;
        }
        if (!gm.setAssessmentScore(score)) {
            setError(res, 400, kScoreRules);
            return;
        }
        if (!gm.save()) {
            setError(res, 500, kSaveFailed);
            return;
        }
        setJson(res, 200, assessmentScoreJson(score));
    });

    svr.Delete("/api/assessment-scores", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string studentId = req.get_param_value("studentId");
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        const std::string name = req.get_param_value("name");
        if (studentId.empty() || courseCode.empty() || semester.empty() || name.empty()) {
            setError(res, 400, "studentId, courseCode, semester, name 을 모두 지정해야 합니다");
            return;
        }
        if (!checkCourseAccess(*account, gm, courseCode, res)) return;
        if (!gm.findStudent(studentId)) {
            setError(res, 404, "없는 학생입니다: " + studentId);
            return;
        }
        if (std::find(kAssessmentCategories.begin(), kAssessmentCategories.end(), name) ==
            kAssessmentCategories.end()) {
            setError(res, 400, kAssessmentRules);
            return;
        }
        if (name == kAttendanceCategory) {
            setError(res, 409, "출석 점수는 출석부에서 자동 계산됩니다");
            return;
        }
        const std::vector<Assessment> items = gm.getAssessments(courseCode, semester);
        if (std::none_of(items.begin(), items.end(), [&](const Assessment& item) {
                return item.name == name;
            })) {
            setError(res, 409, "비율이 0%인 항목에는 점수를 입력할 수 없습니다");
            return;
        }
        if (!gm.removeAssessmentScore(studentId, courseCode, semester, name)) {
            setError(res, 404, "입력된 항목 점수가 없습니다: " + studentId + " " + name);
            return;
        }
        if (!gm.save()) {
            setError(res, 500, kSaveFailed);
            return;
        }
        setJson(res, 200, json{{"deleted", true}});
    });

    std::cout << "http://localhost:" << port
              << " 에서 실행 중 (종료: Ctrl+C)" << std::endl;
    if (!svr.listen("127.0.0.1", port)) {
        std::cerr << "포트 " << port << " 를 열 수 없습니다 (이미 사용 중인지 확인)"
                  << std::endl;
    }
}
