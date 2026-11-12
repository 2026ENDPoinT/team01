// 담당: 조장 (Eno74)
// 등록된 TEST_CASE 를 모두 실행하고, 실패가 하나라도 있으면 0 이 아닌 값으로 끝냄 (ctest 가 실패로 봄)
#include <exception>
#include <iostream>

#include "check.h"

int main() {
    int failedTests = 0;
    for (const check::TestCase& test : check::registry()) {
        const int before = check::failures();
        try {
            test.fn();
        } catch (const std::exception& e) {
            check::reportFailure(__FILE__, __LINE__, test.name, std::string("예외: ") + e.what());
        } catch (...) {
            check::reportFailure(__FILE__, __LINE__, test.name, "알 수 없는 예외");
        }
        const bool ok = check::failures() == before;
        if (!ok) {
            failedTests++;
        }
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << test.name << std::endl;
    }

    std::cout << check::registry().size() << "개 중 " << failedTests << "개 실패" << std::endl;
    return failedTests == 0 ? 0 : 1;
}
