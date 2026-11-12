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

**테스트**
```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
# Windows(Visual Studio): ctest --test-dir build -C Debug --output-on-failure
# CMake 3.20 미만 (--test-dir 없음): cd build && ctest --output-on-failure
```

`tests/` 는 외부 라이브러리 없이 `GradeTests` 하나로 등급 경계, 재수강 규칙, 석차 동점, 과목코드 변경·삭제 연쇄, CSV 저장/불러오기를 검사합니다 (임시 폴더만 사용, `data/` 는 건드리지 않음).

## 데이터

`data/` 의 CSV (학생 9명, 과목 6개, 성적 29건, 수강신청 29건)를 git 으로 같이 관리합니다. 실행하면 바로 화면에서 확인할 수 있습니다.

| 파일 | 칸 |
|---|---|
| `students.csv` | `id,name,major,year` |
| `courses.csv` | `code,name,credit,professor,category,area,schedule,language,capacity,remote` |
| `grades.csv` | `studentId,courseCode,semester,score` |
| `enrollments.csv` | `studentId,courseCode,semester` (수강신청) |
| `settings.csv` | `key,value` (`enrollmentSemester` = 수강신청 학기, 비어 있으면 신청 기간이 아님) |
| `accounts.csv` | `username,role,name,studentId,salt,passwordHash` (로그인 계정, 비밀번호는 salt + SHA-256 반복 해시) |
| `course_posts.csv` | `id,courseCode,semester,category,week,title,body,link,dueDate,author,createdAt` (과목 게시물, body 의 줄바꿈은 `\n` 두 글자로, `\` 는 `\\` 로 저장) |

프로그램이 등록·수정·삭제할 때마다 이 파일을 바로 고쳐 씁니다 (직전 내용은 `.bak`, git 제외).
화면에서 테스트하느라 바뀐 내용은 커밋 전에 `git diff data/` 로 확인하고, 되돌리려면 `git checkout -- data/` 를 쓰면 됩니다.

일부러 넣은 경우:
- 등급 경계 점수 (100, 95, 90, 85, 65, 60, 59.5, 0), 소수점 점수
- GPA 동점: 이서연·박지호 (전체, 2026-1 둘 다 같음) → 석차 같은 등수
- 재수강: 윤지아 미적분학 `100101` (2018년 이후 입학, 2025-2 F → 2026-1, 성적표에 2025-2 는 "R"),
  한지민 자료구조 `109201` (2017년 입학, 2025-1 F → 2025-2 97점이지만 A0, 성적표에서 2025-1 줄은 빠짐)
- 성적 없는 학생 (조서준), 성적 없는 과목 (캡스톤디자인 `109401`, 정원 30명)
- 쉼표가 든 값: 미적분학 담당 교수 `"최정훈, 한수진"`
- 과목 상세: 이수구분 3종류 (전공필수·전공선택·교양필수), 교양영역 (미적분학 자연과학, 글쓰기 인문), 강의언어 3종류,
  강의시간 여러 칸 (캡스톤디자인 `화(6~9) 목(6~7)`), 강의시간 없는 원격강좌 (글쓰기)

## 역할

| 역할 | 담당 | 파일 |
|---|---|---|
| 조장 | 동현 | `file_io`, `report_exporter`, `retake`, `CMakeLists.txt`, 헤더 관리, PR 리뷰·merge |
| A | 연준 | `student`, `course`, `grade` — 데이터 클래스, 점수→등급 변환, 입력 검증 |
| B | 현준 | `grade_manager` — 등록/수정/삭제/조회, 검색 |
| C | 원준 | `statistics`, `server`, `web/` — GPA·평균·석차, HTTP 서버, 웹 UI |

## 폴더 구조

```
include/      헤더 (함수 시그니처)
src/          구현 + main.cpp
web/          index.html, style.css, app.js
data/         students.csv, courses.csv, grades.csv, enrollments.csv, settings.csv, accounts.csv, course_posts.csv (위 "데이터" 참고)
third_party/  httplib.h (cpp-httplib v0.18.3), json.hpp (nlohmann/json v3.11.3)
tests/        GradeTests 단위 테스트 (check.h 의 CHECK 매크로)
```

의존 방향: `student/course/grade` ← `file_io` ← `grade_manager` ← `statistics`, `report_exporter` ← `server` ← `main`

## 규칙

- 파일명 `snake_case.h/.cpp`, 클래스명 `PascalCase`, 함수·변수 `camelCase`
- include 할 때 대소문자 정확히 (Linux 에서는 틀리면 빌드 에러)
- 브랜치: `feat/이름-기능` (예: `feat/yeonjun-grade`) → PR → `main` 은 조장만 merge, main 직접 push 금지
- 커밋 메시지: [Conventional Commits](https://www.conventionalcommits.org/ko/) (`feat:`, `fix:`, `docs:`, `refactor:` ...)

## 학기 규칙

- 학기 형식: `YYYY-N` (예: `2026-1`), N 은 1~4 — 1·2 는 정규학기, 3 은 여름 계절학기, 4 는 겨울 계절학기 (#20)
- 학생별 허용 학기: 입학년도(2000 + 학번 앞 2자리)-1 ~ (올해+1)-4 (#16). 예: `23101001` 은 2023-1 ~ 2027-4 (2026년 기준). 범위 밖이면 성적·수강신청 등록이 `400`. 이미 저장된 데이터는 다시 검사하지 않습니다.

## 로그인 / 역할

계정은 관리자가 만듭니다 (학생을 등록해도 계정이 자동으로 생기지 않음). 관리자 계정이 하나도 없는 상태로 시작하면 (첫 실행 등) `admin` / `admin1234` (학교 관리자) 가 자동으로 만들어지고 콘솔에 안내가 나옵니다. 로그인한 뒤 비밀번호를 바꾸세요.

- 아이디: 영문·숫자·`_` 3~20자 (바꿀 수 없음), 비밀번호: 8~64자 (salt + SHA-256 반복 해시로 저장)
- 역할 `admin`(관리자) / `professor`(교수) / `student`(학생). 교수·관리자는 이름, 학생은 학번(존재하는 학생, 학생 계정끼리 중복 불가)이 필요합니다.
- 교수의 담당 과목: 과목의 담당 교수 이름(쉼표로 구분)과 계정 이름이 정확히 같은 과목
- 세션은 쿠키 `sid` (HttpOnly, 8시간)로 유지하고 서버 메모리에만 둡니다. 서버를 다시 켜면 모두 로그아웃됩니다.

| 기능 | 관리자 | 교수 | 학생 |
|---|---|---|---|
| 과목 목록·과목 통계 | O | O | O |
| 과목 등록·수정·삭제 | O | X | X |
| 학생 목록·검색 | 전체 | 담당 과목 수강생 | 본인 |
| 학생 등록·수정·삭제 | O | X | X |
| 성적 조회 | 전체 | 담당 과목 | 본인 |
| 성적 등록·수정·삭제 | O | 담당 과목 | X |
| GPA·학사경고 | O | X | 본인 |
| 석차, 전체 학사경고 | O | X | X |
| 성적표 CSV | 학생·과목 모두 | 담당 과목 | 본인 |
| 수강신청·취소 | 누구든·어느 학기든 | 수강생 조회만 | 본인, 신청 학기에만 |
| 수강신청 학기 설정, 계정 관리 | O | X | X |
| 내 정보·로그아웃·비밀번호 변경 | O | O | O |

데모 계정 (해커톤 시연용):

| 아이디 | 비밀번호 | 역할 | 이름 / 학생 |
|---|---|---|---|
| `admin` | `admin1234` | 관리자 | 학교 관리자 |
| `kimcs` | `prof1234` | 교수 | 김철수 (109001 담당) |
| `leeyh` | `prof1234` | 교수 | 이영희 (109201·109401 담당) |
| `23101001` | `student1234` | 학생 | 김민준 |
| `25103008` | `student1234` | 학생 | 윤지아 |

## 수강신청

- 신청 학기는 관리자가 설정합니다 (`settings.csv`). 비어 있으면 수강신청 기간이 아닙니다. 학생은 신청 학기에만 신청·취소할 수 있고, 관리자는 어느 학기든 대리로 신청·취소할 수 있습니다.
- 신청 검사: 중복 (`409`), 정원 (그 학기 신청 인원이 정원 이상이면 `409`), 같은 학생·학기에 신청한 과목과 강의시간 겹침 (`409`). 강의시간이 빈 과목(원격 등)은 겹치지 않습니다.
- 성적 등록은 같은 (학번, 과목, 학기)의 수강신청이 있어야 합니다 (없으면 `409`). 성적이 있는 수강은 취소할 수 없습니다 (`409`).
- 과목 목록의 수강 표시 (#14): 수강신청이 있는 가장 늦은 학기의 신청 인원 / 학기당 정원 (`GET /api/stats/course/:code` 의 `latestSemester`, `latestEnrolled`)

## 과목 페이지

- 상단 탭 줄에 보이는 과목마다 탭이 하나씩 생깁니다. 보이는 과목은 관리자는 전체, 교수는 담당 과목, 학생은 수강신청한 과목입니다. 수강과목 목록도 같은 과목만 보여 줍니다.
- 과목 페이지는 학기별로 나뉘고, 게시물은 주차(1~16)별로 묶어 보여 줍니다. 분류는 수업자료 `material` · 중간고사 `midterm` · 기말고사 `final` · 과제 `assignment` 4가지입니다.
- 게시물은 제목(필수), 내용, 링크(`http://` 또는 `https://`, 파일 업로드는 없음), 날짜(과제 마감·시험일, `2026-10-20` 또는 `2026-10-20 13:00`)를 가집니다. 작성자와 작성 시각은 서버가 채웁니다.

| 기능 | 관리자 | 교수 | 학생 |
|---|---|---|---|
| 게시물 조회 | 모든 과목 | 담당 과목 | 수강신청한 과목 (그 수강신청 학기의 게시물만) |
| 게시물 추가·수정·삭제 | O | 담당 과목 | X |

## API

요청/응답 본문은 JSON. `/api/login` 외 모든 `/api/*` 는 로그인이 필요합니다 (안 했으면 `401`, 권한 없음 `403`). 실패 시 `400` 잘못된 값 / `404` 없음 / `409` 중복·정원 초과·수강신청 규칙 위반 + `{"error": "메시지"}`

> 로그인·권한·수강신청 API의 서버 구현은 진행 중입니다 (#25). 그 전에는 웹 화면에서 로그인할 수 없습니다.

| 메서드 | 경로 | 설명 (권한) |
|---|---|---|
| POST | `/api/login` | 로그인 `{username, password}` (공개) |
| POST | `/api/logout` | 로그아웃 |
| GET | `/api/me` | 내 정보 `{username, role, name, studentId}` |
| PUT | `/api/me/password` | 내 비밀번호 변경 `{currentPassword, newPassword}` |
| GET | `/api/accounts` | 계정 목록 (관리자) |
| POST | `/api/accounts` | 계정 생성 `{username, role, name, studentId, password}` (관리자) |
| PUT | `/api/accounts/:username` | 계정 수정 `{role, name, studentId, password}`, password `""` 이면 유지 (관리자) |
| DELETE | `/api/accounts/:username` | 계정 삭제 (관리자, 자기 계정·마지막 관리자는 불가) |
| GET | `/api/students` | 학생 목록 (교수는 담당 과목 수강생, 학생은 본인만) |
| POST | `/api/students` | 학생 등록 (관리자) |
| GET | `/api/students/search?q=` | 학번·이름 검색 (목록과 같은 범위) |
| PUT | `/api/students/:id` | 학생 수정 (관리자) |
| DELETE | `/api/students/:id` | 학생 삭제 (성적·수강신청·계정도 삭제, 관리자) |
| GET | `/api/courses` | 과목 목록 |
| POST | `/api/courses` | 과목 등록 (관리자) |
| PUT | `/api/courses/:code` | 과목 수정 (관리자) |
| DELETE | `/api/courses/:code` | 과목 삭제 (성적·수강신청·게시물도 삭제, 관리자) |
| GET | `/api/courses/:code/posts` | 과목 게시물 목록 (학기·주차·id 순, 관리자·담당 교수·수강신청한 학생) |
| POST | `/api/courses/:code/posts` | 게시물 추가 `{semester, category, week, title, body, link, dueDate}`, `201` (관리자·담당 교수) |
| PUT | `/api/courses/:code/posts/:id` | 게시물 수정 (같은 본문, 관리자·담당 교수) |
| DELETE | `/api/courses/:code/posts/:id` | 게시물 삭제 `{"deleted": true}` (관리자·담당 교수) |
| GET | `/api/grades` | 성적 목록 (`?studentId=` `?courseCode=` `?semester=` 로 거르기, 교수는 담당 과목, 학생은 본인 것만) |
| POST | `/api/grades` | 성적 등록 (관리자, 교수는 담당 과목만. 수강신청이 있어야 함) |
| PUT | `/api/grades` | 성적 수정 (본문의 studentId·courseCode·semester 로 찾음, 관리자·담당 교수) |
| DELETE | `/api/grades?studentId=&courseCode=&semester=` | 성적 삭제 (관리자·담당 교수) |
| GET | `/api/stats/gpa/:id` | 학생 GPA (`?semester=`, 관리자·본인) |
| GET | `/api/stats/course/:code` | 과목 평균/최고/최저, `latestSemester`·`latestEnrolled` |
| GET | `/api/stats/ranking` | 석차표 (`?semester=`, 관리자) |
| GET | `/api/stats/warnings` | 학사경고 전체 `[{studentId, name, semester, reason, gpa, credits}]` (관리자) |
| GET | `/api/stats/warnings/:id` | 학생 학사경고 `[{semester, reason, gpa, credits}]` (관리자·본인) |
| GET | `/api/export/student/:id` | 학생 성적표 CSV (관리자·본인) |
| GET | `/api/export/course/:code` | 과목 성적표 CSV (관리자·담당 교수) |
| GET | `/api/enrollment/settings` | 수강신청 학기 `{"semester"}` |
| PUT | `/api/enrollment/settings` | 수강신청 학기 설정 `{"semester"}`, `""` 이면 신청 마감 (관리자) |
| GET | `/api/enrollments` | 수강신청 목록 (`?studentId=` `?courseCode=` `?semester=`, 교수는 담당 과목, 학생은 본인 것만) |
| GET | `/api/enrollments/counts?semester=` | 그 학기 과목별 신청 인원 `[{courseCode, count}]` |
| POST | `/api/enrollments` | 수강신청 `{studentId, courseCode, semester}` (관리자, 학생은 본인만) |
| DELETE | `/api/enrollments?studentId=&courseCode=&semester=` | 수강신청 취소 (관리자, 학생은 본인만) |

JSON 필드 이름은 헤더의 멤버 이름과 같습니다.
- 학생 `{"id", "name", "major", "year"}`
- 과목 `{"code", "name", "credit", "professor", "category", "area", "schedule", "language", "capacity", "remote"}`
  - `code` 과목코드: 숫자 6자리 (예: `109004`). 수정할 때 바꾸면 그 과목의 성적에 적힌 코드도 같이 바뀜
  - `category` 이수구분: `전공필수` `전공선택` `교양필수` `교양선택` `일반선택`
  - `language` 강의언어: `한국어전용` `영어전용` `모듈강좌(M)`
  - `schedule` 강의시간: `월(1~4)`, `화(2~3) 목(2~3)`, `금(5)` 형식 (빈 값 허용), `area` 교양영역 (빈 값 허용)
  - `capacity` 학기당 정원 (0 = 제한 없음, 최대 999). 정원은 수강신청에서 검사 (그 학기 신청 인원이 정원만큼 있으면 수강신청은 `409`)
  - `remote` 원격강좌 여부 (`true`/`false`)
  - 예전 형식 `courses.csv` (`code,name,credit,professor`) 도 읽고, 다음 저장 때 새 형식으로 바뀜
- 성적 `{"studentId", "courseCode", "semester", "score", "letter"}` — 점수만 입력하면 `letter` 는 서버가 정함 (`scoreToLetter` + 재수강 규칙 `Retake::officialLetter`)
- 수강신청 `{"studentId", "courseCode", "semester", "hasGrade"}` — 같은 (학번, 과목코드, 학기) 하나가 신청 1건
- 게시물 `{"id", "courseCode", "semester", "category", "week", "title", "body", "link", "dueDate", "author", "createdAt"}`
- 계정 `{"username", "role", "name", "studentId"}` — 응답에 비밀번호 해시는 없음. 학생 계정의 `name` 은 학생 이름으로 채워서 내보냄

## 재수강 규칙

같은 학생이 같은 과목을 여러 학기에 들으면 가장 늦은 학기가 재수강 성적이고, 그 전 성적은 삭제된 성적입니다. 자세한 내용과 함수는 `include/retake.h` 를 참고하세요.

- 재수강 성적은 A0 까지만 받습니다 (95점 이상이어도 A0, 평점 4.0).
- 삭제된 성적은 GPA, 석차, 과목 통계, 과목 성적표에서 빠집니다. `grades.csv` 에는 그대로 남습니다.
- 학생 성적표: 2018년 이후 입학생(학번 앞 2자리 ≥ 18)은 삭제된 과목을 등급 `R` 로 표시하고, 그 전 입학생은 그 줄을 뺍니다.
- 학사경고: 이미 내려진 학사경고는 유지합니다. 학사경고를 판단할 때는 재수강 규칙을 적용하지 않고 원래 성적(`scoreToLetter`)을 씁니다 (`statistics.h` 의 `academicWarnings`).
  - 기준: 정규학기 평점평균 1.75 미만, 또는 수강신청 미이행 (그 학기에 신청한 학점이 1 미만)
  - 판단 학기: 그 학생이 처음 수강신청한 학기부터, 전체 성적 데이터에 있는 가장 늦은 정규학기(N=1·2)까지 정규학기마다 봅니다. 계절학기는 판단하지 않습니다. 수강신청 기록이 없는 학생은 판단하지 않고, 휴학 데이터가 없으므로 모두 재학생으로 봅니다.
  - 각 학기: 신청 학점 합계가 1 미만이면 `수강신청 미이행`, 아니고 그 학기 성적이 1건 이상이면 학점 가중 평점(재수강으로 지워진 성적도 포함)이 1.75 미만일 때 `평점평균 1.75 미만`
  - 결과 항목 `{semester, reason, gpa, credits}` (`gpa` 가 없으면 `null`, `credits` = 그 학기 신청 학점). 샘플 데이터에서는 최유진 2025-2·2026-1, 강도윤 2026-1, 윤지아 2025-2, 한지민 2025-1 (평점)·2026-1 (수강신청 미이행)
