#include "server.h"
#include "semester.h"
#include "statistics.h"
#include "course_post.h"
#include "retake.h"
#include "request_validation.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <iostream>
#include <random>
#include <sstream>
#include <tuple>
#include <unordered_set>

#include "httplib.h"
#include "json.hpp"
#include "report_exporter.h"

using json = nlohmann::json;

namespace {

constexpr const char* kPastSemester = "지난 학기는 접근할 수 없습니다";

constexpr const char* kGradeRules =
    "성적 정보가 올바르지 않습니다 (학번 숫자 8자리, 과목코드 숫자 6자리, "
    "학기 예: 2026-1 (1·2 정규학기, 3 여름·4 겨울 계절학기), 점수 0~100)";

constexpr const char* kSemesterRules =
    "학기 형식이 올바르지 않습니다 (예: 2026-1, 1·2 정규학기, 3 여름·4 겨울 계절학기)";

constexpr const char* kAccountInvalid = "계정 정보가 올바르지 않습니다";
// 위 검사를 통과했는데 AccountStore 가 거부한 경우 (남는 규칙 안내)
constexpr const char* kAccountRules =
    "계정 정보가 올바르지 않습니다 (아이디 영문·숫자·_ 3~20자, 비밀번호 8~64자, 교수·관리자는 이름 필수, 학생은 학번 필수)";

constexpr const char* kBadBody = "요청 본문이 올바르지 않습니다";
constexpr const char* kSaveFailed = "저장에 실패했습니다";
constexpr const char* kStudentRules =
    "학생 정보가 올바르지 않습니다 (학번 숫자 8자리, 이름 필수, 학년 1~4)";
constexpr const char* kScoreRules = "점수는 0~100 사이 숫자여야 합니다";
constexpr const char* kLetterRules = "지원하지 않는 성적 등급입니다 (A+ A0 B+ B0 C+ C0 D+ D0 F, P/F 과목은 P NP)";
constexpr const char* kPassFailOnly = "P/F 과목은 P(합격) 또는 NP(불합격)로 입력하세요";
constexpr const char* kPassFailLetterOnly = "P·NP 는 P/F 과목에만 입력할 수 있습니다";
constexpr const char* kAssessmentRules =
    "평가 항목이 올바르지 않습니다 (평가 항목 출석·중간고사·기말고사·과제·팀프로젝트·기타, 비율 0~100 정수)";
constexpr const char* kGradingSchemeRules =
    "평가 기준이 올바르지 않습니다 (분류별 비율 0~100 정수, 합계 100, 방식 absolute·relative)";
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

bool validPassword(const std::string& password) {
    return password.size() >= 8 && password.size() <= 64;
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
        {"capacity", c.capacity}, {"remote", c.remote}, {"gradeType", c.gradeType}
    };
}

// 상대평가 과목은 해당 학기 수강생 전원의 성적이 완성되어야 등급을 공개한다.
bool relativeGradingComplete(const GradeManager& gm, const std::string& courseCode,
                             const std::string& semester) {
    if (gm.getGradingMode(courseCode, semester) != "relative") return true;

    size_t enrolledCount = 0;
    for (const Enrollment& enrollment : gm.getEnrollments()) {
        if (enrollment.courseCode != courseCode || enrollment.semester != semester) continue;
        ++enrolledCount;
        const bool hasGrade = std::any_of(gm.getGrades().begin(), gm.getGrades().end(),
                                          [&](const Grade& current) {
            return current.studentId == enrollment.studentId &&
                   current.courseCode == courseCode && current.semester == semester;
        });
        if (!hasGrade) return false;
    }

    return enrolledCount > 0;
}

// letter 는 재수강 규칙을 적용한 등급 (grades 에는 gm.getGrades() 를 넘김).
// 상대평가 입력이 미완성인 동안에는 절대평가 등급(F 등)이 대신 노출되지 않도록 빈 문자열을 반환한다.
json gradeJson(const Grade& grade, const std::vector<Grade>& grades, const GradeManager& gm) {
    const std::string letter = relativeGradingComplete(gm, grade.courseCode, grade.semester)
        ? Retake::officialLetter(grade, grades)
        : "";
    return {
        {"studentId", grade.studentId}, {"courseCode", grade.courseCode},
        {"semester", grade.semester}, {"score", grade.score},
        {"letter", letter}
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
    for (const std::string& category : assessmentCategories()) {
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
        const std::string text = letter->is_string() ? letter->get<std::string>() : "";
        if (isPassFailLetter(text)) {
            // P/F 과목 성적: 등급은 assignedLetter 에, 점수는 P = 100 / NP = 0 (과목이 P/F 인지는 checkGradeType)
            grade.assignedLetter = text;
            grade.score = text == kLetterPass ? kPassScore : kNonPassScore;
        } else {
            grade.score = letter->is_string() ? letterToScore(text) : -1.0;
        }
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
    Grade rest = grade;  // P·NP 는 Grade::isValid 의 등급 검사에서 빼고 checkGradeType 에서 봄
    if (isPassFailLetter(rest.assignedLetter)) rest.assignedLetter.clear();
    if (!rest.isValid()) {
        setError(res, 400, kGradeRules);
        return false;
    }
    return true;
}

// 성적이 과목의 성적 방식에 맞는지: P/F 과목은 P·NP 만, 일반 과목은 P·NP 를 받지 않음. 어긋나면 400 을 채우고 false
//   과목이 없으면 통과 (없는 과목은 각 라우트가 404 로 처리)
bool checkGradeType(const GradeManager& gm, const Grade& grade, httplib::Response& res) {
    const Course* course = gm.findCourse(grade.courseCode);
    if (!course) return true;
    const bool pfLetter = isPassFailLetter(grade.assignedLetter);
    if (course->isPassFail() && !pfLetter) {
        setError(res, 400, kPassFailOnly);
        return false;
    }
    if (!course->isPassFail() && pfLetter) {
        setError(res, 400, kPassFailLetterOnly);
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

// 지난 학기 접근 거부 메시지에 쓰는 현재 학기 (설정값이 없으면 오늘 날짜로 계산)
std::string currentSemester(const GradeManager& gm) {
    return semester::current(gm.getEnrollmentSemester(), std::time(nullptr));
}

bool isPastSemester(const GradeManager& gm, const std::string& sem) {
    return semester::isPast(sem, currentSemester(gm));
}

// 비관리자가 지난 학기를 대상으로 하면 403 을 채우고 true (관리자·빈 학기·미래 학기는 false)
bool rejectPastSemester(const GradeManager& gm, const Account& account,
                        const std::string& sem, httplib::Response& res) {
    if (account.role == "admin" || sem.empty()) return false;
    const std::string current = currentSemester(gm);
    if (!semester::isPast(sem, current)) return false;
    setError(res, 403, std::string(kPastSemester) + " (현재 학기: " + current + ")");
    return true;
}

// 파일 전체를 읽는다 (없거나 읽기 실패면 false)
bool readWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    if (in.bad()) return false;
    out = buf.str();
    return true;
}

// 내용 해시 (FNV-1a 64bit, 16자리 헥사) — 정적 파일 URL 의 ?v= 값
std::string contentHash(const std::string& data) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

// html 안의 `attr="file"` 을 `attr="file?v=<해시>"` 로 바꾼다 (파일을 못 읽으면 경고 후 원본 유지)
void versionAsset(std::string& html, const std::string& webDir,
                  const std::string& attr, const std::string& file) {
    const std::string needle = attr + "=\"" + file + "\"";
    const std::size_t pos = html.find(needle);
    if (pos == std::string::npos) return;
    std::string data;
    if (!readWholeFile(webDir + "/" + file, data)) {
        std::cerr << "[server] 정적 파일을 읽을 수 없어 버전을 붙이지 못했습니다: "
                  << webDir << "/" << file << std::endl;
        return;
    }
    html.replace(pos, needle.size(),
                 attr + "=\"" + file + "?v=" + contentHash(data) + "\"");
}

// index.html 을 읽어 style.css / app.js 참조에 내용 해시를 붙인다.
// 엣지(Cloudflare)·브라우저에 옛 파일이 캐시돼 있어도 파일이 바뀌면 URL 이 바뀌어 곧바로 새 파일을 받는다.
bool renderIndexHtml(const std::string& webDir, std::string& html) {
    if (!readWholeFile(webDir + "/index.html", html)) return false;
    versionAsset(html, webDir, "href", "style.css");
    versionAsset(html, webDir, "src", "app.js");
    return true;
}

}  // namespace

Server::Server(GradeManager& gm, AccountStore& accounts,
               const std::string& webDir, int port, const std::string& bindAddress)
    : gm(gm), webDir(webDir), port(port), bindAddress(bindAddress), accounts(&accounts) {}

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

    // 모든 응답(정적 파일·API 공통)에 Cache-Control: no-cache — 지정이 없으면 Cloudflare 가
    // 정적 파일을 기본 4시간 엣지 캐시해서 배포 후에도 옛 app.js 가 나간다. (no-cache = 매번 재검증)
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        if (!res.has_header("Cache-Control")) res.set_header("Cache-Control", "no-cache");
    });

    // index.html 은 마운트 포인트가 먼저 처리하므로 라우팅 전에 가로채서 동적으로 서빙한다.
    // style.css / app.js 참조에 내용 해시 (?v=) 를 붙여 파일이 바뀌면 캐시 키도 바뀌게 한다.
    svr.set_pre_routing_handler(
        [this](const httplib::Request& req, httplib::Response& res) {
            if ((req.method != "GET" && req.method != "HEAD") ||
                (req.path != "/" && req.path != "/index.html")) {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            std::string html;
            if (!renderIndexHtml(webDir, html)) {
                std::cerr << "[server] index.html 을 읽을 수 없습니다: " << webDir << std::endl;
                res.status = 404;
                res.set_content("index.html 을 찾을 수 없습니다", "text/plain; charset=utf-8");
                return httplib::Server::HandlerResponse::Handled;
            }
            res.set_content(html, "text/html; charset=utf-8");
            return httplib::Server::HandlerResponse::Handled;
        });

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

        // 틀린 항목을 짚는 문구 (예: "비밀번호는 8~64자여야 합니다")
        const std::string reason = RequestValidation::accountError(
            account.username, true, account.role, account.name, account.studentId, password, true);
        if (!reason.empty()) {
            setError(res, 400, std::string(kAccountInvalid) + " (" + reason + ")");
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
        // 비밀번호는 "" 이면 바꾸지 않으므로 넣었을 때만 검사
        const std::string reason = RequestValidation::accountError(username, false, role, name, studentId, password, false);
        if (!reason.empty()) {
            setError(res, 400, std::string(kAccountInvalid) + " (" + reason + ")");
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
        const std::string current = currentSemester(gm);
        const auto grades = gm.getGrades();
        json arr = json::array();

        for (const Grade& grade : grades) {
            if (!requestedStudent.empty() && grade.studentId != requestedStudent) continue;
            if (!requestedCourse.empty() && grade.courseCode != requestedCourse) continue;
            if (!requestedSemester.empty() && grade.semester != requestedSemester) continue;
            if (account->role == "student" && grade.studentId != account->studentId) continue;
            if (account->role == "professor" && semester::isPast(grade.semester, current)) continue;
            if (account->role == "professor" &&
                !isProfessorOf(*account, gm, grade.courseCode)) continue;
            arr.push_back(gradeJson(grade, grades, gm));
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });

    // 전체 학생 GPA 일괄 조회 (관리자 전용). 개별 GPA API 와 같은 값·null 규칙.
    // 고정 경로를 /:id 보다 먼저 등록해 "batch" 가 학생 ID 로 해석되지 않게 한다.
    svr.Get("/api/stats/gpa/batch", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin"})) return;

        json arr = json::array();
        for (const Student& student : gm.getStudents()) {
            const Statistics::GpaSummary summary = Statistics::gpaSummary(gm, student.id);
            const json gpa = summary.credits > 0
                ? json(Statistics::calcGPA(gm, student.id))
                : json(nullptr);
            arr.push_back({
                {"studentId", student.id}, {"semester", ""}, {"gpa", gpa}
            });
        }
        setJson(res, 200, arr);
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
            semesters.push_back({
                {"semester", s.semester}, {"gpa", s.gpa}, {"credits", s.credits},
                {"majorGpa", s.majorCredits > 0 ? json(s.majorGpa) : json(nullptr)},
                {"majorCredits", s.majorCredits}
            });
        }
        setJson(res, 200, json{
            {"semesters", semesters},
            {"gpa", summary.credits > 0 ? json(summary.gpa) : json(nullptr)},
            {"credits", summary.credits},
            {"majorGpa", summary.majorCredits > 0 ? json(summary.majorGpa) : json(nullptr)},
            {"majorCredits", summary.majorCredits}
        });
    });

        // 재수강 시뮬레이션: 그 학생의 성적 복사본에 가상 성적을 넣어 전/후 평점을 계산. 저장하지 않음 (gm.save() 호출 금지)
    //   본문 {studentId?, items:[{courseCode, letter}]}: 학생은 본인만 (studentId 생략 가능), 관리자는 studentId 필수
    svr.Post("/api/stats/simulate/retake", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "student"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        std::string studentId;
        if (!readOptString(body, "studentId", account->role == "student" ? account->studentId : "",
                           studentId, res)) return;
        if (account->role == "student" && studentId != account->studentId) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (studentId.empty()) {
            setError(res, 400, "studentId 를 지정해야 합니다");
            return;
        }
        if (!gm.findStudent(studentId)) {
            setError(res, 404, "없는 학생입니다: " + studentId);
            return;
        }

        const auto itemsIt = body.find("items");
        if (itemsIt == body.end() || !itemsIt->is_array() || itemsIt->empty()) {
            setError(res, 400, "items 에 과목코드와 등급을 하나 이상 넣으세요 (예: [{\"courseCode\":\"100101\",\"letter\":\"A+\"}])");
            return;
        }
        std::vector<Statistics::RetakeItem> items;
        for (const json& entry : *itemsIt) {
            Statistics::RetakeItem item;
            if (!entry.is_object()) {
                setError(res, 400, "items 항목이 올바르지 않습니다");
                return;
            }
            if (!readString(entry, "courseCode", item.courseCode, res) ||
                !readString(entry, "letter", item.letter, res)) return;
            items.push_back(item);
        }

        const Statistics::RetakeSimulation sim =
            Statistics::simulateRetake(gm, studentId, currentSemester(gm), items);
        if (sim.error != Statistics::RetakeError::None) {
            setError(res, sim.error == Statistics::RetakeError::NoCourse ? 404 : 400, sim.message);
            return;
        }

        const auto summaryJson = [](const Statistics::GpaSummary& s) {
            return json{
                {"gpa", s.credits > 0 ? json(s.gpa) : json(nullptr)},
                {"credits", s.credits},
                {"majorGpa", s.majorCredits > 0 ? json(s.majorGpa) : json(nullptr)},
                {"majorCredits", s.majorCredits}
            };
        };
        json itemsJson = json::array();
        for (const Statistics::RetakeItemResult& r : sim.items) {
            itemsJson.push_back({{"courseCode", r.courseCode}, {"previousLetter", r.previousLetter},
                                 {"appliedLetter", r.appliedLetter}});
        }
        setJson(res, 200, json{{"before", summaryJson(sim.before)}, {"after", summaryJson(sim.after)},
                               {"items", itemsJson}});
    });

    // 전체 과목 통계 일괄 조회. 개별 과목 통계와 같은 필드·계산 규칙.
    // 고정 경로를 /:code 보다 먼저 등록한다.
    svr.Get("/api/stats/course/batch", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;

        json arr = json::array();
        for (const Course& course : gm.getCourses()) {
            const auto stats = Statistics::courseStats(gm, course.code);
            json enrolledBySemester = json::object();
            for (const Enrollment& enrollment : gm.getEnrollments()) {
                if (enrollment.courseCode != stats.courseCode || enrollment.semester.empty()) continue;
                enrolledBySemester[enrollment.semester] =
                    enrolledBySemester.value(enrollment.semester, 0) + 1;
            }
            arr.push_back({
                {"courseCode", stats.courseCode}, {"count", stats.count},
                {"average", stats.average}, {"max", stats.max}, {"min", stats.min},
                {"latestSemester", stats.latestSemester}, {"latestEnrolled", stats.latestEnrolled},
                {"enrolledBySemester", enrolledBySemester}
            });
        }
        setJson(res, 200, arr);
    });

    // 과목 통계
    svr.Get("/api/stats/course/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        const auto stats = Statistics::courseStats(gm, req.path_params.at("code"));
        // 학기별 수강신청 인원 (화면이 과목 카드·과목 페이지에서 같은 학기를 고르고 그 학기 인원을 보이는 데 씀)
        json enrolledBySemester = json::object();
        for (const Enrollment& e : gm.getEnrollments()) {
            if (e.courseCode != stats.courseCode || e.semester.empty()) continue;
            enrolledBySemester[e.semester] = enrolledBySemester.value(e.semester, 0) + 1;
        }
        json result = {
            {"courseCode", stats.courseCode}, {"count", stats.count},
            {"average", stats.average}, {"max", stats.max}, {"min", stats.min},
            {"latestSemester", stats.latestSemester}, {"latestEnrolled", stats.latestEnrolled},
            {"enrolledBySemester", enrolledBySemester}
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
            const std::string letter = relativeGradingComplete(gm, grade.courseCode, grade.semester)
                ? Retake::officialLetter(grade, grades)
                : "";
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
                   escapeCSV(relativeGradingComplete(gm, grade.courseCode, grade.semester)
                                  ? Retake::officialLetter(grade, grades)
                                  : "") + "\r\n";
        }
        res.set_header("Content-Disposition", "attachment; filename=\"course_grades.csv\"");
        res.set_content(csv, "text/csv; charset=utf-8");
    });

    // 수강신청 학기 설정 조회 / 변경
    svr.Get("/api/enrollment/settings", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        std::string latestGradeSemester;
        for (const Grade& grade : gm.getGrades()) {
            if (grade.semester > latestGradeSemester) latestGradeSemester = grade.semester;
        }
        setJson(res, 200, json{
            {"semester", gm.getEnrollmentSemester()},
            {"latestGradeSemester", latestGradeSemester}
        });
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

    // 현재 학기 (설정된 수강신청 학기, 없으면 오늘 날짜 기준)
    svr.Get("/api/semester/current", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!requireRole(req, res, {"admin", "professor", "student"})) return;
        setJson(res, 200, json{{"semester", currentSemester(gm)}});
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
        const std::string current = currentSemester(gm);
        const auto graded = gradedEnrollments(gm);
        json arr = json::array();
        for (const Enrollment& enrollment : gm.getEnrollments()) {
            if (!requestedStudent.empty() && enrollment.studentId != requestedStudent) continue;
            if (!requestedCourse.empty() && enrollment.courseCode != requestedCourse) continue;
            if (!requestedSemester.empty() && enrollment.semester != requestedSemester) continue;
            if (account->role == "student" && enrollment.studentId != account->studentId) continue;
            if (account->role == "professor" &&
                semester::isPast(enrollment.semester, current)) continue;
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
        if (rejectPastSemester(gm, *account, enrollment.semester, res)) return;
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
            if (rejectPastSemester(gm, *account, semester, res)) return;
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
        // 틀린 항목을 짚는 문구 (예: 학점 "abc" → "학점은 숫자여야 합니다")
        const std::string reason = RequestValidation::readCourse(body, "", course);
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
        // 본문에 code 가 없으면 주소의 과목코드 그대로
        const std::string reason = RequestValidation::readCourse(body, req.path_params.at("code"), course);
        if (!reason.empty()) {
            setError(res, 400, "과목 정보가 올바르지 않습니다 (" + reason + ")");
            return;
        }
        const std::string oldCode = req.path_params.at("code");
        const Course* oldCourse = gm.findCourse(oldCode);
        if (oldCourse == nullptr) {
            setError(res, 404, "수정할 과목을 찾을 수 없습니다.");
            return;
        }
        // 성적 방식은 본문에 없으면 그대로 (예전 화면·API 로 수정해도 P/F 가 풀리지 않게)
        if (!body.contains("gradeType") || body["gradeType"].is_null()) course.gradeType = oldCourse->gradeType;
        if (course.gradeType != oldCourse->gradeType && gm.courseHasGradesOrAssessments(oldCode)) {
            setError(res, 409, "성적이나 평가 기준이 있는 과목은 성적 방식을 바꿀 수 없습니다");
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
            if (account->role != "admin" && isPastSemester(gm, post.semester)) continue;
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
        if (rejectPastSemester(gm, *account, post.semester, res)) return;
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
        if (rejectPastSemester(gm, *account, existing->semester, res)) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        // id·courseCode·author·createdAt 은 기존 값을 유지
        CoursePost input;
        if (!readPostBody(body, input)) {
            setError(res, 400, kPostRules);
            return;
        }
        if (rejectPastSemester(gm, *account, input.semester, res)) return;
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
        if (rejectPastSemester(gm, *account, existing->semester, res)) return;
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
        if (!checkGradeType(gm, grade, res)) return;
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
        if (rejectPastSemester(gm, *account, grade.semester, res)) return;
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
                                    gm.getGrades(), gm));
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
        if (!checkGradeType(gm, grade, res)) return;

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
        if (rejectPastSemester(gm, *account, grade.semester, res)) return;

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

        json response = gradeJson(*savedGrade, grades, gm);
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
        if (!checkGradeType(gm, grade, res)) return;
        if (account->role == "professor" &&
            !isProfessorOf(*account, gm, grade.courseCode)) {
            setError(res, 403, "권한이 없습니다");
            return;
        }
        if (rejectPastSemester(gm, *account, grade.semester, res)) return;
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
                                    gm.getGrades(), gm));
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
        if (rejectPastSemester(gm, *account, semester, res)) return;
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
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
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
        if (account->role == "professor" && rejectPastSemester(gm, *account, semester, res)) return;
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
    // 여러 (과목, 학기)의 평가 기준 일괄 조회. 각 항목은 기존 GET /api/assessments 와 같은 모양.
    // 본문: {"pairs":[{"courseCode":"109201","semester":"2026-2"}, ...]}
    svr.Post("/api/assessments/batch", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;

        json body;
        if (!parseJsonBody(req, body, res)) return;
        const auto pairsIt = body.find("pairs");
        if (pairsIt == body.end() || !pairsIt->is_array()) {
            setError(res, 400, "pairs 배열이 필요합니다");
            return;
        }

        json arr = json::array();
        std::unordered_set<std::string> seen;
        for (const json& pair : *pairsIt) {
            if (!pair.is_object() || !pair.contains("courseCode") ||
                !pair["courseCode"].is_string() || !pair.contains("semester") ||
                !pair["semester"].is_string()) {
                setError(res, 400, "평가 기준 조회 항목이 올바르지 않습니다");
                return;
            }
            const std::string courseCode = pair["courseCode"].get<std::string>();
            const std::string semester = pair["semester"].get<std::string>();
            if (courseCode.empty() || semester.empty() || !isValidSemester(semester)) {
                setError(res, 400, kSemesterRules);
                return;
            }
            if (account->role == "professor" && rejectPastSemester(gm, *account, semester, res)) return;
            if (!gm.findCourse(courseCode)) {
                setError(res, 404, "없는 과목입니다: " + courseCode);
                return;
            }

            const std::string key = courseCode + "\n" + semester;
            if (!seen.insert(key).second) continue;
            arr.push_back({
                {"courseCode", courseCode},
                {"semester", semester},
                {"mode", gm.getGradingMode(courseCode, semester)},
                {"items", gradingSchemeItemsJson(gm, courseCode, semester)}
            });
        }
        setJson(res, 200, arr);
    });

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
        if (rejectPastSemester(gm, *account, semester, res)) return;
        if (gm.findCourse(courseCode)->isPassFail()) {
            setError(res, 409, "P/F 과목에는 평가 기준을 만들 수 없습니다 (성적은 P·NP 로 입력)");
            return;
        }

        std::map<std::string, int> weights;
        for (auto it = weightsIt->begin(); it != weightsIt->end(); ++it) {
            if (std::find(assessmentCategories().begin(), assessmentCategories().end(), it.key()) ==
                    assessmentCategories().end() || !it.value().is_number_integer()) {
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
        for (const std::string& category : assessmentCategories()) {
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
        std::vector<int> weightsVector;
        for (const std::string& category : assessmentCategories()) {
            weightsVector.push_back(weights.at(category));
        }
        if (!gm.setGradingScheme(courseCode, semester, weightsVector, mode)) {
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
        if (rejectPastSemester(gm, *account, semester, res)) return;
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
        if (rejectPastSemester(gm, *account, semester, res)) return;
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
        if (rejectPastSemester(gm, *account, score.semester, res)) return;
        if (!gm.findStudent(score.studentId)) {
            setError(res, 404, "없는 학생입니다: " + score.studentId);
            return;
        }
        if (std::find(assessmentCategories().begin(), assessmentCategories().end(), score.name) ==
            assessmentCategories().end()) {
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
        if (item == items.end() || item->weight == 0) {
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
        if (rejectPastSemester(gm, *account, semester, res)) return;
        if (!gm.findStudent(studentId)) {
            setError(res, 404, "없는 학생입니다: " + studentId);
            return;
        }
        if (std::find(assessmentCategories().begin(), assessmentCategories().end(), name) ==
            assessmentCategories().end()) {
            setError(res, 400, kAssessmentRules);
            return;
        }
        if (name == kAttendanceCategory) {
            setError(res, 409, "출석 점수는 출석부에서 자동 계산됩니다");
            return;
        }
        const std::vector<Assessment> items = gm.getAssessments(courseCode, semester);
        if (std::none_of(items.begin(), items.end(), [&](const Assessment& item) {
                return item.name == name && item.weight > 0;
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

    // 출결 조회: student 는 본인 전체 기록, admin·professor 는 과목·학기 지정
    svr.Get("/api/attendance", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor", "student"});
        if (!account) return;

        std::vector<Attendance> records;
        if (account->role == "student") {
            records = gm.getAttendanceByStudent(account->studentId);
        } else {
            const std::string courseCode = req.get_param_value("courseCode");
            const std::string semester = req.get_param_value("semester");
            if (courseCode.empty() || semester.empty()) {
                setError(res, 400, "courseCode, semester 를 모두 지정해야 합니다");
                return;
            }
            if (!checkCourseAccess(*account, gm, courseCode, res)) return;
            if (rejectPastSemester(gm, *account, semester, res)) return;
            records = gm.getAttendanceByCourse(courseCode, semester);
        }

        json recordItems = json::array();
        std::map<std::tuple<std::string, std::string, std::string>, std::vector<Attendance>> grouped;
        for (const Attendance& record : records) {
            recordItems.push_back({
                {"studentId", record.studentId}, {"courseCode", record.courseCode},
                {"semester", record.semester}, {"week", record.week}, {"status", record.status}
            });
            grouped[{record.studentId, record.courseCode, record.semester}].push_back(record);
        }

        json summaries = json::array();
        for (const auto& entry : grouped) {
            const auto& key = entry.first;
            const AttendanceSummary summary = summarizeAttendance(entry.second);
            json item = {
                {"studentId", std::get<0>(key)},
                {"present", summary.present}, {"late", summary.late},
                {"absent", summary.absent}, {"absentEquivalent", summary.absentEquivalent},
                {"score", summary.score < 0.0 ? json(nullptr) : json(summary.score)},
                {"shortage", summary.shortage}
            };
            if (account->role == "student") {
                item["courseCode"] = std::get<1>(key);
                item["semester"] = std::get<2>(key);
            }
            summaries.push_back(std::move(item));
        }
        setJson(res, 200, json{{"records", recordItems}, {"summaries", summaries}});
    });

    // 출결 등록·수정
    svr.Put("/api/attendance", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        json body;
        if (!parseJsonBody(req, body, res)) return;

        Attendance record;
        if (!readString(body, "studentId", record.studentId, res) ||
            !readString(body, "courseCode", record.courseCode, res) ||
            !readString(body, "semester", record.semester, res) ||
            !readString(body, "status", record.status, res)) return;
        const auto week = body.find("week");
        if (week == body.end() || !week->is_number_integer()) {
            setError(res, 400, "출결 정보가 올바르지 않습니다 (주차 1~16, 상태 출석·지각·결석·공결)");
            return;
        }
        record.week = week->get<int>();
        if (record.week < 1 || record.week > 16 ||
            (record.status != "출석" && record.status != "지각" &&
             record.status != "결석" && record.status != "공결")) {
            setError(res, 400, "출결 정보가 올바르지 않습니다 (주차 1~16, 상태 출석·지각·결석·공결)");
            return;
        }
        if (!checkCourseAccess(*account, gm, record.courseCode, res)) return;
        if (rejectPastSemester(gm, *account, record.semester, res)) return;
        if (!gm.findStudent(record.studentId)) {
            setError(res, 404, "없는 학생입니다: " + record.studentId);
            return;
        }
        if (!gm.findEnrollment(record.studentId, record.courseCode, record.semester)) {
            setError(res, 409, kNotEnrolled);
            return;
        }
        if (!record.isValid() || !gm.setAttendance(record)) {
            setError(res, 400, "출결 정보가 올바르지 않습니다 (주차 1~16, 상태 출석·지각·결석·공결)");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, kSaveFailed);
            return;
        }
        setJson(res, 200, json{
            {"studentId", record.studentId}, {"courseCode", record.courseCode},
            {"semester", record.semester}, {"week", record.week}, {"status", record.status}
        });
    });

    // 출결 삭제
    svr.Delete("/api/attendance", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const Account* account = requireRole(req, res, {"admin", "professor"});
        if (!account) return;
        const std::string studentId = req.get_param_value("studentId");
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        const std::string weekText = req.get_param_value("week");
        int week = 0;
        try {
            std::size_t used = 0;
            week = std::stoi(weekText, &used);
            if (used != weekText.size()) week = 0;
        } catch (...) {
            week = 0;
        }
        if (studentId.empty() || courseCode.empty() || semester.empty() || week < 1 || week > 16) {
            setError(res, 400, "studentId, courseCode, semester, week(1~16) 을 모두 지정해야 합니다");
            return;
        }
        if (!checkCourseAccess(*account, gm, courseCode, res)) return;
        if (rejectPastSemester(gm, *account, semester, res)) return;
        if (!gm.findStudent(studentId)) {
            setError(res, 404, "없는 학생입니다: " + studentId);
            return;
        }
        if (!gm.removeAttendance(studentId, courseCode, semester, week)) {
            setError(res, 404, "없는 출결 기록입니다");
            return;
        }
        if (!gm.save()) {
            setError(res, 500, kSaveFailed);
            return;
        }
        setJson(res, 200, json{{"deleted", true}});
    });

    if (bindAddress != "127.0.0.1") {
        std::cout << "경고: " << bindAddress
                  << " 에 열었습니다. 로컬 밖에서도 접속할 수 있으니 시연용 계정 비밀번호를 바꾸세요"
                  << std::endl;
    }
    std::cout << "http://" << (bindAddress == "127.0.0.1" ? "localhost" : bindAddress) << ":" << port
              << " 에서 실행 중 (종료: Ctrl+C)" << std::endl;
    if (!svr.listen(bindAddress, port)) {
        std::cerr << "포트 " << port << " 를 열 수 없습니다 (이미 사용 중인지 확인)"
                  << std::endl;
    }
}
