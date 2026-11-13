// 상대평가 등급 산출
// A0 이상 30%, B0 이상 70% 상한과 평균 평점 3.50 이하를 적용한다.
#include "relative_grading.h"

#include <algorithm>
#include <numeric>

#include "grade.h"

namespace RelativeGrading {
namespace {

// 실제 평점 계산 순서: 6개 등급
const char* const kGradeLadder[] = {"A+", "A0", "B+", "B0", "C+", "F"};
constexpr size_t kGradeCount = sizeof(kGradeLadder) / sizeof(kGradeLadder[0]);

// 비율 설정 순서와 실제 등급 순서가 동일하다.
const size_t kRatioGradeIndex[] = {0, 1, 2, 3, 4, 5};

size_t gradeIndex(const std::string& letter) {
    for (size_t i = 0; i < kGradeCount; ++i) {
        if (letter == kGradeLadder[i]) return i;
    }
    return kGradeCount - 1;
}

double averagePoint(const std::vector<std::string>& letters) {
    if (letters.empty()) return 0.0;
    double sum = 0.0;
    for (const std::string& letter : letters) sum += letterToPoint(letter);
    return sum / static_cast<double>(letters.size());
}

}  // namespace

std::vector<int> defaultRatios() {
    // 기본 상대평가: 상위 30% A+, 다음 40% B+, 나머지 30% C+.
    // A0 이상 30%, B0 이상 70% 상한을 만족하며 평균 평점은 최대 3.50이다.
    return {30, 0, 40, 0, 30, 0};
}

bool validRatios(const std::vector<int>& ratios) {
    if (ratios.size() != 6) return false;
    int total = 0;
    for (int ratio : ratios) {
        if (ratio < 0 || ratio > 100) return false;
        total += ratio;
    }
    if (total != 100) return false;

    // A+ + A0는 30% 이하, A+ + A0 + B+ + B0는 70% 이하여야 한다.
    const int aPlusAndA0 = ratios[0] + ratios[1];
    const int aThroughB0 = aPlusAndA0 + ratios[2] + ratios[3];
    if (aPlusAndA0 > static_cast<int>(kMaxARatio * 100)) return false;
    if (aThroughB0 > static_cast<int>(kMaxABRatio * 100)) return false;

    return true;
}

std::vector<std::string> assignLetters(const std::vector<double>& scores,
                                       const std::vector<int>& ratios) {
    const size_t n = scores.size();
    std::vector<std::string> result(n);
    if (n == 0 || !validRatios(ratios)) return result;

    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), static_cast<size_t>(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return scores[a] > scores[b];
    });

    // 같은 점수는 하나의 묶음으로 처리한다. 묶음 전체가 들어갈 수 있는
    // 첫 번째 비율 구간을 선택하므로 등급 경계에서 동점자가 갈라지지 않는다.
    size_t begin = 0;
    while (begin < n) {
        size_t end = begin + 1;
        while (end < n && scores[order[end]] == scores[order[begin]]) ++end;

        size_t cumulativeRatio = 0;
        size_t relativeGrade = kRatioGradeIndex[5];  // 기본값 F
        for (size_t category = 0; category < ratios.size(); ++category) {
            cumulativeRatio += static_cast<size_t>(ratios[category]);
            const size_t boundary = (n * cumulativeRatio) / 100;
            if (end <= boundary) {
                relativeGrade = kRatioGradeIndex[category];
                break;
            }
        }

        // 상대평가 결과가 점수 기준 절대평가 등급보다 높아지지 않도록 한다.
        const size_t absoluteGrade = gradeIndex(scoreToLetter(scores[order[begin]]));
        const size_t assignedGrade = std::max(relativeGrade, absoluteGrade);
        for (size_t i = begin; i < end; ++i) {
            result[order[i]] = kGradeLadder[assignedGrade];
        }
        begin = end;
    }

    // 평균 평점 상한 3.50을 보장한다. 가장 낮은 점수의 비-F 동점 묶음부터
    // 한 단계씩 낮추므로 동점자 일치와 점수 순서(고득점이 더 낮은 등급을 받지 않음)를 유지한다.
    while (averagePoint(result) > 3.50 + 1e-9) {
        double lowestScore = 0.0;
        bool found = false;
        for (size_t i = 0; i < n; ++i) {
            if (gradeIndex(result[i]) >= kGradeCount - 1) continue;
            if (!found || scores[i] < lowestScore) {
                lowestScore = scores[i];
                found = true;
            }
        }
        if (!found) break;

        size_t currentGrade = kGradeCount - 1;
        for (size_t i = 0; i < n; ++i) {
            if (scores[i] == lowestScore) {
                currentGrade = gradeIndex(result[i]);
                break;
            }
        }
        if (currentGrade >= kGradeCount - 1) break;
        for (size_t i = 0; i < n; ++i) {
            if (scores[i] == lowestScore) {
                result[i] = kGradeLadder[currentGrade + 1];
            }
        }
    }

    return result;
}

std::vector<std::string> assignLetters(const std::vector<double>& scores) {
    return assignLetters(scores, defaultRatios());
}

}  // namespace RelativeGrading
