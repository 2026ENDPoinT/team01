#include <iostream>
#include <string>

#include "account.h"
#include "grade_manager.h"
#include "server.h"

// 사용법: GradeManager [포트]   (기본 8080)
int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc >= 2) {
        port = std::stoi(argv[1]);
    }

    const std::string root = PROJECT_ROOT;  // CMakeLists.txt 에서 넘겨준 프로젝트 경로

    GradeManager gm(root + "/data");
    if (!gm.load()) {
        std::cerr << "경고: 데이터를 불러오지 못했습니다. 빈 상태로 시작합니다." << std::endl;
    }

    AccountStore accounts(root + "/data");
    if (!accounts.load()) {
        std::cerr << "경고: 계정 데이터를 불러오지 못했습니다. 빈 상태로 시작합니다." << std::endl;
    }
    if (accounts.ensureAdmin()) {
        if (!accounts.save()) {
            std::cerr << "경고: 계정 파일을 저장하지 못했습니다." << std::endl;
        }
        std::cout << "관리자 계정 admin / admin1234 를 만들었습니다. 로그인 후 비밀번호를 바꾸세요" << std::endl;
    }

    Server server(gm, accounts, root + "/web", port);
    server.run();
    return 0;
}
