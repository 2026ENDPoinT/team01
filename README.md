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

## 역할

| 역할 | 담당 | 파일 |
|---|---|---|
| 조장 | 동현 | `file_io`, `report_exporter`, `CMakeLists.txt`, 헤더 관리, PR 리뷰·merge |
| A | 연준 | `student`, `course`, `grade` — 데이터 클래스, 점수→등급 변환, 입력 검증 |
| B | 현준 | `grade_manager` — 등록/수정/삭제/조회, 검색 |
| C | 원준 | `statistics`, `server`, `web/` — GPA·평균·석차, HTTP 서버, 웹 UI |

각자 `src/` 의 자기 `.cpp` 에 있는 `TODO` 를 채우면 됩니다.

## 폴더 구조

```
include/      헤더 (함수 시그니처)
src/          구현 + main.cpp
web/          index.html, style.css, app.js
data/         students.csv, courses.csv, grades.csv (자동 생성)
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
- 성적 `{"studentId", "courseCode", "semester", "score", "letter"}`
