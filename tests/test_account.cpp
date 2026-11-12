// 담당: 조장 (Eno74)
// 계정: SHA-256 표준 값, 비밀번호 확인, 입력 검사, 저장 → 불러오기, 관리자 자동 생성, 담당 과목 판정
#include <string>
#include <vector>

#include "account.h"
#include "check.h"
#include "test_support.h"

namespace {

Account makeAccount(const std::string& username, const std::string& role, const std::string& name,
                    const std::string& studentId = "") {
    Account a;
    a.username = username;
    a.role = role;
    a.name = name;
    a.studentId = studentId;
    return a;
}

}  // namespace

TEST_CASE(sha256Hex_known_vectors) {
    CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    // 64바이트 → 패딩 블록이 하나 더 붙는 경계
    CHECK_EQ(sha256Hex(std::string(64, 'a')),
             std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
}

TEST_CASE(hashPassword_depends_on_salt) {
    const std::string h = hashPassword("password1", "00112233445566778899aabbccddeeff");
    CHECK_EQ(h.size(), static_cast<size_t>(64));
    CHECK_EQ(h, hashPassword("password1", "00112233445566778899aabbccddeeff"));
    CHECK(h != hashPassword("password1", "ffeeddccbbaa99887766554433221100"));
    CHECK(h != hashPassword("password2", "00112233445566778899aabbccddeeff"));
}

TEST_CASE(account_add_and_verify) {
    TempDir dir;
    AccountStore store(dir.str());
    CHECK(store.add(makeAccount("kimcs", "professor", "김철수"), "prof1234"));
    CHECK(store.verify("kimcs", "prof1234"));
    CHECK(!store.verify("kimcs", "prof12345"));
    CHECK(!store.verify("nobody", "prof1234"));

    const Account* a = store.find("kimcs");
    CHECK(a != nullptr && a->salt.size() == 32 && a->passwordHash.size() == 64);
    CHECK(a != nullptr && a->passwordHash != "prof1234");  // 원문은 저장하지 않음
}

TEST_CASE(account_add_rejects_invalid) {
    TempDir dir;
    AccountStore store(dir.str());
    CHECK(store.add(makeAccount("admin_2", "admin", "관리자"), "admin1234"));
    CHECK(!store.add(makeAccount("admin_2", "admin", "다른 관리자"), "admin1234"));   // 아이디 중복
    CHECK(!store.add(makeAccount("ab", "admin", "관리자"), "admin1234"));             // 2자
    CHECK(!store.add(makeAccount("a-b", "admin", "관리자"), "admin1234"));            // - 불가
    CHECK(!store.add(makeAccount(std::string(21, 'a'), "admin", "관리자"), "admin1234"));  // 21자
    CHECK(!store.add(makeAccount("teacher", "teacher", "선생"), "admin1234"));        // 잘못된 역할
    CHECK(!store.add(makeAccount("shortpw", "admin", "관리자"), "1234567"));          // 7자 비밀번호
    CHECK(!store.add(makeAccount("longpw", "admin", "관리자"), std::string(65, 'x')));
    CHECK(!store.add(makeAccount("noname", "professor", "  "), "prof1234"));          // 교수 이름 필수
    CHECK(!store.add(makeAccount("stu1", "student", "", "2310100"), "student1234"));  // 학번 7자리

    CHECK(store.add(makeAccount("stu1", "student", "무시됨", "23101001"), "student1234"));
    const Account* stu = store.find("stu1");
    CHECK(stu != nullptr && stu->name.empty() && stu->studentId == "23101001");  // 학생 이름은 저장 안 함
    CHECK(!store.add(makeAccount("stu2", "student", "", "23101001"), "student1234"));  // 같은 학생 계정 중복
    CHECK(store.add(makeAccount(std::string(20, 'a'), "admin", "관리자"), std::string(64, 'x')));  // 경계 OK
    CHECK_EQ(store.getAccounts().size(), static_cast<size_t>(3));
}

TEST_CASE(account_update_and_change_password) {
    TempDir dir;
    AccountStore store(dir.str());
    CHECK(store.add(makeAccount("kimcs", "professor", "김철수"), "prof1234"));
    CHECK(store.add(makeAccount("stu1", "student", "", "23101001"), "student1234"));

    CHECK(store.update("kimcs", "professor", "김철수2", "", ""));  // 비밀번호 유지
    CHECK(store.verify("kimcs", "prof1234"));
    CHECK(store.find("kimcs")->name == "김철수2");
    CHECK(!store.update("kimcs", "student", "", "23101001", ""));  // 다른 학생 계정과 학번 중복
    CHECK(!store.update("kimcs", "professor", "김철수", "", "short"));
    CHECK(!store.update("nobody", "professor", "김철수", "", ""));
    CHECK(store.update("stu1", "student", "", "23101001", "newpass99"));  // 자기 학번은 중복 아님
    CHECK(store.verify("stu1", "newpass99"));

    CHECK(!store.changePassword("kimcs", "1234567"));
    CHECK(store.changePassword("kimcs", "changed123"));
    CHECK(store.verify("kimcs", "changed123"));
    CHECK(!store.verify("kimcs", "prof1234"));
}

TEST_CASE(account_save_load_roundtrip) {
    TempDir dir;
    {
        AccountStore store(dir.str());
        CHECK(store.load());  // 파일 없음 → 빈 목록
        CHECK(store.getAccounts().empty());
        CHECK(store.add(makeAccount("kimcs", "professor", "김철수, 교수"), "prof1234"));
        CHECK(store.add(makeAccount("stu1", "student", "", "23101001"), "student1234"));
        CHECK(store.save());
    }

    AccountStore loaded(dir.str());
    CHECK(loaded.load());
    CHECK_EQ(loaded.getAccounts().size(), static_cast<size_t>(2));
    CHECK(loaded.verify("kimcs", "prof1234"));
    CHECK(loaded.verify("stu1", "student1234"));
    const Account* prof = loaded.find("kimcs");
    CHECK(prof != nullptr && prof->name == "김철수, 교수");  // 쉼표 든 이름도 그대로
    const Account* stu = loaded.findByStudentId("23101001");
    CHECK(stu != nullptr && stu->username == "stu1");
}

TEST_CASE(ensureAdmin_only_first_time) {
    TempDir dir;
    AccountStore store(dir.str());
    CHECK(store.ensureAdmin());
    CHECK(!store.ensureAdmin());
    const Account* admin = store.find("admin");
    CHECK(admin != nullptr && admin->role == "admin" && admin->name == "학교 관리자");
    CHECK(store.verify("admin", "admin1234"));
}

TEST_CASE(teaches_matches_trimmed_professor_names) {
    Course course = makeCourse("100101", "미적분학");
    course.professor = "최정훈, 한수진";
    CHECK(teaches(makeAccount("hsj", "professor", "한수진"), course));
    CHECK(teaches(makeAccount("cjh", "professor", "최정훈"), course));
    CHECK(!teaches(makeAccount("hs", "professor", "한수"), course));
    CHECK(!teaches(makeAccount("stu", "student", "한수진"), course));  // 교수가 아님
    CHECK(!teaches(makeAccount("adm", "admin", "한수진"), course));

    course.professor = "";
    CHECK(!teaches(makeAccount("hsj", "professor", "한수진"), course));
}

TEST_CASE(relink_and_remove_student_accounts) {
    TempDir dir;
    AccountStore store(dir.str());
    CHECK(store.add(makeAccount("stu1", "student", "", "23101001"), "student1234"));
    CHECK(store.add(makeAccount("stu2", "student", "", "23101002"), "student1234"));

    store.relinkStudent("23101001", "23101009");
    CHECK(store.findByStudentId("23101001") == nullptr);
    const Account* relinked = store.findByStudentId("23101009");
    CHECK(relinked != nullptr && relinked->username == "stu1");

    const std::vector<std::string> removed = store.removeStudentAccounts("23101009");
    CHECK_EQ(removed.size(), static_cast<size_t>(1));
    CHECK(removed.size() == 1 && removed[0] == "stu1");
    CHECK(store.find("stu1") == nullptr);
    CHECK(store.find("stu2") != nullptr);
    CHECK(store.removeStudentAccounts("99999999").empty());
}
