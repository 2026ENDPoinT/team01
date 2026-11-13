// 담당: C (원준)
// 브라우저(web/)와 GradeManager 를 연결하는 HTTP 서버 (cpp-httplib + nlohmann/json)
//
// API (요청/응답 본문은 JSON, 자세한 표는 README 참고)
//   권한: 로그인 없이 쓸 수 있는 것은 정적 파일(/)과 POST /api/login 뿐.
//         나머지 /api/* 는 로그인 필요 → 안 했으면 401 {"error":"로그인이 필요합니다"},
//         역할이 안 맞으면 403 {"error":"권한이 없습니다"}
//   역할: admin(관리자) / professor(교수) / student(학생). 아래 괄호가 그 라우트의 권한
//   교수의 담당 과목 = 과목의 professor 를 ',' 로 나눠 공백을 지운 이름 중 계정 name 과 같은 과목
//
//   POST   /api/login                 로그인 {username,password} (공개)
//   POST   /api/logout                로그아웃 (로그인)
//   GET    /api/me                    내 정보 {username,role,name,studentId} (로그인)
//   PUT    /api/me/password           내 비밀번호 변경 {currentPassword,newPassword} (로그인)
//   GET    /api/accounts              계정 목록        POST /api/accounts        계정 생성 (admin)
//   PUT    /api/accounts/:username    계정 수정        DELETE /api/accounts/:username 계정 삭제 (admin)
//
//   GET    /api/students              학생 목록 (admin 전체, professor 담당 과목 수강생만, student 본인만)
//   GET    /api/students/search?q=    학번·이름 검색 (위와 같은 범위)
//   POST   /api/students              학생 등록 → 201 학생 {id,name,major,year}        (admin)
//   PUT    /api/students/:id          학생 수정 → 200 학생    DELETE /api/students/:id  학생 삭제 → {"deleted":true} (admin)
//   GET    /api/courses               과목 목록 (로그인)
//   POST   /api/courses               과목 등록 → 201 과목 (목록과 같은 필드)        (admin)
//   PUT    /api/courses/:code         과목 수정 → 200 과목    DELETE /api/courses/:code 과목 삭제 → {"deleted":true} (admin)
//   GET    /api/courses/:code/posts   과목 게시물 목록 (semester·week·id 순)
//                                     (admin 전체, professor 담당 과목만, student 는 수강신청한 과목의 수강신청 학기 게시물만)
//   POST   /api/courses/:code/posts   게시물 추가 {semester,category,week,title,body,link,dueDate}
//   PUT    /api/courses/:code/posts/:id   게시물 수정 (같은 본문)    DELETE /api/courses/:code/posts/:id   게시물 삭제
//                                     (admin, professor 는 담당 과목만. 작성자·작성시각은 서버가 채움)
//   GET    /api/grades                성적 목록 (?studentId= ?courseCode= ?semester= 로 거르기)
//                                     (admin 전체, professor 담당 과목만, student 본인 것만)
//   POST   /api/grades                성적 등록 → 201      PUT /api/grades           성적 수정 → 200
//                                     본문 {studentId,courseCode,semester,score} 또는 score 대신 letter (A+ A0 B+ B0 C+ C0 D+ D0 F
//                                     → letterToScore 점수로 저장, 둘 다 있으면 score). 응답 {studentId,courseCode,semester,score,letter}
//   DELETE /api/grades?studentId=&courseCode=&semester=   성적 삭제 → {"deleted":true}
//                                     (admin, professor 는 담당 과목만. 등록은 수강신청이 있어야 함.
//                                      평가 항목이 있는 과목·학기는 등록·수정·삭제 모두 409)
//   GET    /api/stats/gpa/:id         학생 GPA {studentId,semester,gpa} (?semester=) (admin, student 본인)
//                                     평점 계산에 들어간 학점이 0 이면 "gpa": null
//   GET    /api/stats/summary/:id     학기별·누적 평점 {"semesters":[{semester,gpa,credits}],"gpa","credits"}
//                                     (credits 0 이면 "gpa": null) (admin, student 본인)
//   GET    /api/stats/course/:code    과목 평균/최고/최저 + latestSemester·latestEnrolled (로그인)
//   GET    /api/stats/ranking         석차표 (?semester=) (admin)
//   GET    /api/stats/warnings        학사경고 전체 (admin)
//   GET    /api/stats/warnings/:id    학생 학사경고 (admin, student 본인)
//   GET    /api/export/student/:id    학생 성적표 CSV 다운로드 (admin, student 본인)
//   GET    /api/export/course/:code   과목 성적표 CSV 다운로드 (admin, professor 담당 과목만)
//
//   GET    /api/enrollment/settings   수강신청 학기 {"semester"} (로그인)
//   PUT    /api/enrollment/settings   수강신청 학기 설정 (admin, ""이면 신청 마감)
//   GET    /api/enrollments           수강신청 목록 [{studentId,courseCode,semester,hasGrade}] (?studentId= ?courseCode= ?semester=)
//                                     (admin 전체, professor 담당 과목만, student 본인 것만)
//                                     hasGrade: 성적 또는 항목 점수가 있으면 true
//   GET    /api/enrollments/counts?semester=   과목별 신청 인원 (로그인)
//   POST   /api/enrollments           수강신청 {studentId,courseCode,semester} (admin, student 본인)
//   DELETE /api/enrollments?studentId=&courseCode=&semester=   수강신청 취소 (admin, student 본인)
//                                     성적 또는 항목 점수가 있으면 409
//
//   GET    /api/assessments?courseCode=&semester=   평가 항목 [{courseCode,semester,name,weight}] (로그인, 두 쿼리 필수)
//   POST   /api/assessments           항목 추가 {courseCode,semester,name,weight} → 201 항목
//   PUT    /api/assessments           항목 수정 {courseCode,semester,name,newName,weight} → 200 항목 (newName 없으면 이름 유지)
//   DELETE /api/assessments?courseCode=&semester=&name=   항목 삭제 (그 항목 점수도 삭제) → {"deleted":true}
//                                     (위 셋: admin, professor 는 담당 과목만)
//   GET    /api/assessment-scores?courseCode=&semester=   항목 점수 [{studentId,courseCode,semester,name,score}]
//                                     (admin·담당 교수는 두 쿼리 필수, student 는 쿼리 무시하고 본인 것 전부)
//   PUT    /api/assessment-scores     항목 점수 입력 {studentId,courseCode,semester,name,score} (있으면 수정) → 200 항목 점수
//   DELETE /api/assessment-scores?studentId=&courseCode=&semester=&name=   항목 점수 삭제 → {"deleted":true}
//                                     (위 둘: admin, professor 는 담당 과목만. 항목 점수가 바뀌면 성적을 다시 계산)
//   GET    /                          web/ 정적 파일 (공개)
//
// 세션: 로그인하면 쿠키 sid=<토큰 hex 64자> (HttpOnly, SameSite=Strict, 8시간) 를 줌.
//       세션은 메모리에만 있어서 서버를 다시 켜면 모두 로그아웃됨
//
// 실패 시: 400 (잘못된 값, 깨진 JSON, 칸이 빠졌거나 타입이 틀림) / 401 (로그인 안 함) / 403 (권한 없음) /
//          404 (없음) / 409 (중복, 정원 초과, 수강신청 규칙 위반, 평가 항목 규칙 위반) /
//          500 (파일 저장 실패) + {"error": "메시지"}
// 저장: 데이터를 바꾸는 라우트는 성공한 뒤 gm.save() (계정은 accounts->save()) 로 바로 CSV 에 씀
//
// 동시 접근: cpp-httplib 은 요청을 여러 스레드에서 동시에 처리함
//   → gm, accounts, sessions 를 쓰는 모든 라우트는 첫 줄에 std::lock_guard<std::mutex> lock(mtx); (server.cpp 예시 라우트 참고)
#pragma once

#include <chrono>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "account.h"
#include "grade_manager.h"

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

// 로그인 세션 (토큰은 Server::sessions 의 키)
struct Session {
    std::string username;
    std::chrono::steady_clock::time_point expires;  // 로그인 후 8시간, 연장하지 않음
};

class Server {
public:
    Server(GradeManager& gm, AccountStore& accounts, const std::string& webDir, int port);

    // 서버 실행 (Ctrl+C 로 종료할 때까지 반환하지 않음)
    void run();

private:
    GradeManager& gm;
    std::string webDir;
    int port;
    std::mutex mtx;  // gm 을 한 번에 한 요청만 쓰도록 잠금
    AccountStore* accounts = nullptr;  // 계정 (main이 소유)
    std::unordered_map<std::string, Session> sessions;  // 로그인 세션 (mtx 안에서만 접근)

    // 아래 도우미는 mtx 를 잡은 상태에서 호출 (server.cpp 정의)
    // 요청의 Cookie 헤더에서 sid 값을 꺼냄 (없으면 "")
    std::string sessionToken(const httplib::Request& req) const;
    // 로그인한 계정 (세션이 없거나 만료됐거나 계정이 사라졌으면 nullptr, 만료된 세션은 지움)
    const Account* currentAccount(const httplib::Request& req);
    // 로그인·역할 확인: 안 했으면 401, 역할이 roles 에 없으면 403 을 채우고 nullptr
    const Account* requireRole(const httplib::Request& req, httplib::Response& res,
                               std::initializer_list<const char*> roles);
    // 계정을 응답 JSON 문자열로 (해시·salt 제외, student 는 name 을 학생 이름으로 채움)
    std::string accountJson(const Account& account) const;
};
