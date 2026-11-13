// 담당: 조장 (Eno74)
// 학기 문자열 유틸 ("YYYY-N", N: 1 1학기, 2 2학기, 3 여름계절, 4 겨울계절)
//   - 시간순: 1학기 < 여름계절(3) < 2학기 < 겨울계절(4) (같은 해 안에서), 다음 해 1학기가 그 뒤
//   - "현재 학기"는 설정된 값이 있으면 그것, 없으면 오늘 날짜로 계산
#pragma once

#include <ctime>
#include <string>

namespace semester {

// 형식 YYYY-N (N=1..4) 이면 true
bool isValid(const std::string& semester);

// 시간순 비교 키: year*4 + {1->0, 3->1, 2->2, 4->3}  (1학기 < 여름 < 2학기 < 겨울). 형식 오류면 -1
int orderKey(const std::string& semester);

// semester 가 current 보다 이전이면 true. 둘 중 하나라도 형식 오류면 false (막지 않는 쪽으로)
bool isPast(const std::string& semester, const std::string& current);

// 날짜로 학기 계산: 3~8월 -> year-1, 9~12월 -> year-2, 1~2월 -> (year-1)-2. (month 1..12)
std::string fromDate(int year, int month);

// 현재 학기: enrollmentSemester 가 비어 있지 않으면 그 값, 비었으면 now(로컬 시간) 로 fromDate
std::string current(const std::string& enrollmentSemester, std::time_t now);

}  // namespace semester
