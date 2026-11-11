// 담당: C (원준)
// 브라우저(web/)와 GradeManager 를 연결하는 HTTP 서버 (cpp-httplib + nlohmann/json)
//
// API (요청/응답 본문은 JSON, 자세한 표는 README 참고)
//   GET    /api/students              학생 목록        POST /api/students        학생 등록
//   GET    /api/students/search?q=    학번·이름 검색
//   PUT    /api/students/:id          학생 수정        DELETE /api/students/:id  학생 삭제
//   GET    /api/courses               과목 목록        POST /api/courses         과목 등록
//   PUT    /api/courses/:code         과목 수정        DELETE /api/courses/:code 과목 삭제
//   GET    /api/grades                성적 목록 (?studentId= ?courseCode= ?semester= 로 거르기)
//   POST   /api/grades                성적 등록        PUT /api/grades           성적 수정
//   DELETE /api/grades?studentId=&courseCode=&semester=   성적 삭제
//   GET    /api/stats/gpa/:id         학생 GPA (?semester=)
//   GET    /api/stats/course/:code    과목 평균/최고/최저
//   GET    /api/stats/ranking         석차표 (?semester=)
//   GET    /api/export/student/:id    학생 성적표 CSV 다운로드
//   GET    /api/export/course/:code   과목 성적표 CSV 다운로드
//   GET    /                          web/ 정적 파일
//
// 실패 시: 400 (잘못된 값) / 404 (없음) / 409 (중복, 정원 초과) + {"error": "메시지"}
//
// 동시 접근: cpp-httplib 은 요청을 여러 스레드에서 동시에 처리함
//   → gm 을 쓰는 모든 라우트는 첫 줄에 std::lock_guard<std::mutex> lock(mtx); (server.cpp 예시 라우트 참고)
#pragma once

#include <mutex>
#include <string>

#include "grade_manager.h"

class Server {
public:
    Server(GradeManager& gm, const std::string& webDir, int port);

    // 서버 실행 (Ctrl+C 로 종료할 때까지 반환하지 않음)
    void run();

private:
    GradeManager& gm;
    std::string webDir;
    int port;
    std::mutex mtx;  // gm 을 한 번에 한 요청만 쓰도록 잠금
};
