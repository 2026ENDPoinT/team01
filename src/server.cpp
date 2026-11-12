#include "server.h"
#include "statistics.h"
#include "retake.h"

#include <algorithm>
#include <iostream>
#include <algorithm>

#include "httplib.h"
#include "json.hpp"
#include "report_exporter.h"
#include "retake.h"
#include "statistics.h"

using json = nlohmann::json;

/*
CSV 형식으로 만들어줌

홍길동       → "홍길동"
홍길동, 학생 → "홍길동, 학생"
A"B          → "A""B"

*/
std::string escapeCSV(const std::string& value){
    std::string result = "\"";

    for(char c:value){
        if(c=='"'){
            result+="\"\"";
        }
        else{
            result+=c;
        }
    }

    result += "\"";
    return result;
}


Server::Server(GradeManager& gm, const std::string& webDir, int port)
    : gm(gm), webDir(webDir), port(port) {}

void Server::run() {
    httplib::Server svr;

    // web/ 폴더를 / 로 서비스 (index.html, style.css, app.js)
    svr.set_mount_point("/", webDir);


    // TODO(C): server.h 에 적힌 나머지 API 라우트 추가

//GET 사용

    // 학생 목록
    svr.Get("/api/students", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const Student& s : gm.getStudents()) {
            arr.push_back({
                {"id", s.id},
                {"name", s.name},
                {"major", s.major},
                {"year", s.year}});
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });


    // 학번, 이름 검색
    svr.Get("/api/students/search", [this](const httplib::Request& req, httplib::Response& res){

        //검색어가 비어있는 경우 -> 에러
        if(req.get_param_value("q").empty()){
            res.status = 400;
            res.set_content("검색 키워드(학번 or 이름)를 입력해주세요", "text/plain; charset=utf-8");
            return;
        }

        auto students = gm.searchStudents(req.get_param_value("q"));

        json arr = json::array();
        for(const Student& s:students){
            arr.push_back({
                {"id", s.id},
                {"name", s.name},
                {"major", s.major},
                {"year", s.year}
            });
        }

        if(arr.empty()){
            res.status = 404;
            res.set_content("등록되지 않은 학생입니다", "text/plain; charset=utf-8");
        }
        else{
            res.set_content(arr.dump(), "application/json; charset=utf-8");
        }
    });


    // 과목 목록
    svr.Get("/api/courses", [this](const httplib::Request&, httplib::Response& res){
        auto courses = gm.getCourses();

        json arr = json::array();
        for(const Course& c:courses){
            arr.push_back({
                {"code", c.code},
                {"name", c.name},
                {"credit", c.credit},
                {"professor", c.professor},
                {"category", c.category},
                {"area", c.area},
                {"schedule", c.schedule},
                {"language", c.language},
                {"capacity", c.capacity},
                {"remote", c.remote}
            });
        }

        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });


    // 성적 목록
    svr.Get("/api/grades", [this](const httplib::Request& req, httplib::Response& res){
        auto grades = gm.getGrades();
        auto studentId = req.get_param_value("studentId");
        auto courseCode = req.get_param_value("courseCode");
        auto semester = req.get_param_value("semester");

        json arr = json::array();
        for(const Grade& g:grades){
            if( (studentId.empty()||g.studentId == studentId) &&
                (courseCode.empty() || g.courseCode == courseCode) &&
                (semester.empty()||g.semester == semester)){
                arr.push_back({
                    {"studentId", g.studentId},
                    {"courseCode", g.courseCode},
                    {"semester", g.semester},
                    {"score", g.score},
                    {"letter", Retake::officialLetter(g, grades)}
                });
            }
        }

        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });


    // 학생 GPA
    svr.Get("/api/stats/gpa/:id", [this](const httplib::Request& req, httplib::Response& res)
    {
        //:id -> 경로 파라미터 사용 -> id 입력 안 할 경우 -> 그건 프로그램이 자동적으로 오류 표시 해줌
        auto gpa = Statistics::calcGPA(gm, req.path_params.at("id"), req.get_param_value("semester"));
        json result = {{"GPA", gpa}};
        res.set_content(result.dump(), "application/json; charset=utf-8");
    });


    //과목 평균/최고/최저
    svr.Get("/api/stats/course/:code", [this](const httplib::Request& req, httplib::Response& res)
    {
        auto courseStats = Statistics::courseStats(gm, req.path_params.at("code"));
        json result = {{"average", courseStats.average},
                        {"max", courseStats.max},
                        {"min", courseStats.min}};
        res.set_content(result.dump(), "application/json; charset=utf-8");
    });


    //석차표
    svr.Get("/api/stats/ranking", [this](const httplib::Request& req, httplib::Response& res)
    {
        auto semester = req.get_param_value("semester");
        json arr = json::array();

        for(auto rankEntry:Statistics::ranking(gm, semester)){
            arr.push_back({
                {"rank", rankEntry.rank},
                {"studentId", rankEntry.studentId},
                {"name", rankEntry.name},
                {"gpa", rankEntry.gpa}
            });
        }
        res.set_content(arr.dump(), "application/json; charset=utf-8");
    });


    //학생 성적표 CSV 다운로드
    svr.Get("/api/export/student/:id", [this](const httplib::Request& req, httplib::Response& res)
    {
        // 찾으려는 학생 id
        std::string id = req.path_params.at("id");

        auto students = gm.getStudents();

        // 학생 찾기
        auto it = std::find_if(students.begin(), students.end(),
            [&id](const Student& s){
                return s.id == id;
            });

        // 없을시 404 에러
        if (it == students.end()){
            res.status = 404;
            res.set_content("해당 학번의 학생을 찾지 못했습니다", "text/plain; charset=utf-8");
            return;
        }

        // CSV 생성
        std::string csv;
        csv += "\xEF\xBB\xBF";  //UTF-8 인코딩임을 엑셀에게 알려줌
        csv += "학생ID,이름,과목코드,학기,점수,등급\r\n";

        auto grades = gm.getGrades();

        for (const auto& grade : grades) {
            if (grade.studentId == id) {
                std::string letter = Retake::officialLetter(grade, grades);

                // 2018년 이전 입학생은 삭제된 성적 행을 성적표에서 제외
                if(letter == "R" && !Retake::admittedSince2018(id)){
                    continue;
                }

                csv += escapeCSV(id) + ",";
                csv += escapeCSV(it->name) + ",";
                csv += escapeCSV(grade.courseCode) + ",";
                csv += escapeCSV(grade.semester) + ",";
                csv += std::to_string(grade.score) + ",";
                csv += escapeCSV(letter) + "\r\n";
            }
        }

        // 다운로드 응답
        res.set_header(
            "Content-Disposition",
            "attachment; filename=\"student_grades.csv\""
        );

        res.set_content(csv, "text/csv; charset=utf-8");
    });


    //과목 성적표 CSV 다운로드
    svr.Get("/api/export/course/:code", [this](const httplib::Request& req, httplib::Response& res)
    {
        //찾고자 하는 과목 코드
        std::string code = req.path_params.at("code");

        std::string csv;
        csv += "\xEF\xBB\xBF";  //UTF-8 인코딩임을 엑셀에게 알려줌
        csv += "학생ID,과목코드,학기,점수,등급\r\n";

        auto grades = gm.getGrades();
        auto activeGrades = Retake::activeGrades(grades);

        for(const auto& grade:activeGrades){
            if(grade.courseCode == code){
                csv += escapeCSV(grade.studentId) + ",";
                csv += escapeCSV(grade.courseCode) + ",";
                csv += escapeCSV(grade.semester) + ",";
                csv += std::to_string(grade.score) + ",";
                csv += escapeCSV(Retake::officialLetter(grade, grades)) + "\r\n";
            }
        }

        // 다운로드 응답
        res.set_header(
            "Content-Disposition",
            "attachment; filename=\"course_grades.csv\""
        );

        res.set_content(csv, "text/csv; charset=utf-8");
    });


//POST 사용

    //학생 등록
    svr.Post("/api/students", [this](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body);

        Student student;
        student.id = body.at("id");
        student.name = body.at("name");
        student.major = body.at("major");
        student.year = body.at("year");

        if(!gm.addStudent(student)){
            res.status = 409;
            res.set_content(json{
                {"error", "학생 등록 실패 (이미 등록된 학번입니다)"}
            }.dump(), "application/json; charset=utf-8");
        }
        else{
            res.set_content("학생 정보가 등록되었습니다", "text/plain; charset=utf-8");
        }
    });


    //과목 등록
    svr.Post("/api/courses", [this](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body);

        Course course;
        course.code = body.at("code");
        course.name = body.at("name");
        course.credit = body.at("credit");
        course.professor = body.at("professor");
        course.category = body.value("category", "전공선택");
        course.area = body.value("area", "");
        course.schedule = body.value("schedule", "");
        course.language = body.value("language", "한국어전용");
        course.capacity = body.value("capacity", 0);
        course.remote = body.value("remote", false);

        if(!gm.addCourse(course)){
            res.status = 409;
            res.set_content(json{
                {"error", "과목 등록 실패 (이미 등록된 과목입니다)"}
            }.dump(), "application/json; charset=utf-8");
        }
        else{
            res.set_content("과목 정보가 등록되었습니다", "text/plain; charset=utf-8");
        }
    });


    //성적 등록
    svr.Post("/api/grades", [this](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body);

        Grade grade;
        grade.studentId = body.at("studentId");
        grade.courseCode = body.at("courseCode");
        grade.semester = body.at("semester");
        grade.score = body.at("score");

        if(!gm.addGrade(grade)){
            res.status = 409;
            res.set_content(json{
                {"error", "성적 등록 실패 (존재하지 않는 학생/과목이거나 이미 추가된 학생/과목입니다)"}
            }.dump(), "application/json; charset=utf-8");
        }
        else{
            res.set_content("성적 정보가 등록되었습니다", "text/plain; charset=utf-8");
        }
    });

// PUT 사용

    // 학생 수정
    svr.Put("/api/students/:id", [this](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body);
        Student student;
        student.id = req.path_params.at("id");
        student.name = body.at("name");
        student.major = body.at("major");
        student.year = body.at("year");

        if(!gm.updateStudent(student.id, student)){
            res.status = 404;
            res.set_content(
                json{{"error", "수정할 학생을 찾을 수 없습니다."}}.dump(),
                "application/json; charset=utf-8"
            );
            return;
        }

        res.set_content("학생 정보가 수정되었습니다", "text/plain; charset=utf-8");
    });

    //과목 수정
    svr.Put("/api/courses/:code", [this](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body);
        Course course;
        course.code = req.path_params.at("code");
        course.name = body.at("name");
        course.credit = body.at("credit");
        course.professor = body.at("professor");
        course.category = body.value("category", "전공선택");
        course.area = body.value("area", "");
        course.schedule = body.value("schedule", "");
        course.language = body.value("language", "한국어전용");
        course.capacity = body.value("capacity", 0);
        course.remote = body.value("remote", false);

        if(!gm.updateCourse(course.code, course)){
            res.status = 404;
            res.set_content(
                json{{"error", "수정할 과목을 찾을 수 없습니다."}}.dump(),
                "application/json; charset=utf-8"
            );
            return;
        }

        res.set_content("과목 정보가 수정되었습니다", "text/plain; charset=utf-8");
    });

    //성적 수정
    svr.Put("/api/grades", [this](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body);
        Grade grade;
        grade.studentId = body.at("studentId");
        grade.courseCode = body.at("courseCode");
        grade.semester = body.at("semester");
        grade.score = body.at("score");

        if(!gm.updateGrade(grade)){
            res.status = 404;
            res.set_content(
                json{{"error", "수정할 성적을 찾을 수 없습니다."}}.dump(),
                "application/json; charset=utf-8"
            );
            return;
        }

        res.set_content("성적 정보가 수정되었습니다", "text/plain; charset=utf-8");
    });

//DELETE

    //학생 삭제
    svr.Delete("/api/students/:id",[this](const httplib::Request& req, httplib::Response& res){
        if(!gm.removeStudent(req.path_params.at("id"))){
            res.status = 404;
            res.set_content(
                json{{"error", "삭제할 학생을 찾을 수 없습니다."}}.dump(),
                "application/json; charset=utf-8"
            );
            return;
        }

        res.set_content("학생 정보가 삭제되었습니다.", "text/plain; charset=utf-8");

    });

    //과목 삭제
    svr.Delete("/api/courses/:code",[this](const httplib::Request& req, httplib::Response& res){
        if(!gm.removeCourse(req.path_params.at("code"))){
            res.status = 404;
            res.set_content(
                json{{"error", "삭제할 과목을 찾을 수 없습니다."}}.dump(),
                "application/json; charset=utf-8"
            );
            return;
        }

        res.set_content("과목 정보가 삭제되었습니다.", "text/plain; charset=utf-8");
    });

    //성적 삭제
    svr.Delete("/api/grades",[this](const httplib::Request& req, httplib::Response& res){
        if(!gm.removeGrade(req.get_param_value("studentId"),
                       req.get_param_value("courseCode"),
                       req.get_param_value("semester") )){
            res.status = 404;
            res.set_content(
                json{{"error", "삭제할 성적을 찾을 수 없습니다."}}.dump(),
                "application/json; charset=utf-8"
            );
            return;
        }

        res.set_content("성적 정보가 삭제되었습니다.", "text/plain; charset=utf-8");
    });


    std::cout << "http://localhost:" << port << " 에서 실행 중 (종료: Ctrl+C)" << std::endl;
    // 이 PC 에서만 접속 가능 (같은 네트워크의 다른 기기에서는 접속 불가)
    if (!svr.listen("127.0.0.1", port)) {
        std::cerr << "포트 " << port << " 를 열 수 없습니다 (이미 사용 중인지 확인)" << std::endl;
    }
}