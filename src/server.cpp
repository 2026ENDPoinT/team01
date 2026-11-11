#include "server.h"

#include <iostream>

#include "httplib.h"
#include "json.hpp"
#include "report_exporter.h"
#include "retake.h"
#include "statistics.h"

using json = nlohmann::json;

namespace {

void sendJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void fail(httplib::Response& res, int status, const std::string& message) {
    sendJson(res, {{"error", message}}, status);
}

// 본문을 JSON 객체로 읽음. 실패하면 400 을 채우고 false
bool parseBody(const httplib::Request& req, httplib::Response& res, json& out) {
    out = json::parse(req.body, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        fail(res, 400, "요청 본문이 올바른 JSON 객체가 아닙니다");
        return false;
    }
    return true;
}

// 필드가 문자열이어야 함. 없거나 타입이 틀리면 400 을 채우고 false
bool readString(const json& body, const char* key, httplib::Response& res, std::string& out) {
    auto it = body.find(key);
    if (it == body.end() || !it->is_string()) {
        fail(res, 400, std::string("'") + key + "' 값이 없거나 문자열이 아닙니다");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool readInt(const json& body, const char* key, httplib::Response& res, int& out) {
    auto it = body.find(key);
    if (it == body.end() || !it->is_number_integer()) {
        fail(res, 400, std::string("'") + key + "' 값이 없거나 정수가 아닙니다");
        return false;
    }
    out = it->get<int>();
    return true;
}

bool readNumber(const json& body, const char* key, httplib::Response& res, double& out) {
    auto it = body.find(key);
    if (it == body.end() || !it->is_number()) {
        fail(res, 400, std::string("'") + key + "' 값이 없거나 숫자가 아닙니다");
        return false;
    }
    out = it->get<double>();
    return true;
}

bool readStudent(const httplib::Request& req, httplib::Response& res, Student& s) {
    json body;
    return parseBody(req, res, body) && readString(body, "id", res, s.id) &&
           readString(body, "name", res, s.name) && readString(body, "major", res, s.major) &&
           readInt(body, "year", res, s.year);
}

bool readCourse(const httplib::Request& req, httplib::Response& res, Course& c) {
    json body;
    return parseBody(req, res, body) && readString(body, "code", res, c.code) &&
           readString(body, "name", res, c.name) && readInt(body, "credit", res, c.credit) &&
           readString(body, "professor", res, c.professor);
}

bool readGrade(const httplib::Request& req, httplib::Response& res, Grade& g) {
    json body;
    return parseBody(req, res, body) && readString(body, "studentId", res, g.studentId) &&
           readString(body, "courseCode", res, g.courseCode) &&
           readString(body, "semester", res, g.semester) && readNumber(body, "score", res, g.score);
}

json studentJson(const Student& s) {
    return {{"id", s.id}, {"name", s.name}, {"major", s.major}, {"year", s.year}};
}

json courseJson(const Course& c) {
    return {{"code", c.code}, {"name", c.name}, {"credit", c.credit}, {"professor", c.professor}};
}

// letter 는 재수강 규칙을 적용한 등급
json gradeJson(const GradeManager& gm, const Grade& g) {
    return {{"studentId", g.studentId},
            {"courseCode", g.courseCode},
            {"semester", g.semester},
            {"score", g.score},
            {"letter", Retake::officialLetter(g, gm.getGrades())}};
}

bool sameKey(const Grade& g, const std::string& studentId, const std::string& courseCode,
             const std::string& semester) {
    return g.studentId == studentId && g.courseCode == courseCode && g.semester == semester;
}

bool gradeExists(const GradeManager& gm, const std::string& studentId, const std::string& courseCode,
                 const std::string& semester) {
    for (const Grade& g : gm.getGrades()) {
        if (sameKey(g, studentId, courseCode, semester)) return true;
    }
    return false;
}

// 변경이 성공한 뒤 CSV 에 저장. 저장 실패는 500
void saveOrFail(GradeManager& gm, httplib::Response& res, const json& okBody, int okStatus = 200) {
    if (!gm.save()) {
        fail(res, 500, "변경은 적용됐지만 CSV 파일 저장에 실패했습니다");
        return;
    }
    sendJson(res, okBody, okStatus);
}

void sendCsv(httplib::Response& res, const std::string& csv, const std::string& filename) {
    if (csv.empty()) {
        fail(res, 404, "내보낼 대상을 찾을 수 없습니다");
        return;
    }
    res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    res.set_content(csv, "text/csv; charset=utf-8");
}

}  // namespace

Server::Server(GradeManager& gm, const std::string& webDir, int port)
    : gm(gm), webDir(webDir), port(port) {}

void Server::run() {
    httplib::Server svr;

    // web/ 폴더를 / 로 서비스 (index.html, style.css, app.js)
    svr.set_mount_point("/", webDir);

    // 처리 중 예외가 나도 서버가 죽지 않고 JSON 으로 응답
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        std::string msg = "서버 내부 오류";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            msg += std::string(": ") + e.what();
        } catch (...) {
        }
        fail(res, 500, msg);
    });

    // ───────── 학생 ─────────

    svr.Get("/api/students", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        json arr = json::array();
        for (const Student& s : gm.getStudents()) arr.push_back(studentJson(s));
        sendJson(res, arr);
    });

    svr.Get("/api/students/search", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        json arr = json::array();
        for (const Student& s : gm.searchStudents(req.get_param_value("q"))) arr.push_back(studentJson(s));
        sendJson(res, arr);
    });

    svr.Post("/api/students", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        Student s;
        if (!readStudent(req, res, s)) return;
        if (!s.isValid()) return fail(res, 400, "학생 정보가 올바르지 않습니다 (학번 숫자 8자리, 이름 필수, 학년 1~4)");
        if (gm.findStudent(s.id)) return fail(res, 409, "이미 등록된 학번입니다: " + s.id);
        if (!gm.addStudent(s)) return fail(res, 400, "학생을 등록할 수 없습니다");
        saveOrFail(gm, res, studentJson(s), 201);
    });

    svr.Put("/api/students/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string id = req.path_params.at("id");
        Student s;
        if (!readStudent(req, res, s)) return;
        if (!gm.findStudent(id)) return fail(res, 404, "없는 학생입니다: " + id);
        if (!s.isValid()) return fail(res, 400, "학생 정보가 올바르지 않습니다 (학번 숫자 8자리, 이름 필수, 학년 1~4)");
        if (s.id != id && gm.findStudent(s.id)) return fail(res, 409, "이미 등록된 학번입니다: " + s.id);
        if (!gm.updateStudent(id, s)) return fail(res, 400, "학생을 수정할 수 없습니다");
        saveOrFail(gm, res, studentJson(s));
    });

    svr.Delete("/api/students/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string id = req.path_params.at("id");
        if (!gm.removeStudent(id)) return fail(res, 404, "없는 학생입니다: " + id);
        saveOrFail(gm, res, {{"deleted", id}});
    });

    // ───────── 과목 ─────────

    svr.Get("/api/courses", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        json arr = json::array();
        for (const Course& c : gm.getCourses()) arr.push_back(courseJson(c));
        sendJson(res, arr);
    });

    svr.Post("/api/courses", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        Course c;
        if (!readCourse(req, res, c)) return;
        if (!c.isValid()) return fail(res, 400, "과목 정보가 올바르지 않습니다 (코드·과목명 필수, 학점 1~4)");
        if (gm.findCourse(c.code)) return fail(res, 409, "이미 등록된 과목 코드입니다: " + c.code);
        if (!gm.addCourse(c)) return fail(res, 400, "과목을 등록할 수 없습니다");
        saveOrFail(gm, res, courseJson(c), 201);
    });

    svr.Put("/api/courses/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string code = req.path_params.at("code");
        Course c;
        if (!readCourse(req, res, c)) return;
        if (!gm.findCourse(code)) return fail(res, 404, "없는 과목입니다: " + code);
        if (!c.isValid()) return fail(res, 400, "과목 정보가 올바르지 않습니다 (코드·과목명 필수, 학점 1~4)");
        if (c.code != code && gm.findCourse(c.code)) return fail(res, 409, "이미 등록된 과목 코드입니다: " + c.code);
        if (!gm.updateCourse(code, c)) return fail(res, 400, "과목을 수정할 수 없습니다");
        saveOrFail(gm, res, courseJson(c));
    });

    svr.Delete("/api/courses/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string code = req.path_params.at("code");
        if (!gm.removeCourse(code)) return fail(res, 404, "없는 과목입니다: " + code);
        saveOrFail(gm, res, {{"deleted", code}});
    });

    // ───────── 성적 ─────────

    svr.Get("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const bool byStudent = req.has_param("studentId");
        const bool byCourse = req.has_param("courseCode");
        const bool bySemester = req.has_param("semester");
        json arr = json::array();
        for (const Grade& g : gm.getGrades()) {
            if (byStudent && g.studentId != req.get_param_value("studentId")) continue;
            if (byCourse && g.courseCode != req.get_param_value("courseCode")) continue;
            if (bySemester && g.semester != req.get_param_value("semester")) continue;
            arr.push_back(gradeJson(gm, g));
        }
        sendJson(res, arr);
    });

    svr.Post("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        Grade g;
        if (!readGrade(req, res, g)) return;
        if (!g.isValid()) return fail(res, 400, "성적 정보가 올바르지 않습니다 (학번·과목코드·학기 형식, 점수 0~100)");
        if (!gm.findStudent(g.studentId)) return fail(res, 404, "없는 학생입니다: " + g.studentId);
        if (!gm.findCourse(g.courseCode)) return fail(res, 404, "없는 과목입니다: " + g.courseCode);
        if (gradeExists(gm, g.studentId, g.courseCode, g.semester)) {
            return fail(res, 409, "이미 등록된 성적입니다 (같은 학생·과목·학기)");
        }
        if (!gm.addGrade(g)) return fail(res, 400, "성적을 등록할 수 없습니다");
        saveOrFail(gm, res, gradeJson(gm, g), 201);
    });

    svr.Put("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        Grade g;
        if (!readGrade(req, res, g)) return;
        if (!g.isValid()) return fail(res, 400, "성적 정보가 올바르지 않습니다 (학번·과목코드·학기 형식, 점수 0~100)");
        if (!gradeExists(gm, g.studentId, g.courseCode, g.semester)) return fail(res, 404, "없는 성적입니다");
        if (!gm.updateGrade(g)) return fail(res, 400, "성적을 수정할 수 없습니다");
        saveOrFail(gm, res, gradeJson(gm, g));
    });

    svr.Delete("/api/grades", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!req.has_param("studentId") || !req.has_param("courseCode") || !req.has_param("semester")) {
            return fail(res, 400, "studentId, courseCode, semester 를 모두 지정해야 합니다");
        }
        const std::string studentId = req.get_param_value("studentId");
        const std::string courseCode = req.get_param_value("courseCode");
        const std::string semester = req.get_param_value("semester");
        if (!gm.removeGrade(studentId, courseCode, semester)) return fail(res, 404, "없는 성적입니다");
        saveOrFail(gm, res, {{"deleted", true}});
    });

    // ───────── 통계 ─────────

    svr.Get("/api/stats/gpa/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string id = req.path_params.at("id");
        if (!gm.findStudent(id)) return fail(res, 404, "없는 학생입니다: " + id);
        const std::string semester = req.get_param_value("semester");
        sendJson(res, {{"studentId", id}, {"semester", semester}, {"gpa", Statistics::calcGPA(gm, id, semester)}});
    });

    svr.Get("/api/stats/course/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string code = req.path_params.at("code");
        if (!gm.findCourse(code)) return fail(res, 404, "없는 과목입니다: " + code);
        const Statistics::CourseStats st = Statistics::courseStats(gm, code);
        sendJson(res, {{"courseCode", st.courseCode},
                       {"count", st.count},
                       {"average", st.average},
                       {"max", st.max},
                       {"min", st.min}});
    });

    svr.Get("/api/stats/ranking", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        json arr = json::array();
        for (const Statistics::RankEntry& r : Statistics::ranking(gm, req.get_param_value("semester"))) {
            arr.push_back({{"rank", r.rank}, {"studentId", r.studentId}, {"name", r.name}, {"gpa", r.gpa}});
        }
        sendJson(res, arr);
    });

    // ───────── 성적표 CSV ─────────

    svr.Get("/api/export/student/:id", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string id = req.path_params.at("id");
        sendCsv(res, ReportExporter::studentReport(gm, id), "student_" + id + ".csv");
    });

    svr.Get("/api/export/course/:code", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        const std::string code = req.path_params.at("code");
        sendCsv(res, ReportExporter::courseReport(gm, code), "course_" + code + ".csv");
    });

    std::cout << "http://localhost:" << port << " 에서 실행 중 (종료: Ctrl+C)" << std::endl;
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "포트 " << port << " 를 열 수 없습니다 (이미 사용 중인지 확인)" << std::endl;
    }
}
