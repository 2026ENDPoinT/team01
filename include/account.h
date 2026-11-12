// 담당: 조장 (Eno74)
// 로그인 계정 (관리자·교수·학생)
//   - 비밀번호는 salt + SHA-256 반복 해시로만 저장 (원문은 저장하지 않음)
//   - accounts.csv : username,role,name,studentId,salt,passwordHash
//   - 학생 존재 여부, 마지막 관리자·자기 계정 삭제 금지 규칙은 서버가 검사
#pragma once

#include <string>
#include <vector>

#include "course.h"
#include "file_io.h"

struct Account {
    std::string username;      // 아이디: 영문·숫자·_ 3~20자, 바꿀 수 없음
    std::string role;          // "admin", "professor", "student"
    std::string name;          // 관리자·교수 이름 (학생 계정은 "" 로 저장, 화면에는 학생 이름)
    std::string studentId;     // 학생 계정의 학번 (관리자·교수는 "")
    std::string salt;          // 소문자 hex 32자
    std::string passwordHash;  // hashPassword(비밀번호, salt), 소문자 hex 64자
};

// 역할 검사: admin / professor / student
bool isValidRole(const std::string& role);

// SHA-256 (외부 라이브러리 없이 직접 구현), 결과는 소문자 hex 64자
std::string sha256Hex(const std::string& data);

// 비밀번호 해시: d = sha256(salt + 비밀번호), 이어서 9999번 d = sha256(d + salt) → 소문자 hex 64자
std::string hashPassword(const std::string& password, const std::string& salt);

// 담당 과목인지: 교수 계정이고, 과목 담당 교수(쉼표로 여러 명)의 앞뒤 공백을 지운 이름 중 하나가 계정 이름과 같으면 true
bool teaches(const Account& account, const Course& course);

class AccountStore {
public:
    explicit AccountStore(const std::string& dataDir);

    bool load();
    bool save();

    // admin 역할 계정이 없으면 admin / admin1234 (이름 "학교 관리자") 를 추가하고 true. 저장은 호출자가 함
    bool ensureAdmin();

    const Account* find(const std::string& username) const;
    const Account* findByStudentId(const std::string& studentId) const;
    const std::vector<Account>& getAccounts() const;

    // 아이디·역할·비밀번호(8~64자)·이름(관리자·교수)·학번(학생, 숫자 8자리, 학생 계정끼리 중복 불가) 검사 후 추가
    //   아이디 중복이면 false. account 의 salt·passwordHash 는 무시하고 새로 만듦
    bool add(const Account& account, const std::string& password);
    // add 와 같은 검사. newPassword 가 "" 이면 비밀번호 유지. 없는 계정이면 false
    bool update(const std::string& username, const std::string& role, const std::string& name,
                const std::string& studentId, const std::string& newPassword);
    bool remove(const std::string& username);
    bool verify(const std::string& username, const std::string& password) const;
    // 8~64자 검사 후 새 salt 로 다시 해시
    bool changePassword(const std::string& username, const std::string& newPassword);
    // 학번이 바뀌면 학생 계정의 학번도 바꿈
    void relinkStudent(const std::string& oldId, const std::string& newId);
    // 그 학번의 학생 계정을 지우고 지운 아이디 목록을 반환
    std::vector<std::string> removeStudentAccounts(const std::string& studentId);

private:
    FileIO fileIO;
    std::vector<Account> accounts;
};
