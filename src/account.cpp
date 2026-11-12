// 담당: 조장 (Eno74)
#include "account.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <random>

namespace {

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------

const uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

// 결과는 raw 32바이트
std::string sha256Raw(const std::string& data) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    // 패딩: 0x80 하나 + 0 들 + 원래 길이(비트, 빅엔디언 8바이트) → 64바이트 배수
    std::string msg = data;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back(static_cast<char>(0));
    }
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
    for (int i = 7; i >= 0; i--) {
        msg.push_back(static_cast<char>((bitLength >> (i * 8)) & 0xff));
    }

    uint32_t w[64];
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        for (int i = 0; i < 16; i++) {
            const size_t p = chunk + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(msg[p])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[p + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[p + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<unsigned char>(msg[p + 3]));
        }
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + s1 + ch + kRoundConstants[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::string digest;
    digest.reserve(32);
    for (uint32_t word : h) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            digest.push_back(static_cast<char>((word >> shift) & 0xff));
        }
    }
    return digest;
}

// 바이트 → 소문자 hex
std::string toHex(const std::string& bytes) {
    static const char* const digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        hex.push_back(digits[c >> 4]);
        hex.push_back(digits[c & 0x0f]);
    }
    return hex;
}

// 16바이트 random_device 값의 소문자 hex (32자)
std::string makeSalt() {
    std::random_device rd;
    std::string bytes;
    for (int i = 0; i < 16; i++) {
        bytes.push_back(static_cast<char>(rd() & 0xff));
    }
    return toHex(bytes);
}

// ---------------------------------------------------------------------------
// 입력 검사
// ---------------------------------------------------------------------------

// 아이디: 영문·숫자·_ 3~20자
bool isValidUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 20) return false;
    for (char c : username) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// 비밀번호: 8~64자 (바이트 기준)
bool isValidPassword(const std::string& password) {
    return password.size() >= 8 && password.size() <= 64;
}

// 공백(스페이스·탭·줄바꿈)만 있거나 비어있으면 true
bool isBlank(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

bool isEightDigits(const std::string& s) {
    if (s.size() != 8) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

// 앞뒤 스페이스·탭 제거
std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    const size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

}  // namespace

bool isValidRole(const std::string& role) {
    return role == "admin" || role == "professor" || role == "student";
}

std::string sha256Hex(const std::string& data) {
    return toHex(sha256Raw(data));
}

std::string hashPassword(const std::string& password, const std::string& salt) {
    std::string d = sha256Raw(salt + password);
    for (int i = 0; i < 9999; i++) {
        d = sha256Raw(d + salt);
    }
    return toHex(d);
}

bool teaches(const Account& account, const Course& course) {
    if (account.role != "professor") return false;
    size_t start = 0;
    while (start <= course.professor.size()) {
        size_t comma = course.professor.find(',', start);
        if (comma == std::string::npos) comma = course.professor.size();
        const std::string piece = trim(course.professor.substr(start, comma - start));
        if (!piece.empty() && piece == account.name) return true;
        start = comma + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// AccountStore
// ---------------------------------------------------------------------------

AccountStore::AccountStore(const std::string& dataDir) : fileIO(dataDir) {}

bool AccountStore::load() {
    return fileIO.loadAccounts(accounts);
}

bool AccountStore::save() {
    return fileIO.saveAccounts(accounts);
}

bool AccountStore::ensureAdmin() {
    const bool hasAdmin = std::any_of(accounts.begin(), accounts.end(), [](const Account& a) {
        return a.role == "admin";
    });
    if (hasAdmin) return false;

    Account admin;
    admin.username = "admin";
    admin.role = "admin";
    admin.name = "학교 관리자";
    return add(admin, "admin1234");  // "admin" 아이디를 다른 역할이 쓰고 있으면 추가하지 못함
}

const Account* AccountStore::find(const std::string& username) const {
    auto it = std::find_if(accounts.begin(), accounts.end(), [&](const Account& a) {
        return a.username == username;
    });
    return it == accounts.end() ? nullptr : &*it;
}

const Account* AccountStore::findByStudentId(const std::string& studentId) const {
    auto it = std::find_if(accounts.begin(), accounts.end(), [&](const Account& a) {
        return a.role == "student" && a.studentId == studentId;
    });
    return it == accounts.end() ? nullptr : &*it;
}

const std::vector<Account>& AccountStore::getAccounts() const { return accounts; }

bool AccountStore::add(const Account& account, const std::string& password) {
    if (!isValidUsername(account.username) || find(account.username) != nullptr) return false;
    if (!isValidRole(account.role) || !isValidPassword(password)) return false;

    Account stored;
    stored.username = account.username;
    stored.role = account.role;
    if (account.role == "student") {
        if (!isEightDigits(account.studentId) || findByStudentId(account.studentId) != nullptr) return false;
        stored.studentId = account.studentId;  // 학생 이름은 학생 목록에서 가져옴
    } else {
        if (isBlank(account.name)) return false;
        stored.name = account.name;
    }
    stored.salt = makeSalt();
    stored.passwordHash = hashPassword(password, stored.salt);
    accounts.push_back(stored);
    return true;
}

bool AccountStore::update(const std::string& username, const std::string& role, const std::string& name,
                          const std::string& studentId, const std::string& newPassword) {
    auto it = std::find_if(accounts.begin(), accounts.end(), [&](const Account& a) {
        return a.username == username;
    });
    if (it == accounts.end() || !isValidRole(role)) return false;
    if (!newPassword.empty() && !isValidPassword(newPassword)) return false;

    std::string newName;
    std::string newStudentId;
    if (role == "student") {
        if (!isEightDigits(studentId)) return false;
        const Account* other = findByStudentId(studentId);
        if (other != nullptr && other->username != username) return false;  // 다른 학생 계정과 중복
        newStudentId = studentId;
    } else {
        if (isBlank(name)) return false;
        newName = name;
    }

    // 모든 검사를 통과한 뒤에 바꿈
    it->role = role;
    it->name = newName;
    it->studentId = newStudentId;
    if (!newPassword.empty()) {
        it->salt = makeSalt();
        it->passwordHash = hashPassword(newPassword, it->salt);
    }
    return true;
}

bool AccountStore::remove(const std::string& username) {
    auto it = std::find_if(accounts.begin(), accounts.end(), [&](const Account& a) {
        return a.username == username;
    });
    if (it == accounts.end()) return false;
    accounts.erase(it);
    return true;
}

bool AccountStore::verify(const std::string& username, const std::string& password) const {
    const Account* account = find(username);
    if (account == nullptr) return false;
    return hashPassword(password, account->salt) == account->passwordHash;
}

bool AccountStore::changePassword(const std::string& username, const std::string& newPassword) {
    if (!isValidPassword(newPassword)) return false;
    auto it = std::find_if(accounts.begin(), accounts.end(), [&](const Account& a) {
        return a.username == username;
    });
    if (it == accounts.end()) return false;
    it->salt = makeSalt();
    it->passwordHash = hashPassword(newPassword, it->salt);
    return true;
}

void AccountStore::relinkStudent(const std::string& oldId, const std::string& newId) {
    for (Account& a : accounts) {
        if (a.role == "student" && a.studentId == oldId) {
            a.studentId = newId;
        }
    }
}

std::vector<std::string> AccountStore::removeStudentAccounts(const std::string& studentId) {
    std::vector<std::string> removed;
    for (const Account& a : accounts) {
        if (a.role == "student" && a.studentId == studentId) {
            removed.push_back(a.username);
        }
    }
    accounts.erase(std::remove_if(accounts.begin(), accounts.end(), [&](const Account& a) {
        return a.role == "student" && a.studentId == studentId;
    }), accounts.end());
    return removed;
}
