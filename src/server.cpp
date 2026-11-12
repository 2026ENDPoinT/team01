#include "server.h"

#include <iostream>

#include "httplib.h"
#include "json.hpp"
#include "retake.h"

using json = nlohmann::json;

Server::Server(GradeManager& gm, const std::string& webDir, int port)
    : gm(gm), webDir(webDir), port(port) {}

void Server::run() {
    httplib::Server svr;

    // web/ 폴더를 / 로 서비스 (index.html, style.css, app.js)
    svr.set_mount_point("/", webDir);

    // 예시 라우트: 학생 목록
    svr.Get("/api/students", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);  // 모든 /api 라우트 첫 줄에 넣기 (server.h 참고)
        json arr = json::array();
        for (const Student& s : gm.getStudents()) {
            arr.push_back({{"id", s.id}, {"name", s.name}, {"major", s.major}, {"year", s.year}});
        }
        res.set_content(arr.dump(), "application/json");
    });

    // 성적 등록 / 수정: 본문 {"studentId", "courseCode", "semester", "score"}
    //   실패 시 GradeError::kind → 400 / 404 / 409, {"error": 이유}
    auto handleGrade = [this](const httplib::Request& req, httplib::Response& res, bool isUpdate) {
        std::lock_guard<std::mutex> lock(mtx);
        const json body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object() ||
            !body.contains("studentId") || !body["studentId"].is_string() ||
            !body.contains("courseCode") || !body["courseCode"].is_string() ||
            !body.contains("semester") || !body["semester"].is_string() ||
            !body.contains("score") || !body["score"].is_number()) {
            res.status = 400;
            res.set_content(json{{"error", "studentId, courseCode, semester, score 가 필요합니다."}}.dump(),
                            "application/json");
            return;
        }

        Grade grade;
        grade.studentId = body["studentId"].get<std::string>();
        grade.courseCode = body["courseCode"].get<std::string>();
        grade.semester = body["semester"].get<std::string>();
        grade.score = body["score"].get<double>();

        GradeError error;
        const bool ok = isUpdate ? gm.updateGrade(grade, &error) : gm.addGrade(grade, &error);
        if (!ok) {
            switch (error.kind) {
                case GradeError::NotFound: res.status = 404; break;
                case GradeError::Duplicate: res.status = 409; break;
                default: res.status = 400; break;
            }
            res.set_content(json{{"error", error.message}}.dump(), "application/json");
            return;
        }

        gm.save();
        res.status = isUpdate ? 200 : 201;
        res.set_content(json{{"studentId", grade.studentId},
                             {"courseCode", grade.courseCode},
                             {"semester", grade.semester},
                             {"score", grade.score},
                             {"letter", Retake::officialLetter(grade, gm.getGrades())}}
                            .dump(),
                        "application/json");
    };
    svr.Post("/api/grades", [handleGrade](const httplib::Request& req, httplib::Response& res) {
        handleGrade(req, res, false);
    });
    svr.Put("/api/grades", [handleGrade](const httplib::Request& req, httplib::Response& res) {
        handleGrade(req, res, true);
    });

    // TODO(C): server.h 에 적힌 나머지 API 라우트 추가

    std::cout << "http://localhost:" << port << " 에서 실행 중 (종료: Ctrl+C)" << std::endl;
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "포트 " << port << " 를 열 수 없습니다 (이미 사용 중인지 확인)" << std::endl;
    }
}
