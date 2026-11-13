// 상대평가 등급 산출
//   구현: #67 (지금은 스텁: 각 점수의 scoreToLetter 를 그대로 돌려줌)
#include "relative_grading.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "grade.h"

namespace RelativeGrading {

namespace {

const char* const kLadder[] = {"A+", "A0", "B+", "B0", "C+", "C0", "D+", "D0"};
const int kLadderSize = 8;

// kLadder 안의 위치 (A+ = 0 ... D0 = 7), F·그 밖은 -1
int ladderIndex(const std::string& letter) {
    for (int i = 0; i < kLadderSize; i++) {
        if (letter == kLadder[i]) return i;
    }
    return -1;
}

double averagePoint(const std::vector<std::string>& letters) {
    double sum = 0.0;
    for (const std::string& letter : letters) sum += letterToPoint(letter);
    return sum / static_cast<double>(letters.size());
}

}  // namespace

std::vector<std::string> assignLetters(const std::vector<double>& scores) {
    const size_t n = scores.size();
    std::vector<std::string> letters;
    letters.reserve(n);
    for (double score : scores) letters.push_back(scoreToLetter(score));
    if (n == 0) return letters;

    const size_t capA = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.30 + 1e-9));
    const size_t capAB = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.70 + 1e-9));

    // 점수 높은 순 (같은 점수는 입력 순서 유지)
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), static_cast<size_t>(0));
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return scores[a] > scores[b]; });

    size_t countA = 0;
    size_t countAB = 0;
    bool aClosed = false;
    bool abClosed = false;

    size_t begin = 0;
    while (begin < n) {
        size_t end = begin;
        while (end < n && scores[order[end]] == scores[order[begin]]) end++;
        const size_t k = end - begin;
        std::string letter = letters[order[begin]];

        if (letter == "A+" || letter == "A0") {
            if (!aClosed && countA + k <= capA) {
                countA += k;
                countAB += k;
            } else {
                aClosed = true;
                letter = "B+";
            }
        }
        if (letter == "B+" || letter == "B0") {
            // A 에서 그대로 통과한 묶음은 위에서 이미 countAB 를 올렸고 letter 가 A 라 여기 오지 않음
            if (!abClosed && countAB + k <= capAB) {
                countAB += k;
            } else {
                abClosed = true;
                letter = "C+";
            }
        }
        for (size_t i = begin; i < end; i++) letters[order[i]] = letter;
        begin = end;
    }

    // 평균 상한 3.50 (안전장치)
    while (averagePoint(letters) > 3.50 + 1e-9) {
        int top = kLadderSize;  // 가장 높은 등급 (F 제외)
        for (const std::string& letter : letters) {
            const int idx = ladderIndex(letter);
            if (idx >= 0 && idx < top) top = idx;
        }
        if (top >= kLadderSize - 1) break;  // D0 이하는 더 내릴 수 없음

        double lowest = 0.0;  // 그 등급 중 가장 낮은 점수
        bool first = true;
        for (size_t i = 0; i < n; i++) {
            if (ladderIndex(letters[i]) != top) continue;
            if (first || scores[i] < lowest) lowest = scores[i];
            first = false;
        }
        for (size_t i = 0; i < n; i++) {
            if (ladderIndex(letters[i]) == top && scores[i] == lowest) letters[i] = kLadder[top + 1];
        }
    }
    return letters;
}

}  // namespace RelativeGrading