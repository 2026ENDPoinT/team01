// 담당: 조장 (Eno74)
// 변경 번호 판정 (server.h changesData): 어떤 요청이 성공했을 때 X-Data-Version 번호를 올리는지
#include <string>

#include "check.h"
#include "server.h"

TEST_CASE(changesData_successful_writes_bump) {
    CHECK(changesData("POST", "/api/students", 201));
    CHECK(changesData("PUT", "/api/students/25103008", 200));
    CHECK(changesData("DELETE", "/api/grades", 200));
    CHECK(changesData("PUT", "/api/me/password", 200));  // 계정 파일이 바뀜
    CHECK(changesData("POST", "/api/accounts", 201));
    CHECK(changesData("PUT", "/api/assessments", 200));
    CHECK(changesData("POST", "/api/stats/simulate", 200));  // 접두어 '/api/stats/simulate/' 와 다름
    CHECK(changesData("POST", "/api/assessments/batch/x", 200));  // batch 는 정확히 같은 경로만 제외
    CHECK(changesData("PUT", "/api/enrollment/settings", 399));  // 경계: 399 는 성공
}

TEST_CASE(changesData_failed_or_read_requests_do_not_bump) {
    CHECK(!changesData("POST", "/api/students", 400));  // 경계: 400 부터 실패
    CHECK(!changesData("PUT", "/api/students/1", 404));
    CHECK(!changesData("DELETE", "/api/grades", 500));  // 저장 실패
    CHECK(!changesData("POST", "/api/students", -1));  // 상태를 아직 못 정함
    CHECK(!changesData("POST", "/api/students", 101));
    CHECK(!changesData("GET", "/api/students", 200));
    CHECK(!changesData("HEAD", "/api/students", 200));
    CHECK(!changesData("PATCH", "/api/students/1", 200));
    CHECK(!changesData("", "", 200));  // 요청을 못 읽은 응답
}

TEST_CASE(changesData_excluded_routes) {
    CHECK(!changesData("POST", "/api/login", 200));
    CHECK(!changesData("POST", "/api/logout", 200));
    CHECK(!changesData("POST", "/api/stats/simulate/retake", 200));
    CHECK(!changesData("POST", "/api/assessments/batch", 200));
}

TEST_CASE(changesData_only_api_paths) {
    CHECK(!changesData("POST", "/", 200));
    CHECK(!changesData("POST", "/index.html", 200));
    CHECK(!changesData("POST", "/api", 200));  // '/api/' 로 시작하지 않음
    CHECK(!changesData("POST", "/apix/students", 200));
}
