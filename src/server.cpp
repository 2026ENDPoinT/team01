#include "server.h"

#include <iostream>

#include "httplib.h"
#include "json.hpp"

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

    // TODO(C): server.h 에 적힌 나머지 API 라우트 추가

    std::cout << "http://localhost:" << port << " 에서 실행 중 (종료: Ctrl+C)" << std::endl;
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "포트 " << port << " 를 열 수 없습니다 (이미 사용 중인지 확인)" << std::endl;
    }
}
