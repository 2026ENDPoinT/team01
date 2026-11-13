// 상대평가 등급 산출
//   구현: #67 (지금은 스텁: 각 점수의 scoreToLetter 를 그대로 돌려줌)
#include "relative_grading.h"

#include <algorithm>
#include <cmath>
#include <numeric>


namespace RelativeGrading {

namespace {
const char* const kLetters[] = {"A+", "A0", "B+", "B0", "C+", "C0", "F"};
constexpr size_t kLetterCount = sizeof(kLetters) / sizeof(kLetters[0]);
}

std::vector<int> defaultRatios() {
    return {10, 20, 20, 20, 10, 10, 10};
}

bool validRatios(const std::vector<int>& ratios) {
    if (ratios.size() != kLetterCount) return false;
    int total = 0;
    for (int ratio : ratios) {
        if (ratio < 0 || ratio > 100) return false;
        total += ratio;
    }
    return total == 100;
}

std::vector<std::string> assignLetters(const std::vector<double>& scores,
                                       const std::vector<int>& ratios) {
    const size_t n = scores.size();
    std::vector<std::string> result(n);
    if (n == 0 || !validRatios(ratios)) return result;

    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), static_cast<size_t>(0));
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return scores[a] > scores[b]; });

    // 동점자는 같은 등급으로 묶는다. 동점 묶음의 시작 순위에 해당하는 비율 구간의 등급을 부여한다.
    size_t begin = 0;
    while (begin < n) {
        size_t end = begin + 1;
        while (end < n && scores[order[end]] == scores[order[begin]]) ++end;

        size_t cumulativePercent = 0;
        size_t category = kLetterCount - 1;  // 마지막 구간은 F
        for (size_t i = 0; i < kLetterCount; ++i) {
            cumulativePercent += static_cast<size_t>(ratios[i]);
            const size_t boundary = (n * cumulativePercent) / 100;
            if (begin < boundary) {
                category = i;
                break;
            }
        }
        for (size_t i = begin; i < end; ++i) {
            result[order[i]] = kLetters[category];
        }
        begin = end;
    }
    return result;
}

std::vector<std::string> assignLetters(const std::vector<double>& scores) {
    return assignLetters(scores, defaultRatios());
}

}  // namespace RelativeGrading