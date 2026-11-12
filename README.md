# team01 — 성적 관리 프로그램

ENDPoinT 해커톤 2026 Team 01 (구현과제 트랙)

C++17 프로그램이 로컬 HTTP 서버로 동작하고, 브라우저에서 `http://localhost:8080` 으로 접속해서 사용합니다.

## 빌드 / 실행

필요: CMake 3.16+, C++17 컴파일러 (GCC, Clang, MSVC, MinGW)

**CLion**: 폴더 열기 → `GradeManager` 실행 → 브라우저에서 http://localhost:8080

**명령줄**
```bash
cmake -B build
cmake --build build
./build/GradeManager          # Windows: build\Debug\GradeManager.exe
./build/GradeManager 9090     # 8080 이 이미 사용 중이면 다른 포트로
```

`data/`, `web/` 경로는 빌드 시 프로젝트 경로로 고정되므로 어디서 실행해도 됩니다.

**샘플 데이터** (학생 9명, 과목 6개, 성적 29건): `data/sample/` 을 `data/` 로 복사하면 바로 화면에서 확인할 수 있습니다.
```bash
cp data/sample/*.csv data/          # Windows: copy data\sample\*.csv data\
```
`data/*.csv` 는 실행 중에 바뀌므로 git 에 올리지 않습니다 (`.gitignore`). 처음 상태로 돌리려면 다시 복사하면 됩니다.

샘플에 일부러 넣은 경우:
- 등급 경계 점수 (100, 95, 90, 85, 65, 60, 59.5, 0), 소수점 점수
- GPA 동점: 이서연·박지호 (전체, 2026-1 둘 다 같음) → 석차 같은 등수
- 재수강: 윤지아 MA101 (2018년 이후 입학, 2025-2 F → 2026-1, 성적표에 2025-2 는 "R"),
  한지민 CS201 (2017년 입학, 2025-1 F → 2025-2 97점이지만 A0, 성적표에서 2025-1 줄은 빠짐)
- 성적 없는 학생 (조서준), 성적 없는 과목 (CS401)
- 쉼표가 든 값: MA101 담당 교수 `"최정훈, 한수진"`

## 역할

| 역할 | 담당 | 파일 |
|---|---|---|
| 조장 | 동현 | `file_io`, `report_exporter`, `retake`, `CMakeLists.txt`, 헤더 관리, PR 리뷰·merge |
| A | 연준 | `student`, `course`, `grade` — 데이터 클래스, 점수→등급 변환, 입력 검증 |
| B | 현준 | `grade_manager` — 등록/수정/삭제/조회, 검색 |
| C | 원준 | `statistics`, `server`, `web/` — GPA·평균·석차, HTTP 서버, 웹 UI |

각자 `src/` 의 자기 `.cpp` 에 있는 `TODO` 를 채우면 됩니다.

## 폴더 구조

```
include/      헤더 (함수 시그니처)
src/          구현 + main.cpp
web/          index.html, style.css, app.js
data/         students.csv, courses.csv, grades.csv (자동 생성, git 제외)
data/sample/  샘플 데이터 (위 "샘플 데이터" 참고)
third_party/  httplib.h (cpp-httplib v0.18.3), json.hpp (nlohmann/json v3.11.3)
```

의존 방향: `student/course/grade` ← `file_io` ← `grade_manager` ← `statistics`, `report_exporter` ← `server` ← `main`

## 규칙

- 파일명 `snake_case.h/.cpp`, 클래스명 `PascalCase`, 함수·변수 `camelCase`
- include 할 때 대소문자 정확히 (Linux 에서는 틀리면 빌드 에러)
- 브랜치: `feat/이름-기능` (예: `feat/yeonjun-grade`) → PR → `main` 은 조장만 merge, main 직접 push 금지
- 커밋 메시지: [Conventional Commits](https://www.conventionalcommits.org/ko/) (`feat:`, `fix:`, `docs:`, `refactor:` ...)

## API

요청/응답 본문은 JSON. 실패 시 `400` 잘못된 값 / `404` 없음 / `409` 중복 + `{"error": "메시지"}`

| 메서드 | 경로 | 설명 |
|---|---|---|
| GET | `/api/students` | 학생 목록 |
| POST | `/api/students` | 학생 등록 |
| GET | `/api/students/search?q=` | 학번·이름 검색 |
| PUT | `/api/students/:id` | 학생 수정 |
| DELETE | `/api/students/:id` | 학생 삭제 (성적도 삭제) |
| GET | `/api/courses` | 과목 목록 |
| POST | `/api/courses` | 과목 등록 |
| PUT | `/api/courses/:code` | 과목 수정 |
| DELETE | `/api/courses/:code` | 과목 삭제 (성적도 삭제) |
| GET | `/api/grades` | 성적 목록 (`?studentId=` `?courseCode=` `?semester=` 로 거르기) |
| POST | `/api/grades` | 성적 등록 |
| PUT | `/api/grades` | 성적 수정 (본문의 studentId·courseCode·semester 로 찾음) |
| DELETE | `/api/grades?studentId=&courseCode=&semester=` | 성적 삭제 |
| GET | `/api/stats/gpa/:id` | 학생 GPA (`?semester=`) |
| GET | `/api/stats/course/:code` | 과목 평균/최고/최저 |
| GET | `/api/stats/ranking` | 석차표 (`?semester=`) |
| GET | `/api/export/student/:id` | 학생 성적표 CSV |
| GET | `/api/export/course/:code` | 과목 성적표 CSV |

JSON 필드 이름은 헤더의 멤버 이름과 같습니다.
- 학생 `{"id", "name", "major", "year"}`
- 과목 `{"code", "name", "credit", "professor"}`
- 성적 `{"studentId", "courseCode", "semester", "score", "letter"}` — `letter` 는 `Retake::officialLetter` (재수강 규칙 적용)
- 학기 `semester` 는 `YYYY-N`: `-1` 1학기, `-2` 2학기, `-3` 여름 계절학기, `-4` 겨울 계절학기 (순서 1 → 3 → 2 → 4). 입학년도(학번 앞 2자리 → 20XX) 이전 학기와 현재보다 미래 학기의 성적은 등록·수정할 수 없음 (400 + 이유)

## 재수강 규칙

같은 학생이 같은 과목을 여러 학기에 들으면 가장 늦은 학기가 재수강 성적이고, 그 전 성적은 삭제된 성적입니다. 자세한 내용과 함수는 `include/retake.h` 를 참고하세요.

- 재수강 성적은 A0 까지만 받습니다 (95점 이상이어도 A0, 평점 4.0).
- 삭제된 성적은 GPA, 석차, 과목 통계, 과목 성적표에서 빠집니다. `grades.csv` 에는 그대로 남습니다.
- 학생 성적표: 2018년 이후 입학생(학번 앞 2자리 ≥ 18)은 삭제된 과목을 등급 `R` 로 표시하고, 그 전 입학생은 그 줄을 뺍니다.
- 학사경고: 이미 내려진 학사경고는 유지합니다. 학사경고를 판단할 때는 재수강 규칙을 적용하지 않고 원래 성적을 씁니다.
  - 기준 (아직 구현 안 함): 정규학기 평점평균 1.75 미만, 또는 수강신청 미이행 (기간 내 수강신청을 안 했거나 1학점 미만으로 신청한 재학생)
