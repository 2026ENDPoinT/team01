#include "server.h"
#include "statistics.h"
#include "course_post.h"
#include "retake.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
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

void setError(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(json{{"error", message}}.dump(), "application/json; charset=utf-8");
}

void setText(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(message, "text/plain; charset=utf-8");
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

bool hasGrade(const GradeManager& gm, const std::string& studentId,
              const std::string& courseCode, const std::string& semester) {
    const auto& grades = gm.getGrades();
    return std::any_of(grades.begin(), grades.end(), [&](const Grade& grade) {
        return grade.studentId == studentId &&
               grade.courseCode == courseCode &&
               grade.semester == semester;
    });
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
            setError(res, 400, "요청 본문이 올바르지 않습니다");
            return false;
        }
        return true;
    } catch (...) {
        setError(res, 400, "요청 본문이 올바르지 않습니다");
        return false;
    }
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
        const std::string username = body.value("username", "");
        const std::string password = body.value("password", "");
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
        const std::string currentPassword = body.value("currentPassword", "");
        const std::string newPassword = body.value("newPassword", "");
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
        account.username = body.value("username", "");
        account.role = body.value("role", "");
        account.name = body.value("name", "");
        account.studentId = body.value("studentId", "");
        const std::string password = body.value("password", "");

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
        const std::string role = body.value("role", oldAccount->role);
        std::string name = body.value("name", oldAccount->name);
        std::string studentId = body.value("studentId", oldAccount->studentId);
        const std::string password = body.value("password", "");

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
            if (include) arr.push_back({
                {"id", student.id}, {"name", student.name},
                {"major", student.major}, {"year", student.year}
            });
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
            if (include) arr.push_back({
                {"id", student.id}, {"name", student.name},
                {"major", student.major}, {"year", student.year}
            });
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    // 과목 목록
    svr.Get("/api/courses", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        json arr = json::array();
        for (const Course& c : gm.getCourses()) {
            arr.push_back({
                {"code", c.code}, {"name", c.name}, {"credit", c.credit},
                {"professor", c.professor}, {"category", c.category}, {"area", c.area},
                {"schedule", c.schedule}, {"language", c.language},
                {"capacity", c.capacity}, {"remote", c.remote}
            });
        }
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
            arr.push_back({
                {"studentId", grade.studentId}, {"courseCode", grade.courseCode},
                {"semester", grade.semester}, {"score", grade.score},
                {"letter", Retake::officialLetter(grade, grades)}
            });
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
        const double gpa = Statistics::calcGPA(gm, id, req.get_param_value("semester"));
        res.set_content(json{{"studentId", id},
                             {"semester", req.get_param_value("semester")},
                             {"gpa", gpa}}.dump(),
                        "application/json; charset=utf-8");
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
        const std::string semester = body.value("semester", "");
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
                {"hasGrade", hasGrade(gm, enrollment.studentId, enrollment.courseCode, enrollment.semester)}
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
        enrollment.studentId = body.value("studentId", "");
        enrollment.courseCode = body.value("courseCode", "");
        enrollment.semester = body.value("semester", "");

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
            {"semester", enrollment.semester}, {"hasGrade", false}
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
        if (hasGrade(gm, studentId, courseCode, semester)) {
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
            setError(res, 400, "학생 정보가 올바르지 않습니다");
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
        setText(res, 201, "학생 정보가 등록되었습니다");
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
            setError(res, 400, "학생 정보가 올바르지 않습니다");
            return;
        }
        if (!gm.updateStudent(oldId, student)) {
            setError(res, 404, "수정할 학생을 찾을 수 없습니다.");
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
        setText(res, 200, "학생 정보가 수정되었습니다");
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
        setText(res, 200, "학생 정보가 삭제되었습니다.");
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
        if (!gm.addCourse(course)) {
            setError(res, 409, "과목 등록 실패 (이미 등록된 과목입니다)");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "과목 정보 저장에 실패했습니다");
            return;
        }
        setText(res, 201, "과목 정보가 등록되었습니다");
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
        if (!gm.updateCourse(req.path_params.at("code"), course)) {
            setError(res, 404, "수정할 과목을 찾을 수 없습니다.");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "과목 정보 저장에 실패했습니다");
            return;
        }
        setText(res, 200, "과목 정보가 수정되었습니다");
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
        setText(res, 200, "과목 정보가 삭제되었습니다.");
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
    svr.Post("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        Grade grade;
        try {
            grade.studentId = body.at("studentId").get<std::string>();
            grade.courseCode = body.at("courseCode").get<std::string>();
            grade.semester = body.at("semester").get<std::string>();
            grade.score = body.at("score").get<double>();
        } catch (...) {
            setError(res, 400, kGradeRules);
            return;
        }
        if (!grade.isValid()) {
            setError(res, 400, kGradeRules);
            return;
        }
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
        for (const Grade& existing : gm.getGrades()) {
            if (existing.studentId == grade.studentId &&
                existing.courseCode == grade.courseCode &&
                existing.semester == grade.semester) {
                setError(res, 409, "이미 등록된 성적입니다");
                return;
            }
        }
        if (!gm.findEnrollment(grade.studentId, grade.courseCode, grade.semester)) {
            setError(res, 409, "수강신청하지 않은 학생입니다 (학번·과목·학기를 확인하세요)");
            return;
        }
        if (!gm.addGrade(grade) || !gm.save()) {
            setError(res, 500, "성적 등록에 실패했습니다");
            return;
        }
        res.set_content("성적 정보가 등록되었습니다", "text/plain; charset=utf-8");
    });

    svr.Put("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;
        Grade grade;
        try {
            grade.studentId = body.at("studentId").get<std::string>();
            grade.courseCode = body.at("courseCode").get<std::string>();
            grade.semester = body.at("semester").get<std::string>();
            grade.score = body.at("score").get<double>();
        } catch (...) {
            setError(res, 400, kGradeRules);
            return;
        }
        if (account->role == "professor" &&
            !isProfessorOf(*account, gm, grade.courseCode)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (!grade.isValid()) {
            setError(res, 400, kGradeRules);
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
        setText(res, 200, "성적 정보가 수정되었습니다");
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
        if (!gm.removeGrade(studentId, courseCode, semester)) {
            setError(res, 404, "삭제할 성적을 찾을 수 없습니다.");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, "성적 삭제 저장에 실패했습니다");
            return;
        }
        setText(res, 200, "성적 정보가 삭제되었습니다.");
    });

    std::cout << "http://localhost:" << port
              << " 에서 실행 중 (종료: Ctrl+C)" << std::endl;
    if (!svr.listen("127.0.0.1", port)) {
        std::cerr << "포트 " << port << " 를 열 수 없습니다 (이미 사용 중인지 확인)"
                  << std::endl;
    }
}
