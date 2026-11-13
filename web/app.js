// 담당: C (원준)
// 서버 API 를 호출해서 화면에 표시 (프레임워크 없이 순수 JS)

// id 로 요소 찾기 (document.getElementById 줄임)
const $ = (id) => document.getElementById(id);
// me: 로그인한 계정 {username, role, name, studentId} (로그인 전 null)
// students·courses: 여러 탭이 같이 쓰는 학생·과목 목록 (loadBase 로 채움, 서버가 역할에 맞게 걸러 줌)
// stats: 과목코드 → 과목 통계 (수강과목 탭을 그릴 때 채움, 통계를 못 받은 과목은 null)
// enrollCtx: 수강신청 탭에서 지금 보고 있는 {학번, 학기} (신청·취소 버튼이 사용)
// myEnrollments: 학생 자기 수강신청 목록 (loadBase 가 채움, 다른 역할은 빈 목록) → 학생이 볼 수 있는 과목을 정함
// courseCode·courseSem·postFilter: 과목 페이지에서 지금 보는 과목 · 학기 · 분류 칩 ('all' = 전체)
// coursePosts: 지금 과목 페이지의 게시물 (서버가 권한에 맞게 걸러 줌, 학기·분류는 화면에서 거름)
// assessSem: 과목 페이지 '평가 기준' 영역에서 고른 학기
// assess: 그 영역에 지금 그려 둔 {code, sem, mode, items, cols} (items = 6분류 전부, cols = 점수표 열 = 비율이 0보다 큰 분류.
//   점수 칸·버튼이 어느 분류인지 찾을 때 사용, 안 그렸으면 null)
// attendSem: 과목 페이지 '출결' 영역에서 고른 학기
// attend: 그 영역에 지금 그려 둔 {code, sem, records, summaries, seq} (출결 칸이 사용, 안 그렸거나 불러오기 실패면 null)
// currentSem: 서버가 알려 준 현재 학기 (loadBase 가 채움, 예: '2026-2'). 지난 학기 판정 기준
// baseLoadedAt: loadBase 가 마지막으로 성공한 시각 (ms, 0 = 캐시 없음). showTab 이 30초(BASE_TTL_MS) 안이면 loadBase 를 건너뜀
// renderSeq: 탭을 열거나 refreshAll 할 때마다 올리는 렌더 토큰. 렌더러는 시작할 때 값을 잡아 두고, 기다린 뒤 달라졌으면 화면을 건드리지 않음
let renderSeq = 0;
// baseGen: loadBase 호출 번호. 더 새 호출이 있거나 resetView 가 불렀으면 늦게 온 옛 응답은 state 에 쓰지 않음
let baseGen = 0;
// dirtyTabs: 변경(refreshAll) 뒤 다시 그리지 못한 탭 이름. 이런 탭은 옛 값이 보이지 않게 열 때 비우고 '불러오는 중…' 으로 시작
const dirtyTabs = new Set();
const BASE_TTL_MS = 30000;
let gradePage = 1;
const GRADE_PAGE_SIZE = 10;

const state = {
    me: null, students: [], courses: [], stats: {}, accounts: [], enrollCtx: null,
    myEnrollments: [], courseCode: null, courseSem: null, postFilter: 'all', coursePosts: [],
    assessSem: null, assess: null, attendSem: null, attend: null, currentSem: '', baseLoadedAt: 0,
};

// ───────── 공용 도우미 ─────────

// HTML 에 끼워 넣을 값의 특수문자를 바꿔서 이름 등에 든 태그가 실행되지 않게 함
function esc(v) {
    return String(v ?? '').replace(/[&<>"']/g, (c) =>
        ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// 서버 API 호출. body 가 있으면 JSON 으로 보내고, 응답 JSON 을 돌려줌
// 실패(4xx/5xx)하면 서버가 준 {"error"} 메시지로 Error 를 던짐 → 호출한 쪽에서 토스트/폼에 표시
// 401(로그인 필요)이면 로그인 화면으로 돌아감. 단, 로그인 요청 자체의 401 은 로그인 폼에 오류로 표시
async function api(method, url, body) {
    const opt = { method, headers: {} };
    if (body !== undefined) {
        opt.headers['Content-Type'] = 'application/json';
        opt.body = JSON.stringify(body);
    }
    const res = await fetch(url, opt);
    const data = await res.json().catch(() => ({}));
    if (res.status === 401 && url !== '/api/login') showLogin();
    if (!res.ok) throw new Error(data.error || `요청 실패 (${res.status})`);
    return data;
}

// 화면 아래에 잠깐(2.5초) 뜨는 알림. isError 면 빨간색
let toastTimer;
function toast(msg, isError = false) {
    const el = $('toast');
    el.textContent = msg;
    el.className = 'show' + (isError ? ' error' : '');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (el.className = ''), 2500);
}

// 숫자를 소수 d 자리로, 숫자가 아니면 '-'
const fmt = (n, d = 2) => (typeof n === 'number' ? n.toFixed(d) : '-');
// 소수 둘째 자리 반올림한 숫자 (점수 표시용: 89 → 89, 79.333 → 79.33)
const round2 = (n) => Math.round(n * 100) / 100;
// GPA 응답에서 숫자만 꺼냄 ({gpa: 3.5} 또는 3.5 둘 다 처리). 성적이 없으면 서버가 null → fmt 가 '-'
const gpaOf = (r) => (typeof r === 'number' ? r : r?.gpa);
// 직접 입력할 수 있는 등급 (서버 letterToScore 가 받는 값)
const GRADE_LETTERS = ['A+', 'A0', 'B+', 'B0', 'C+', 'C0', 'D+', 'D0', 'F'];
// P/F 과목 (course.gradeType 'pf'): 성적은 P(합격)·NP(불합격) 만 (서버 kLetterPass·kLetterNonPass)
const PF_LETTERS = ['P', 'NP'];
const isPassFailCourse = (code) => state.courses.find((c) => c.code === code)?.gradeType === 'pf';
// 점수 → 등급 (서버 scoreToLetter 와 같은 경계, "이상" 기준)
const scoreToLetter = (s) => [[95, 'A+'], [90, 'A0'], [85, 'B+'], [80, 'B0'], [75, 'C+'], [70, 'C0'], [65, 'D+'], [60, 'D0']]
    .find(([min]) => s >= min)?.[1] ?? 'F';
// 등급 배지. 첫 글자(A~F, R)로 색을 정함 (style.css 의 .badge.A 등)
const letterBadge = (l) => l ? `<span class="badge ${esc(l[0])}">${esc(l)}</span>` : '-';
// 학번/과목코드 → 이름 (state 에서 찾음, 없으면 빈 문자열)
const studentName = (id) => state.students.find((s) => s.id === id)?.name ?? '';
const courseName = (code) => state.courses.find((c) => c.code === code)?.name ?? '';
// 표가 비었을 때 넣는 안내 줄
const emptyRow = (cols, msg) => `<tr><td class="empty" colspan="${cols}">${msg}</td></tr>`;
// 정원 표시 (0 = 제한 없음)
const capacityText = (c) => (c.capacity > 0 ? esc(c.capacity) : '제한 없음');
// 학기 형식 YYYY-N (1·2 정규학기, 3 여름·4 겨울 계절학기)
const SEMESTER_RE = /^\d{4}-[1-4]$/;
// 학기 형식 오류 문구 (서버 kSemesterRules 와 같음)
const SEMESTER_RULES = '학기 형식이 올바르지 않습니다 (예: 2026-1, 1·2 정규학기, 3 여름·4 겨울 계절학기)';
// 점수 범위 오류 문구 (서버 kScoreRules 와 같음)
const SCORE_RULES = '점수는 0~100 사이 숫자여야 합니다';
// 중복을 지운 학기 목록 (최신 학기가 위로, 빈 값 제외)
const sortSemesters = (list) => [...new Set(list.filter(Boolean))].sort().reverse();
// 학기 시간 순서 (서버 semester.h 와 같음): 1학기 < 여름(3) < 2학기 < 겨울(4)
const SEMESTER_ORDER = { 1: 0, 3: 1, 2: 2, 4: 3 };
// sem 이 cur(현재 학기)보다 지난 학기인지. 형식이 틀리면 false (미래 학기·현재 학기도 false)
function isPastSemester(sem, cur) {
    if (!SEMESTER_RE.test(sem ?? '') || !SEMESTER_RE.test(cur ?? '')) return false;
    const rank = (s) => Number(s.slice(0, 4)) * 4 + SEMESTER_ORDER[s[5]];
    return rank(sem) < rank(cur);
}

// 평가 기준·항목 점수 API 의 과목·학기 쿼리
const assessQuery = (code, sem) => new URLSearchParams({ courseCode: code, semester: sem }).toString();
// (과목, 학기) 평가 기준 {mode, items}. cache(Map) 에 요청을 담아 같은 조합은 한 번만 부름 (실패하면 {mode:'absolute', items:[]})
function assessmentsOf(code, sem, cache) {
    const k = JSON.stringify([code, sem]);
    if (!cache.has(k)) {
        cache.set(k, api('GET', '/api/assessments?' + assessQuery(code, sem)).catch(() => ({ mode: 'absolute', items: [] })));
    }
    return cache.get(k);
}
// 평가 분류 6개 (서버 assessmentCategories() 와 같은 순서·글자)
const ASSESS_CATEGORIES = ['출석', '중간고사', '기말고사', '과제', '팀프로젝트', '기타'];
// 출석부에서 자동 계산되는 분류 (서버 kAttendanceCategory). 점수표에서 늘 읽기 전용
const ATTEND_ITEM = '출석';
const ATTEND_LOCK_MSG = '출석 점수는 출석부에서 자동 계산됩니다 (아래 출결 영역에서 고치세요)';
// 상대평가 규칙 안내 (서버 RelativeGrading 상수와 같은 값)
const RELATIVE_RULES = 'A0 이상 30%·B0 이상 70% 이하, 평균 3.50 이하';
// 등급 → 평점 (서버 letterToPoint 와 같음, F·R·모르는 값은 0)
const LETTER_POINT = { 'A+': 4.5, A0: 4.0, 'B+': 3.5, B0: 3.0, 'C+': 2.5, C0: 2.0, 'D+': 1.5, D0: 1.0 };

// ───────── 역할 ─────────

const ROLE_LABEL = { admin: '관리자', professor: '교수', student: '학생' };
const isAdmin = () => state.me?.role === 'admin';
// 이 사용자가 접근할 수 없는 지난 학기인지 (관리자는 성적 정정용으로 모든 학기 가능)
const isLockedSemester = (sem) => !isAdmin() && isPastSemester(sem, state.currentSem);
// 서버가 지난 학기 요청에 돌려주는 문구와 같음
const pastSemesterMessage = () => `지난 학기는 접근할 수 없습니다 (현재 학기: ${state.currentSem})`;
// 학기 선택 상자에 넣을 학기 목록 (최신 학기가 위로): 현재 학기는 항상 넣고, 관리자 외에는 지난 학기를 뺌
const visibleSemesters = (list) => sortSemesters([...list, state.currentSem]).filter((s) => !isLockedSemester(s));
// 담당 과목 판정 (서버 teaches 와 같은 규칙): 교수 계정이고, 과목 담당 교수를 , 로 나눈 이름 중 하나가 내 이름
const teaches = (c) => state.me?.role === 'professor' &&
    String(c.professor ?? '').split(',').map((s) => s.trim()).includes(state.me.name);
// 성적 탭에서 고를 수 있는 과목: 관리자는 전체, 교수는 담당 과목만
const gradeCourses = () => (isAdmin() ? state.courses : state.courses.filter(teaches));
// 수강과목 목록에 보이는 과목: 관리자는 전체, 교수는 담당 과목,
//   학생은 현재 학기 이후(현재·미래 학기) 수강신청이 있는 과목 (지난 학기만 수강한 과목은 뺌)
const myCourses = () => {
    if (isAdmin()) return state.courses;
    if (state.me?.role === 'professor') return state.courses.filter(teaches);
    if (state.me?.role !== 'student') return [];
    const mine = new Set(state.myEnrollments.filter((e) => !isPastSemester(e.semester, state.currentSem)).map((e) => e.courseCode));
    return state.courses.filter((c) => mine.has(c.code));
};

// ───────── 대화상자 ─────────

// 등록·수정 폼과 과목 상세·성적표 창이 같이 쓰는 <dialog> 하나
// dlgSubmit: 저장 버튼을 눌렀을 때 실행할 함수 (없으면 저장 버튼을 숨김 = 보기 전용 창)
// dlgValidate: 저장 전에 폼 값을 검사하는 함수 (잘못되면 이유 문자열을 돌려줌 → 서버에 보내지 않고 창 안에 표시)
const dlg = $('dlg');
let dlgSubmit = null;
let dlgValidate = null;
// 저장 요청 중복 방지: 요청을 보내는 동안 dlgBusy = true, 저장 버튼 비활성화
//   dlgSeq: 창을 열 때마다 1 증가. 저장 도중 창을 닫고 다른 창을 연 경우,
//   늦게 끝난 이전 요청이 새 창을 닫거나 버튼을 건드리지 않게 하려고 씀
let dlgBusy = false;
let dlgSeq = 0;

function setDlgBusy(busy) {
    dlgBusy = busy;
    $('dlg-ok').disabled = busy;
}

// 제목·내용을 채우고 창을 띄움
// validate 가 있으면 브라우저 기본 검사(required·min·pattern)를 끄고 validate 문구만 보임 (서버와 같은 문구)
function openDialog({ title, bodyHtml, onSubmit, validate = null, okText = '저장' }) {
    dlgSeq++;
    setDlgBusy(false);
    $('dlg-title').textContent = title;
    $('dlg-body').innerHTML = bodyHtml;
    $('dlg-ok').textContent = okText;
    $('dlg-ok').style.display = onSubmit ? '' : 'none';
    $('dlg-form').noValidate = !!validate;
    dlgSubmit = onSubmit;
    dlgValidate = validate;
    dlg.showModal();
}

$('dlg-cancel').onclick = () => dlg.close();
// 저장: 폼 값을 객체로 모아 dlgSubmit 에 넘김. 성공하면 창을 닫고, 실패하면 창 안에 오류 메시지 표시
//   요청이 끝나기 전에 다시 누르면(버튼 연타·Enter 반복) 무시 → 같은 등록이 여러 번 가지 않음
//   실패하면 버튼을 다시 살려서 고친 뒤 다시 저장할 수 있게 함
$('dlg-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    if (!dlgSubmit || dlgBusy) return;
    const data = Object.fromEntries(new FormData($('dlg-form')));
    const box = $('dlg-body').querySelector('.form-error');
    const invalid = dlgValidate?.(data);
    if (invalid) {
        if (box) box.textContent = invalid;
        toast(invalid, true);
        return;
    }
    const seq = dlgSeq;
    setDlgBusy(true);
    try {
        await dlgSubmit(data);
        if (seq !== dlgSeq) return;  // 저장 중에 다른 창이 열림 (onSubmit 이 연 창 포함) → 그 창은 그대로 둠
        dlg.close();
    } catch (err) {
        if (seq !== dlgSeq) return;
        if (box) box.textContent = err.message;
        toast(err.message, true);
    } finally {
        if (seq === dlgSeq) setDlgBusy(false);
    }
});

// 입력 폼 창 만들기
//   fields: [{name, label, type, readonly, optional, step, min, max, pattern, hint, placeholder, autocomplete, options:[{value,label}]}]
//     min·max·pattern·hint 는 입력칸 검사용 (hint 는 pattern 이 안 맞을 때 브라우저가 보여 줄 설명)
//     options 가 있으면 선택 상자, type 이 'textarea' 면 여러 줄 입력칸, 그 외는 입력칸
//     readonly 는 수정할 때 키(학번 등)를 못 바꾸게 할 때 사용
//     optional 이면 빈칸으로 저장할 수 있음 (그 외 입력칸은 필수)
//     autocomplete 를 주지 않으면 "off": 브라우저 자동완성이 저장된 이름·주소 등으로 칸을 덮어쓰지 않게 함
//       (예: 학생 수정의 name="name" 칸을 브라우저가 다른 이름으로 채우는 경우). 비밀번호 칸은 'new-password' 처럼 직접 지정
//   values: 처음 채워둘 값, onSubmit: 저장할 때 실행할 함수 (폼 값 객체를 받음)
//   validate: 저장 전에 폼 값을 검사하는 함수 (잘못되면 이유 문자열을 돌려줌, 서버 호출 전에 막음)
function openForm({ title, fields, values = {}, onSubmit, validate }) {
    const html = fields.map((f) => {
        const v = esc(values[f.name] ?? '');
        const ac = `autocomplete="${esc(f.autocomplete || 'off')}"`;
        let input;
        if (f.options) {
            input = `<select name="${f.name}" ${ac} ${f.readonly ? 'disabled' : ''}>` +
                f.options.map((o) =>
                    `<option value="${esc(o.value)}" ${String(o.value) === String(values[f.name]) ? 'selected' : ''}>${esc(o.label)}</option>`
                ).join('') + '</select>';
            // disabled 항목은 FormData 에서 빠지므로 숨은 값으로 보냄
            if (f.readonly) input += `<input type="hidden" name="${f.name}" value="${v}">`;
        } else if (f.type === 'textarea') {
            // 시작 태그 바로 뒤 줄바꿈 하나는 HTML 이 버리므로, 값이 줄바꿈으로 시작해도 남도록 하나를 먼저 넣음
            input = `<textarea name="${f.name}" rows="6" ${ac} ${f.placeholder ? `placeholder="${esc(f.placeholder)}"` : ''}
                ${f.optional ? '' : 'required'}>\n${v}</textarea>`;
        } else {
            input = `<input name="${f.name}" type="${f.type || 'text'}" value="${v}" ${f.step ? `step="${f.step}"` : ''}
                ${f.min !== undefined ? `min="${f.min}"` : ''} ${f.max !== undefined ? `max="${f.max}"` : ''}
                ${f.pattern ? `pattern="${esc(f.pattern)}"` : ''} ${f.hint ? `title="${esc(f.hint)}"` : ''}
                ${f.readonly ? 'readonly' : ''} ${f.placeholder ? `placeholder="${esc(f.placeholder)}"` : ''}
                ${ac} ${f.optional ? '' : 'required'}>`;
        }
        return `<div class="field"><label>${f.label}</label>${input}</div>`;
    }).join('') + '<div class="form-error"></div>';
    openDialog({ title, bodyHtml: html, onSubmit, validate });
    const first = $('dlg-body').querySelector('input:not([readonly]):not([type=hidden])');
    if (first) first.focus();
}

// 확인창을 띄우고, 확인하면 fn(삭제 API 호출) 실행 후 목록 새로고침
async function confirmDelete(msg, fn) {
    if (!confirm(msg)) return;
    try {
        await fn();
        toast('삭제했습니다');
        await refreshAll();
    } catch (e) { toast(e.message, true); }
}

// ───────── 탭 ─────────

// 탭 이름 → 그 탭을 그리는 함수 (course = 과목 페이지, 어떤 과목인지는 state.courseCode)
const renderers = {
    courses: renderCourses,
    enroll: renderEnroll,
    students: renderStudents,
    grades: renderGrades,
    myreport: renderMyReport,
    accounts: renderAccounts,
    course: renderCoursePage,
};
// 지금 열린 탭 이름 (보이는 화면 section 의 id 'tab-이름' 에서 꺼냄)
const currentTab = () => document.querySelector('main .tab.active').id.slice('tab-'.length);

// 탭 이름 → 처음 열 때 (그 화면이 비어 있을 때만) 보일 '불러오는 중…' 자리. 이미 그려 둔 탭은 이전 내용을 그대로 둠
const loadingSlots = {
    courses: ['course-list', '<li class="empty">불러오는 중…</li>'],
    enroll: ['enroll-content', '<p class="hint">불러오는 중…</p>'],
    students: ['student-body', emptyRow(7, '불러오는 중…')],
    grades: ['grade-body', emptyRow(7, '불러오는 중…')],
    myreport: ['myreport-body', '<p class="hint">불러오는 중…</p>'],
    accounts: ['account-body', emptyRow(5, '불러오는 중…')],
    course: ['course-posts', '<p class="hint">불러오는 중…</p>'],
};

// 과목 페이지 화면을 비움 (다른 과목으로 옮길 때 이전 과목 내용이 잠깐이라도 보이지 않게)
function clearCoursePage() {
    state.coursePosts = [];
    state.assess = null;
    state.attend = null;
    ['course-hero', 'course-filter', 'course-posts', 'course-sem',
        'assess-sem', 'assess-body', 'attend-sem', 'attend-body'].forEach((id) => ($(id).innerHTML = ''));
    delete $('attend-body').dataset.key;
}

// 탭 버튼·화면을 바꾸고 그 탭을 그림 (탭 이동마다 렌더 토큰을 올려, 늦게 오는 옛 탭 응답이 화면을 덮지 않게 함)
//   이미 그려 둔 탭은 이전 내용을 그대로 보이고 서버 응답이 오면 제자리 갱신, 처음 여는 탭만 '불러오는 중…' 을 보임
//   학생·과목 목록(loadBase)은 30초 안이면 다시 받지 않음 (openTab)
//   등록·수정·삭제(refreshAll) 뒤에는 지금 탭만 다시 그리므로, 나머지 탭은 dirtyTabs 에 넣어 다음에 열 때 옛 값을 비우고 새로 받음
//   code: 과목 페이지면 그 과목코드. 다른 과목으로 바뀌면 학기·분류·평가 기준·출결 학기 선택을 처음으로 되돌리고 화면도 비움
function showTab(name, code, assessSemester = null) {
    if (code && code !== state.courseCode) {
        state.courseCode = code;
        state.courseSem = null;
        state.postFilter = 'all';
        state.assessSem = null;
        state.attendSem = null;
        clearCoursePage();
    }

    if (name === 'course' && assessSemester) {
        state.assessSem = assessSemester;
    }

    document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('active',
        b.dataset.tab === (name === 'course' ? 'courses' : name)));
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.id === 'tab-' + name));
    const seq = ++renderSeq;
    openTab(name, seq).catch((e) => { if (seq === renderSeq) toast(e.message, true); });
}
$('tabs').onclick = (e) => {
    const b = e.target.closest('button[data-tab]');
    if (b && !b.hidden) showTab(b.dataset.tab);
};

// showTab 의 본체. loadBase 캐시 나이에 따라
//   30초 미만: loadBase 없이 바로 그림
//   캐시 없음(로그인 직후): loadBase 를 받은 뒤 그림 (그릴 목록이 아직 없음)
//   30초 이상: 캐시로 먼저 그리면서 loadBase 를 같이 받고, 목록이 바뀌었으면 한 번 더 그림
async function openTab(name, seq) {
    if (!state.me) return;
    if (dirtyTabs.delete(name)) {
        if (name === 'course') clearCoursePage();
        else $(loadingSlots[name][0]).innerHTML = '';
    }
    const slot = loadingSlots[name];
    if ($(slot[0]).innerHTML === '') $(slot[0]).innerHTML = slot[1];
    if (Date.now() - state.baseLoadedAt < BASE_TTL_MS) { await renderTab(); return; }
    if (!state.baseLoadedAt) {
        await loadBase();
        if (seq === renderSeq) await renderTab();
        return;
    }
    const [changed] = await Promise.all([loadBase(), renderTab()]);
    if (changed && seq === renderSeq) await renderTab();
}

// 학생·과목 목록과 현재 학기를 서버에서 받아 state 에 저장 (학생 목록은 서버가 역할에 맞게 걸러 줌)
// 학생이면 자기 수강신청도 받음 (서버가 자기 것만 줌) → myCourses 가 사용
// 받은 값이 이전과 다르면 true (openTab 이 그 탭을 다시 그릴지 정함). 더 새 호출이 있거나 resetView 가 불렀으면 쓰지 않고 false
async function loadBase() {
    const gen = ++baseGen;
    const [students, courses, myEnrollments, cur] = await Promise.all([
        api('GET', '/api/students'),
        api('GET', '/api/courses'),
        state.me.role === 'student' ? api('GET', '/api/enrollments') : [],
        api('GET', '/api/semester/current'),
    ]);
    if (gen !== baseGen) return false;
    const next = { students, courses, myEnrollments, currentSem: cur.semester ?? '' };
    const changed = Object.keys(next).some((k) => JSON.stringify(state[k]) !== JSON.stringify(next[k]));
    Object.assign(state, next);
    state.baseLoadedAt = Date.now();
    return changed;
}

// 지금 열린 탭을 그림. 과목 페이지를 보던 중 그 과목을 볼 수 없게 되면(과목 삭제·권한 변경) 수강과목 탭으로 돌아감
async function renderTab() {
    const name = currentTab();
    if (name === 'course' && !myCourses().some((c) => c.code === state.courseCode)) {
        clearCoursePage();
        showTab('courses');
        return;
    }
    await renderers[name]();
}

// 등록·수정·삭제 뒤에 호출: 캐시를 쓰지 않고 목록을 항상 새로 받은 뒤 지금 탭을 다시 그림 (변경 직후 최신 값이 보여야 함)
async function refreshAll() {
    if (!state.me) return;
    const seq = ++renderSeq;
    for (const n of Object.keys(renderers)) if (n !== currentTab()) dirtyTabs.add(n);
    state.baseLoadedAt = 0;  // 기다리는 사이 다른 탭이 열려도 옛 캐시를 쓰지 않게
    await loadBase();
    if (seq !== renderSeq) return;  // 그 사이 탭을 옮겼으면 그 탭의 렌더가 맡음
    await renderTab();
}

// 성적에 나온 학기 목록 (최신 학기가 위로). 학기 선택 상자에 사용
async function semesterList() {
    const grades = await api('GET', '/api/grades');
    return sortSemesters(grades.map((g) => g.semester));
}

// 선택 상자 채우기. allLabel 이 있으면 맨 위에 "전체" 항목(값 '')을 넣고,
// keep(이전 선택값)이 새 목록에도 있으면 그대로 선택해 둠
function fillSelect(el, items, allLabel, keep) {
    el.innerHTML = (allLabel ? `<option value="">${allLabel}</option>` : '') +
        items.map((i) => `<option value="${esc(i.value)}">${esc(i.label)}</option>`).join('');
    if (keep && [...el.options].some((o) => o.value === keep)) el.value = keep;
}

// ───────── 로그인 ─────────

// 화면에 그려 둔 목록·선택 상자를 모두 비움 (다른 역할로 다시 로그인했을 때 이전 화면이 남지 않게)
function resetView() {
    renderSeq++;
    dirtyTabs.clear();
    baseGen++;
    state.baseLoadedAt = 0;
    gradePage = 1;
    state.students = [];
    state.courses = [];
    state.stats = {};
    state.accounts = [];
    state.enrollCtx = null;
    state.myEnrollments = [];
    state.currentSem = '';
    state.courseCode = null;
    state.courseSem = null;
    state.postFilter = 'all';
    state.coursePosts = [];
    state.assessSem = null;
    state.assess = null;
    state.attendSem = null;
    state.attend = null;
    ['course-list', 'student-body', 'grade-body', 'enroll-content', 'myreport-body', 'account-body',
        'grade-f-student', 'grade-f-course', 'grade-f-semester',
        'enroll-student', 'enroll-course', 'enroll-sem-select',
        'course-hero', 'course-filter', 'course-posts', 'course-sem',
        'assess-sem', 'assess-body', 'attend-sem', 'attend-body', 'grade-pagination'].forEach((id) => ($(id).innerHTML = ''));
    ['student-search', 'enroll-sem-input', 'enroll-set-sem'].forEach((id) => ($(id).value = ''));
    delete $('enroll-sem-select').dataset.course;
    delete $('attend-body').dataset.key;
    $('enroll-period').textContent = '';
}

// 로그인 화면 표시: 상단 탭·사용자 상자·본문을 숨김
function showLogin() {
    state.me = null;
    if (dlg.open) dlg.close();
    resetView();
    $('tabs').hidden = true;
    $('user-box').hidden = true;
    $('app').hidden = true;
    $('login-view').hidden = false;
    $('login-error').textContent = '';
    $('login-password').value = '';
    document.querySelectorAll('.demo-account').forEach((b) => b.classList.remove('selected'));
    $('login-username').focus();
}

// 로그인한 계정으로 앱 시작: 역할에 허용된 탭만 보이고 첫 탭(수강과목)을 엶
function startApp(me) {
    state.me = me;
    resetView();
    $('user-label').textContent = `${me.name || me.username} · ${ROLE_LABEL[me.role] ?? me.role}`;
    document.querySelectorAll('#tabs button[data-roles]').forEach((b) => (b.hidden = !b.dataset.roles.split(' ').includes(me.role)));
    $('course-add').hidden = me.role !== 'admin';
    $('login-view').hidden = true;
    $('tabs').hidden = false;
    $('user-box').hidden = false;
    $('app').hidden = false;
    showTab('courses');
}

// 시연 계정 버튼: 아이디·비밀번호를 채우고 로그인 버튼에 포커스 (Enter 나 클릭 한 번으로 로그인)
document.querySelectorAll('.demo-account').forEach((b) => {
    b.addEventListener('click', () => {
        $('login-username').value = b.dataset.username;
        $('login-password').value = b.dataset.password;
        $('login-error').textContent = '';
        document.querySelectorAll('.demo-account').forEach((other) => other.classList.toggle('selected', other === b));
        $('login-form').querySelector('button[type="submit"]').focus();
    });
});

// 로그인 폼 (Enter 로도 제출). 실패하면 폼 안에 서버 메시지 표시
$('login-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    $('login-error').textContent = '';
    const d = Object.fromEntries(new FormData($('login-form')));
    try {
        const me = await api('POST', '/api/login', { username: d.username, password: d.password });
        $('login-form').reset();
        startApp(me);
    } catch (err) {
        $('login-error').textContent = err.message;
        $('login-password').value = '';
        $('login-password').focus();
    }
});

// 내 비밀번호 변경. 새 비밀번호 두 칸이 다르면 서버에 보내지 않고 폼 오류
$('pw-change').onclick = () => openForm({
    title: '비밀번호 변경',
    fields: [
        { name: 'currentPassword', label: '현재 비밀번호', type: 'password', autocomplete: 'current-password' },
        { name: 'newPassword', label: '새 비밀번호 (8~64자)', type: 'password', autocomplete: 'new-password' },
        { name: 'newPassword2', label: '새 비밀번호 확인', type: 'password', autocomplete: 'new-password' },
    ],
    onSubmit: async (d) => {
        if (d.newPassword !== d.newPassword2) throw new Error('새 비밀번호가 서로 다릅니다');
        await api('PUT', '/api/me/password', { currentPassword: d.currentPassword, newPassword: d.newPassword });
        toast('비밀번호를 바꿨습니다');
    },
});

// 로그아웃: 서버 세션을 지우고 로그인 화면으로
$('logout').onclick = async () => {
    try { await api('POST', '/api/logout'); }
    catch { /* 이미 세션이 끝났어도 로그인 화면으로 */ }
    showLogin();
};

// ───────── 과목 ─────────

// 과목 등록/수정 폼 항목. 과목코드는 숫자 6자리 (예: 109004)
//   수정 때 과목코드를 바꾸면 서버가 그 과목의 성적에 적힌 코드도 같이 바꿈 (GradeManager::updateCourse)
//   이수구분·강의언어 목록은 서버(course.cpp)의 courseCategories / courseLanguages 와 같아야 함
const COURSE_CATEGORIES = ['전공필수', '전공선택', '교양필수', '교양선택', '일반선택'];
const COURSE_LANGUAGES = ['한국어전용', '영어전용', '모듈강좌(M)'];
const asOptions = (list) => list.map((v) => ({ value: v, label: v }));
const courseFields = () => [
    { name: 'code', label: '과목 코드 (숫자 6자리)', placeholder: '예: 109004', pattern: '\\d{6}', hint: '과목 코드는 숫자 6자리여야 합니다' },
    { name: 'name', label: '과목명' },
    { name: 'category', label: '이수구분', options: asOptions(COURSE_CATEGORIES) },
    { name: 'credit', label: '학점 (1~4)', type: 'number', min: 1, max: 4, step: 1 },
    { name: 'professor', label: '담당 교수', optional: true },
    { name: 'schedule', label: '강의시간', optional: true, placeholder: '예: 월(1~4) 또는 화(2~3) 목(2~3)' },
    { name: 'language', label: '강의언어', options: asOptions(COURSE_LANGUAGES) },
    { name: 'area', label: '교양영역 (교양 과목만)', optional: true, placeholder: '예: 인문, 자연과학' },
    { name: 'capacity', label: '학기당 정원 (0 = 제한 없음)', type: 'number' },
    { name: 'remote', label: '원격강좌', options: [{ value: 'N', label: '아니오' }, { value: 'Y', label: '예' }] },
    // 성적이나 평가 기준이 있는 과목은 서버가 바꾸지 못하게 막음 (409)
    { name: 'gradeType', label: '성적 방식', options: [
        { value: 'letter', label: '등급 (A+~F, 평점 반영)' },
        { value: 'pf', label: 'P/F (합격·불합격, 평점 제외)' },
    ] },
];
// 과목 폼 값 검사 (서버 courseInvalidReason 과 같은 규칙·문구): 잘못되면 이유, 괜찮으면 빈 문자열
const validateCourse = (d) => {
    if (!/^\d{6}$/.test(d.code ?? '')) return '과목코드는 숫자 6자리여야 합니다';
    if (!(d.name ?? '').trim()) return '과목명이 비어 있습니다';
    const credit = Number(d.credit);
    if ((d.credit ?? '').trim() === '' || !Number.isInteger(credit) || credit < 1 || credit > 4) return '학점은 1~4 사이여야 합니다';
    const capacity = Number(d.capacity);
    if (!Number.isInteger(capacity) || capacity < 0 || capacity > 999) return '정원은 0~999 사이여야 합니다';
    return '';
};
// 폼 값 → API 본문 (숫자·참거짓 변환)
const courseBody = (d) => ({ ...d, credit: Number(d.credit), capacity: Number(d.capacity), remote: d.remote === 'Y' });
// API 값 → 폼 처음 값 (remote 는 선택 상자 값 Y/N 으로)
const courseFormValues = (c) => ({ ...c, remote: c.remote ? 'Y' : 'N', gradeType: c.gradeType || 'letter' });

// 과목 목록 그리기 (myCourses: 교수는 담당 과목, 학생은 수강신청한 과목만)
// 과목마다 통계 API 도 같이 호출해 state.stats 에 저장 (못 받으면 null)
// 한 줄에 과목 정보(왼쪽)와 최근 학기 수강 인원·평균(오른쪽). 성적이 없으면 평균은 '-'
//   학기 = 교수·관리자는 수강신청이 있는 학기 중 볼 수 있는 가장 최근 학기 (없으면 현재 학기, staffSemesterOf),
//     학생은 본인 수강신청 중 이 과목의 현재 학기 이후 가장 최근 학기 (둘 다 과목 페이지에서 처음 보이는 학기와 같음)
async function renderCourses() {
    const seq = renderSeq;
    const list = $('course-list');
    const courses = myCourses();
    const stats = await api('GET', '/api/stats/course/batch').catch(() => []);
    if (seq !== renderSeq) return;
    const statsByCode = Object.fromEntries(stats.map((item) => [item.courseCode, item]));
    state.stats = Object.fromEntries(courses.map((c) => [c.code, statsByCode[c.code] ?? null]));
    if (!courses.length) {
        const msg = { professor: '담당 과목이 없습니다', student: '수강신청한 과목이 없습니다' }[state.me.role] ?? '등록된 과목이 없습니다';
        list.innerHTML = `<li class="empty">${msg}</li>`;
        return;
    }
    const cache = new Map();
    const latest = await Promise.all(courses.map((c) => courseLatest(c.code, state.stats[c.code], cache)));
    if (seq !== renderSeq) return;
    list.innerHTML = courses.map((c, i) => {
        const st = state.stats[c.code];
        const has = st && st.count > 0;
        const { sem, enrolled } = latest[i];
        return `<li class="course-row" data-code="${esc(c.code)}">
            <div class="course-info">
                <div class="course-name">${esc(c.name)}${c.area ? ` <span class="tag">${esc(c.area)}</span>` : ''}</div>
                <div class="course-line">${esc(c.code)} · ${esc(c.professor) || '-'} · ${esc(c.category)}</div>
                <div class="course-line">${esc(c.schedule) || '-'} · ${esc(c.credit)}학점 · ${esc(c.language)}${c.remote ? ' <span class="tag">원격</span>' : ''}</div>
            </div>
            <div class="course-meta">
                <span class="stat-line">${sem ? esc(sem) + ' ' : ''}수강 <b>${sem ? esc(enrolled) : 0}</b> / ${capacityText(c)}</span>
                <span class="stat-line">${has ? `평균 <b>${fmt(st.average, 1)}</b> (${fmt(st.max, 0)} / ${fmt(st.min, 0)})` : '평균 -'}</span>
            </div>
        </li>`;
    }).join('');
}

// 학생 카드·과목 정보에 보일 학기: 본인 수강신청 중 이 과목의 현재 학기 이후 가장 최근 학기 (없으면 '')
const mySemesterOf = (code) => sortSemesters(state.myEnrollments
    .filter((e) => e.courseCode === code && !isPastSemester(e.semester, state.currentSem))
    .map((e) => e.semester))[0] ?? '';
// 교수·관리자 카드·과목 정보·과목 페이지 처음 학기: 이 과목에 수강신청이 있는 학기 중
//   볼 수 있는(관리자 외에는 지난 학기 제외) 가장 최근 학기. 없으면 현재 학기
//   semesters: 수강신청이 있는 학기 목록 (통계의 enrolledBySemester 키, 또는 과목 페이지가 받은 수강신청 목록)
const staffSemesterOf = (semesters) => sortSemesters(semesters).find((s) => !isLockedSemester(s)) ?? state.currentSem;
// 카드·과목 정보에 보일 {학기, 수강 인원}. 학생은 위 학기 기준이라 그 학기의 과목별 신청 인원(/api/enrollments/counts)을 씀
//   cache: 학기 → 인원 요청 (학기당 한 번만 부르려고 호출하는 쪽이 공유)
//   교수·관리자는 staffSemesterOf 학기와 그 학기 인원 (통계의 enrolledBySemester)
async function courseLatest(code, st, cache = new Map()) {
    if (state.me.role !== 'student') {
        const bySem = st?.enrolledBySemester ?? {};
        const sem = staffSemesterOf(Object.keys(bySem));
        return { sem, enrolled: bySem[sem] ?? 0 };
    }
    const sem = mySemesterOf(code);
    if (!sem) return { sem: '', enrolled: 0 };
    if (!cache.has(sem)) cache.set(sem, api('GET', '/api/enrollments/counts?semester=' + encodeURIComponent(sem)));
    const counts = await cache.get(sem);
    return { sem, enrolled: counts.find((x) => x.courseCode === code)?.count ?? 0 };
}

// 과목 상세 창 (보기 전용): 전체 항목 + 통계 상자 + CSV / 수정 / 삭제 버튼
//   CSV 는 관리자·담당 교수만, 수정·삭제는 관리자만
function showCourseDetail(c, st, latest) {
    const has = st && st.count > 0;
    const { sem, enrolled } = latest;
    const rows = [
        ['이수구분', esc(c.category)],
        ['학점', esc(c.credit)],
        ['담당 교수', esc(c.professor) || '-'],
        ['강의시간', esc(c.schedule) || '-'],
        ['강의언어', esc(c.language)],
        ['교양영역', esc(c.area) || '-'],
        ['정원', c.capacity > 0 ? esc(c.capacity) + '명' : '제한 없음'],
        ['원격강좌', c.remote ? '예' : '아니오'],
        ['성적 방식', c.gradeType === 'pf' ? 'P/F (합격·불합격, 평점 제외)' : '등급 (A+~F)'],
    ];
    const stat = (label, v, sub) => `<div><span>${label}</span><b>${v}</b>${sub ? `<small>${sub}</small>` : ''}</div>`;
    const actions = [
        isAdmin() || teaches(c) ? `<a class="btn-link" href="/api/export/course/${encodeURIComponent(c.code)}">CSV 내려받기</a>` : '',
        isAdmin() ? '<button type="button" data-act="edit">수정</button><button type="button" class="danger" data-act="del">삭제</button>' : '',
    ].join('');
    openDialog({
        title: `${c.name} (${c.code})`,
        bodyHtml: `<dl class="detail-grid">${rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('')}</dl>
            <div class="stat-boxes">
                ${stat('최근 학기 수강', sem ? esc(enrolled) + '명' : '-', sem ? esc(sem) : '')}
                ${stat('평균', has ? fmt(st.average, 1) : '-')}
                ${stat('최고', has ? fmt(st.max, 0) : '-')}
                ${stat('최저', has ? fmt(st.min, 0) : '-')}
            </div>
            ${actions ? `<div class="detail-actions">${actions}</div>` : ''}`,
    });
    const edit = $('dlg-body').querySelector('[data-act=edit]');
    const del = $('dlg-body').querySelector('[data-act=del]');
    if (edit) edit.onclick = () => { dlg.close(); editCourse(c); };
    if (del) del.onclick = () => { dlg.close(); deleteCourse(c); };
}

// 과목 수정 폼
function editCourse(c) {
    openForm({
        title: '과목 수정',
        fields: courseFields(),
        values: courseFormValues(c),
        validate: validateCourse,
        onSubmit: async (d) => {
            await api('PUT', '/api/courses/' + encodeURIComponent(c.code), courseBody(d));
            // 과목 페이지에서 과목코드를 바꿨으면 새 코드의 과목 페이지에 그대로 머묾
            if (state.courseCode === c.code) state.courseCode = d.code;
            toast('수정했습니다');
            await refreshAll();
        },
    });
}

// 과목 삭제 (확인 후)
function deleteCourse(c) {
    confirmDelete(
        `${c.name}(${c.code}) 과목을 삭제할까요?\n이 과목의 성적·수강신청·게시물도 함께 삭제됩니다.`,
        () => api('DELETE', '/api/courses/' + encodeURIComponent(c.code)));
}

// 행을 누르면 그 과목의 과목 페이지 (상세 창은 과목 페이지의 "과목 정보" 버튼)
$('course-list').onclick = (e) => {
    const row = e.target.closest('.course-row');
    if (row) showTab('course', row.dataset.code);
};

$('course-add').onclick = () => openForm({
    title: '과목 등록',
    fields: courseFields(),
    values: { category: '전공선택', credit: 3, language: '한국어전용', capacity: 0, remote: 'N' },
    validate: validateCourse,
    onSubmit: async (d) => {
        await api('POST', '/api/courses', courseBody(d));
        toast('과목을 등록했습니다');
        await refreshAll();
    },
});

// ───────── 과목 페이지 ─────────

// 게시물 분류 (서버 postCategories 와 같은 값). icon: 목록 아이콘 글자, dateLabel: 날짜 칸 이름
const POST_CATEGORIES = [
    { value: 'material', label: '수업자료', icon: '자', dateLabel: '날짜' },
    { value: 'midterm', label: '중간고사', icon: '중', dateLabel: '시험일' },
    { value: 'final', label: '기말고사', icon: '기', dateLabel: '시험일' },
    { value: 'assignment', label: '과제', icon: '과', dateLabel: '마감' },
];
const postCategory = (v) => POST_CATEGORIES.find((k) => k.value === v) ?? { value: v, label: v, icon: '?', dateLabel: '날짜' };
// 과목의 게시물 API 주소
const postsUrl = (code) => '/api/courses/' + encodeURIComponent(code) + '/posts';
// 지금 과목 페이지의 과목 (없으면 undefined)
const pageCourse = () => state.courses.find((c) => c.code === state.courseCode);
// 게시물·평가 기준을 추가·수정·삭제할 수 있는지: 관리자 또는 담당 교수
const canManageCourse = (c) => isAdmin() || teaches(c);
// 마감·시험일이 지났는지 (날짜만 있으면 그날 23:59 기준, 시각은 브라우저 지역 시간)
function isExpired(due) {
    if (!due) return false;
    const t = new Date((due.length === 10 ? due + ' 23:59' : due).replace(' ', 'T'));
    return !Number.isNaN(t.getTime()) && t < new Date();
}

// 과목 페이지 그리기: 게시물과 학기 목록을 받고 학기를 정한 뒤 drawPosts 로 그림
//   학기 목록: 관리자·교수는 게시물 학기 + 이 과목 수강신청 학기 + 신청 학기, 학생은 자기가 수강신청한 학기
//     (visibleSemesters: 현재 학기는 항상 있고, 관리자 외에는 지난 학기가 없음)
//   처음 보이는 학기: 이전 선택(목록에 있으면) → 수강과목 카드에 보인 학기 (학생: mySemesterOf, 관리자·교수: staffSemesterOf)
//     → 신청 학기(지난 학기면 제외) → 가장 최근 학기
//   관리자·담당 교수는 아래 '평가 기준'·'출결' 영역도 그림 (같은 학기 목록, 평가 기준 처음 학기: 이전 선택 → 신청 학기 → 게시물 학기,
//   출결 처음 학기: 이전 선택 → 평가 기준 학기). 출결 영역은 불러오기 실패를 영역 안에 보이고 던지지 않음
async function renderCoursePage() {
    const code = state.courseCode;
    const seq = renderSeq;
    const c = pageCourse();
    if (!c) { clearCoursePage(); showTab('courses'); return; }
    const manage = canManageCourse(c);
    $('post-add').hidden = !manage;
    $('assess-section').hidden = !manage;
    $('attend-section').hidden = !manage;
    const staff = state.me.role !== 'student';
    const [posts, staffInfo] = await Promise.all([
        api('GET', postsUrl(code)),
        staff ? Promise.all([
            api('GET', '/api/enrollments?courseCode=' + encodeURIComponent(code)),
            api('GET', '/api/enrollment/settings'),
        ]) : null,
    ]);
    // 기다리는 사이 다른 과목·탭으로 옮겼으면 그리지 않음 (나중에 끝난 요청이 화면을 덮지 않게)
    if (seq !== renderSeq || currentTab() !== 'course' || state.courseCode !== code) return;

    let sems, fallback, open = '';
    if (staff) {
        const [list, settings] = staffInfo;
        open = settings.semester;
        sems = visibleSemesters([...posts.map((p) => p.semester), ...list.map((e) => e.semester), open]);
        if (isLockedSemester(open)) open = '';
        fallback = open || sems[0] || '';
    } else {
        sems = visibleSemesters(state.myEnrollments.filter((e) => e.courseCode === code).map((e) => e.semester));
        fallback = sems[0] || '';
    }
    // 수강과목 카드에 보인 학기로 시작. 학생은 본인 수강신청 중 현재 학기 이후 가장 최근 학기,
    // 교수·관리자는 이 과목 수강신청이 있는 학기 중 볼 수 있는 가장 최근 학기 (staffSemesterOf, 방금 받은 수강신청 목록으로 계산)
    const mine = staff ? staffSemesterOf(staffInfo[0].map((e) => e.semester)) : mySemesterOf(code);
    const firstSem = sems.includes(mine) ? mine : fallback;
    if (!sems.includes(state.courseSem)) state.courseSem = firstSem;
    fillSelect($('course-sem'), sems.map((s) => ({ value: s, label: s })), '', state.courseSem);

    state.coursePosts = posts;
    drawPosts();

    if (!manage) { state.assess = null; state.attend = null; return; }
    if (state.assessSem && !sems.includes(state.assessSem) && !isLockedSemester(state.assessSem)) {
        sems.push(state.assessSem);
        sems.sort().reverse();
    }
    if (!sems.includes(state.assessSem)) state.assessSem = open || state.courseSem;
    fillSelect($('assess-sem'), sems.map((s) => ({ value: s, label: s })), '', state.assessSem);
    if (!sems.includes(state.attendSem)) state.attendSem = state.assessSem;
    fillSelect($('attend-sem'), sems.map((s) => ({ value: s, label: s })), '', state.attendSem);
    await Promise.all([renderAssess(), renderAttend()]);
}

// 과목 카드 + 분류 칩 + 주차별 게시물 목록 그리기 (선택 학기의 게시물만, 서버를 다시 부르지 않음)
function drawPosts() {
    const c = pageCourse();
    if (!c) return;
    const sem = state.courseSem ?? '';
    $('course-hero').innerHTML = `
        <div class="course-hero-info">
            <div class="course-hero-sem">${sem ? esc(sem) + '학기' : '학기 없음'}</div>
            <div class="course-hero-name">${esc(c.name)}</div>
            <div class="course-line">${esc(c.schedule) || '-'} · ${esc(c.code)} · ${esc(c.professor) || '-'} · ${esc(c.credit)}학점</div>
        </div>
        <button type="button" data-act="info">과목 정보</button>`;

    const inSem = state.coursePosts.filter((p) => p.semester === sem);
    const chips = [{ value: 'all', label: '전체' }, ...POST_CATEGORIES];
    $('course-filter').innerHTML = chips.map((k) => {
        const n = k.value === 'all' ? inSem.length : inSem.filter((p) => p.category === k.value).length;
        return `<button type="button" class="chip${state.postFilter === k.value ? ' active' : ''}" data-cat="${k.value}">${k.label} ${n}</button>`;
    }).join('');

    // 주차 → 분류(POST_CATEGORIES 순서) → id 순
    const order = (v) => POST_CATEGORIES.findIndex((k) => k.value === v);
    const shown = inSem.filter((p) => state.postFilter === 'all' || p.category === state.postFilter)
        .sort((a, b) => a.week - b.week || order(a.category) - order(b.category) || a.id - b.id);
    const box = $('course-posts');
    if (!shown.length) { box.innerHTML = '<p class="empty">이 학기에 등록된 게시물이 없습니다</p>'; return; }
    const editable = canManageCourse(c);
    const weeks = [...new Set(shown.map((p) => p.week))];
    box.innerHTML = weeks.map((w) => `
        <h3 class="week-head">${esc(w)}주</h3>
        <ul class="post-list">${shown.filter((p) => p.week === w).map((p) => postItem(p, editable)).join('')}</ul>`
    ).join('');
}

// 게시물 한 줄: 분류 아이콘 · 제목(누르면 본문 펼침) · 분류/날짜/만료/링크 · 수정·삭제 버튼
function postItem(p, editable) {
    const k = postCategory(p.category);
    const meta = [esc(k.label)];
    if (p.dueDate) {
        meta.push(`${k.dateLabel} ${esc(p.dueDate)}${isExpired(p.dueDate) ? ' <span class="tag expired">만료됨</span>' : ''}`);
    }
    // 서버가 http/https 링크만 받지만, 다른 주소(javascript: 등)는 화면에서도 링크로 만들지 않음
    if (/^https?:\/\//.test(p.link ?? '')) {
        meta.push(`<a href="${esc(p.link)}" target="_blank" rel="noopener noreferrer">링크 열기</a>`);
    }
    return `<li class="post" data-id="${esc(p.id)}">
        <span class="post-icon cat-${esc(k.value)}">${esc(k.icon)}</span>
        <div class="post-main">
            <div class="post-title">${esc(p.title)}</div>
            <div class="post-meta">${meta.join(' · ')}</div>
            ${p.body ? `<div class="post-body" hidden>${esc(p.body)}</div>` : ''}
        </div>
        ${editable ? `<div class="post-actions">
            <button type="button" class="link" data-act="edit">수정</button>
            <button type="button" class="link danger" data-act="del">삭제</button>
        </div>` : ''}
    </li>`;
}

// 게시물 추가/수정 폼 항목과 API 본문 (날짜·링크는 앞뒤 공백을 지움, 빈칸이면 "")
const postFields = () => [
    { name: 'category', label: '분류', options: POST_CATEGORIES.map((k) => ({ value: k.value, label: k.label })) },
    { name: 'semester', label: '학기', placeholder: '예: 2026-2' },
    { name: 'week', label: '주차 (1~16)', type: 'number' },
    { name: 'title', label: '제목' },
    { name: 'body', label: '내용', type: 'textarea', optional: true },
    { name: 'dueDate', label: '날짜 (과제 마감·시험일, 선택)', optional: true, placeholder: '예: 2026-10-20 13:00' },
    { name: 'link', label: '링크 (선택)', optional: true, placeholder: 'https://...' },
];
const postBody = (d) => ({
    semester: d.semester.trim(), category: d.category, week: Number(d.week), title: d.title,
    body: d.body ?? '', link: d.link.trim(), dueDate: d.dueDate.trim(),
});

// 게시물 추가(p 없음) / 수정(p = 기존 게시물) 폼. 저장하면 그 게시물의 학기를 보여 줌
function openPostForm(p) {
    const code = state.courseCode;
    openForm({
        title: p ? '게시물 수정' : '게시물 추가',
        fields: postFields(),
        values: p ?? { category: 'material', semester: state.courseSem ?? '', week: 1 },
        onSubmit: async (d) => {
            const body = postBody(d);
            if (p) {
                await api('PUT', postsUrl(code) + '/' + encodeURIComponent(p.id), body);
                toast('수정했습니다');
            } else {
                await api('POST', postsUrl(code), body);
                toast('게시물을 추가했습니다');
            }
            state.courseSem = body.semester;
            await refreshAll();
        },
    });
}

$('post-add').onclick = () => openPostForm(null);
// 학기를 바꾸면 받아 둔 게시물로 다시 그림
$('course-sem').onchange = () => { state.courseSem = $('course-sem').value; drawPosts(); };
// 분류 칩: 고른 분류만 보임
$('course-filter').onclick = (e) => {
    const b = e.target.closest('[data-cat]');
    if (!b) return;
    state.postFilter = b.dataset.cat;
    drawPosts();
};
// 과목 정보: 통계를 받아 기존 과목 상세 창을 엶
$('course-hero').onclick = async (e) => {
    const c = pageCourse();
    if (!c || !e.target.closest('[data-act=info]')) return;
    try {
        const st = await api('GET', '/api/stats/course/' + encodeURIComponent(c.code));
        showCourseDetail(c, st, await courseLatest(c.code, st));
    } catch (err) { toast(err.message, true); }
};
// 게시물 목록: 제목을 누르면 본문을 펼치고/접음, 수정·삭제 버튼
$('course-posts').onclick = (e) => {
    const li = e.target.closest('.post');
    const p = li && state.coursePosts.find((x) => String(x.id) === li.dataset.id);
    if (!p) return;
    if (e.target.closest('.post-title')) {
        const body = li.querySelector('.post-body');
        if (body) body.hidden = !body.hidden;
        return;
    }
    const act = e.target.closest('[data-act]')?.dataset.act;
    if (act === 'edit') openPostForm(p);
    if (act === 'del') {
        confirmDelete(`"${p.title}" 게시물을 삭제할까요?`,
            () => api('DELETE', postsUrl(state.courseCode) + '/' + encodeURIComponent(p.id)));
    }
};

// ───────── 평가 기준 (과목 페이지, 관리자·담당 교수) ─────────

// 등급 산출 방식 선택 항목 (서버 kGradingAbsolute·kGradingRelative)
const GRADING_MODES = [
    { value: 'absolute', label: '절대평가 (점수 기준)' },
    { value: 'relative', label: `상대평가 (${RELATIVE_RULES})` },
];
// 평가 기준 폼 검사 (서버 PUT /api/assessments 와 같은 규칙·문구)
//   분류별 비율: 0~100 정수 (빈칸도 오류), 방식: absolute·relative, 그다음 합계 100
const SCHEME_RULES = '평가 기준이 올바르지 않습니다 (분류별 비율 0~100 정수, 합계 100, 방식 absolute·relative)';
const validateScheme = (d) => {
    const weights = ASSESS_CATEGORIES.map((c) => String(d[c] ?? '').trim());
    if (weights.some((w) => !/^\d+$/.test(w) || Number(w) > 100)) return SCHEME_RULES;
    if (!GRADING_MODES.some((m) => m.value === d.mode)) return SCHEME_RULES;
    const total = weights.reduce((t, w) => t + Number(w), 0);
    return total === 100 ? '' : `비율 합계가 100이어야 합니다 (지금 ${total}%)`;
};
// 항목 점수표 마지막 열: 서버가 계산한 최종 점수와 등급 (성적이 없으면 = 미완성 '-')
const finalCell = (g) => (g ? `<b>${esc(round2(g.score))}</b> ${letterBadge(g.letter)}` : '-');

// 상대평가 요약 줄: n = 이 과목·학기 성적 수, A0 이상·B0 이상 인원과 평균 평점은 서버가 준 등급으로 셈
//   상한은 서버 규칙처럼 내림 (⌊n×0.3⌋, ⌊n×0.7⌋), 성적이 없으면 평균 '-'
function relativeSummary(grades) {
    const n = grades.length;
    const count = (letters) => grades.filter((g) => letters.includes(g.letter)).length;
    const point = (l) => (Object.hasOwn(LETTER_POINT, l) ? LETTER_POINT[l] : 0);
    const avg = n ? grades.reduce((t, g) => t + point(g.letter), 0) / n : null;
    const cap = (r) => Math.floor(n * r + 1e-9);
    return `상대평가 · A0 이상 ${count(['A+', 'A0'])}명 / 상한 ${cap(0.3)} · B0 이상 ${count(['A+', 'A0', 'B+', 'B0'])}명 / 상한 ${cap(0.7)}` +
        ` · 평균 평점 ${fmt(avg)} / 3.50 <span class="hint">(최종 성적 ${n}명 기준, 성적이 더 생기면 등급이 다시 정해집니다)</span>`;
}

// '평가 기준' 영역 그리기 (고른 학기 state.assessSem)
//   평가 기준이 없으면 안내 문구만 (설정 버튼만, 삭제 버튼 숨김)
//   있으면 6분류 표(분류·비율, 0% 줄은 흐리게) + 비율 합계·등급 방식 배지,
//   수강생별 항목 점수 입력표 (행 = 수강생, 열 = 비율이 0보다 큰 분류, 마지막 = 최종), 상대평가면 요약 줄
//   '출석' 열은 늘 읽기 전용 (lockAttendColumn)
async function renderAssess() {
    const code = state.courseCode;
    const seq = renderSeq;
    const sem = state.assessSem ?? '';
    const box = $('assess-body');
    // 다 받기 전에는 설정·삭제 버튼과 점수 칸이 이전 학기를 가리키지 않게 비워 둠
    state.assess = null;
    $('assess-add').disabled = !sem;
    $('assess-del').hidden = true;
    if (!sem) {
        box.innerHTML = '<p class="hint">고를 학기가 없습니다 (수강신청 학기를 정하거나 이 과목에 수강신청이 있어야 합니다)</p>';
        return;
    }
    const q = assessQuery(code, sem);
    const [{ mode, items }, enrolls, scores, grades] = await Promise.all([
        api('GET', '/api/assessments?' + q),
        api('GET', '/api/enrollments?' + q),
        api('GET', '/api/assessment-scores?' + q),
        api('GET', '/api/grades?' + q),
    ]);
    // 기다리는 사이 과목·학기·탭이 바뀌었으면 그리지 않음
    if (seq !== renderSeq || currentTab() !== 'course' || state.courseCode !== code || state.assessSem !== sem) return;
    const cols = items.filter((a) => a.weight > 0);
    state.assess = { code, sem, mode, items, cols, finalSeq: 0 };
    if (!items.length) {
        box.innerHTML = '<p class="hint">평가 기준이 없습니다. 설정하면 이 과목·학기 성적은 항목 점수로 자동 계산됩니다</p>';
        return;
    }
    $('assess-del').hidden = false;

    const relative = mode === 'relative';
    const total = items.reduce((t, a) => t + a.weight, 0);
    const modeBadge = `<span class="tag mode">${relative ? '상대평가' : '절대평가 (점수 기준)'}</span>`;
    const schemeRows = items.map((a) => `
        <tr${a.weight > 0 ? '' : ' class="zero"'}><td><b>${esc(a.name)}</b></td><td class="num">${esc(a.weight)}</td></tr>`).join('');

    const scoreOf = new Map(scores.map((s) => [JSON.stringify([s.studentId, s.name]), s.score]));
    const gradeOf = new Map(grades.map((g) => [g.studentId, g]));
    const rows = [...enrolls].sort((a, b) => a.studentId.localeCompare(b.studentId)).map((e) => {
        const cells = cols.map((a, i) => {
            const v = esc(scoreOf.get(JSON.stringify([e.studentId, a.name])) ?? '');
            return `<td class="num"><input class="score-cell" type="number" step="any" min="0" max="100" data-i="${i}"
                value="${v}" data-prev="${v}" aria-label="${esc(studentName(e.studentId))} ${esc(a.name)}"></td>`;
        }).join('');
        return `
        <tr data-sid="${esc(e.studentId)}">
            <td>${esc(e.studentId)}</td>
            <td><b>${esc(studentName(e.studentId))}</b></td>
            ${cells}
            <td class="num final">${finalCell(gradeOf.get(e.studentId))}</td>
        </tr>`;
    }).join('');
    box.innerHTML = `
        <div class="table-wrap">
            <table class="scheme-table"><thead><tr><th>분류</th><th class="num">비율(%)</th></tr></thead>
            <tbody>${schemeRows}</tbody>
            <tfoot><tr><td colspan="2" class="weight-sum${total === 100 ? '' : ' warn'}">
                비율 합계 <b>${total} / 100</b> <span class="mode-label">등급 방식 ${modeBadge}</span></td></tr></tfoot></table>
        </div>
        <h4 class="assess-head">항목 점수 <span class="hint">칸을 고치면 바로 저장 · 빈칸으로 지우면 점수 삭제 · 출석은 출석부에서 자동 계산 · 비율이 0보다 큰 분류에 모두 점수가 있으면 최종 성적 자동 계산</span></h4>
        <div class="table-wrap">
            <table><thead><tr><th>학번</th><th>이름</th>
                ${cols.map((a) => `<th class="num">${esc(a.name)} <span class="hint">${esc(a.weight)}%</span></th>`).join('')}
                <th class="num">최종 (점수·등급)</th></tr></thead>
            <tbody>${rows || emptyRow(cols.length + 3, '이 학기 수강생이 없습니다')}</tbody></table>
        </div>
        ${relative ? `<p class="relative-line">${relativeSummary(grades)}</p>` : ''}`;
    lockAttendColumn();
}

// 평가 기준 설정 폼 (ctx = 지금 영역의 {code, sem, mode, items}). 6분류 비율 칸(지금 값, 없으면 0) + 등급 방식
//   저장은 PUT /api/assessments 한 번 (6분류 비율을 모두 보냄)
function openSchemeForm(ctx) {
    const weightOf = new Map(ctx.items.map((a) => [a.name, a.weight]));
    openForm({
        title: `평가 기준 설정 · ${courseName(ctx.code)} ${ctx.sem}`,
        fields: [
            ...ASSESS_CATEGORIES.map((c) => ({ name: c, label: `${c} 비율 (%, 0~100 정수)`, type: 'number', min: 0, max: 100, step: 1 })),
            { name: 'mode', label: '등급 방식', options: GRADING_MODES },
        ],
        values: { ...Object.fromEntries(ASSESS_CATEGORIES.map((c) => [c, weightOf.get(c) ?? 0])), mode: ctx.mode },
        validate: validateScheme,
        onSubmit: async (d) => {
            const weights = Object.fromEntries(ASSESS_CATEGORIES.map((c) => [c, Number(d[c])]));
            await api('PUT', '/api/assessments', { courseCode: ctx.code, semester: ctx.sem, mode: d.mode, weights });
            toast('평가 기준을 저장했습니다');
            await refreshAll();
        },
    });
}

// 이 과목·학기 성적을 다시 받아 점수표의 최종 열 전부와 상대평가 요약 줄을 다시 그림 (표 전체를 다시 그리지 않아 다음 칸 입력이 끊기지 않음)
//   상대평가면 한 학생 점수가 바뀌어도 다른 학생 등급이 바뀌므로 모든 행을 다시 그림
//   받은 성적 목록을 돌려줌. 연달아 부르면 마지막으로 보낸 요청의 응답만 그림 (ctx.finalSeq)
async function refreshFinals(ctx) {
    const seq = ++ctx.finalSeq;
    const grades = await api('GET', '/api/grades?' + assessQuery(ctx.code, ctx.sem));
    if (state.assess !== ctx || ctx.finalSeq !== seq) return grades;
    const gradeOf = new Map(grades.map((g) => [g.studentId, g]));
    $('assess-body').querySelectorAll('tr[data-sid]').forEach((r) => {
        r.querySelector('.final').innerHTML = finalCell(gradeOf.get(r.dataset.sid));
    });
    const line = $('assess-body').querySelector('.relative-line');
    if (line) line.innerHTML = relativeSummary(grades);
    return grades;
}

$('assess-add').onclick = () => { if (state.assess) openSchemeForm(state.assess); };
$('assess-del').onclick = () => {
    const ctx = state.assess;
    if (!ctx?.items.length) return;
    confirmDelete('평가 기준을 지우면 이 과목·학기의 항목 점수와 최종 성적이 모두 지워집니다',
        () => api('DELETE', '/api/assessments?' + assessQuery(ctx.code, ctx.sem)));
};
$('assess-sem').onchange = () => {
    state.assessSem = $('assess-sem').value;
    renderAssess().catch((e) => toast(e.message, true));
};
// 점수 칸을 바꾸면(Enter·칸 이동) 저장: 숫자면 PUT, 빈칸이면 DELETE. 실패하면 이전 값으로 되돌리고 토스트
$('assess-body').addEventListener('change', async (e) => {
    const input = e.target.closest('input.score-cell');
    const ctx = state.assess;
    if (!input || !ctx) return;
    const item = ctx.cols[Number(input.dataset.i)];
    const sid = input.closest('tr').dataset.sid;
    const prev = input.dataset.prev;
    const raw = input.value.trim();
    const key = { studentId: sid, courseCode: ctx.code, semester: ctx.sem, name: item.name };
    try {
        if (input.validity.badInput) throw new Error(SCORE_RULES);
        if (raw === '') {
            if (prev === '') return;
            await api('DELETE', '/api/assessment-scores?' + new URLSearchParams(key));
        } else {
            const score = Number(raw);
            if (!Number.isFinite(score) || score < 0 || score > 100) throw new Error(SCORE_RULES);
            if (prev !== '' && Number(prev) === score) { input.value = prev; return; }
            const saved = await api('PUT', '/api/assessment-scores', { ...key, score });
            input.value = saved.score;
        }
        input.dataset.prev = input.value;
    } catch (err) {
        input.value = prev;
        toast(err.message, true);
        return;
    }
    try {
        const g = (await refreshFinals(ctx)).find((x) => x.studentId === sid);
        toast(`${studentName(sid)} ${item.name} ${raw === '' ? '삭제' : '저장'} · ` + (g ? `최종 ${round2(g.score)}점 → ${g.letter}` : '최종 성적 미완성'));
    } catch (err) { toast(err.message, true); }
});

// ───────── 출결 (과목 페이지, 관리자·담당 교수) ─────────

// 출결 상태 (서버 Attendance 와 같은 값). 칸을 누를 때마다 없음('') → 출석 → 지각 → 결석 → 공결 → 없음 순으로 바뀜
const ATTEND_STATUSES = ['출석', '지각', '결석', '공결'];
const nextAttendStatus = (s) => ATTEND_STATUSES[ATTEND_STATUSES.indexOf(s) + 1] ?? '';
// 상태 → 칸 색 클래스 (style.css 의 button.att.*)
const ATTEND_CLASS = { 출석: 'present', 지각: 'late', 결석: 'absent', 공결: 'excused' };
// 수업 주차 수 (서버 출결 주차 1~16, 게시물 주차와 같음)
const ATTEND_WEEKS = 16;
// 출결 칸 하나를 구분하는 키 (학번, 주차)
const attendKey = (sid, week) => sid + '|' + week;

// 출결 기록·요약 {records, summaries} 받기 (query: 'courseCode=…&semester=…', 학생은 '' = 본인 기록 전체)
//   응답 모양이 다르면 오류로 던짐. 출석 점수는 서버 summaries 값만 쓰고 화면에서 다시 계산하지 않음
async function attendanceOf(query) {
    const data = await api('GET', '/api/attendance' + (query ? '?' + query : ''));
    if (!Array.isArray(data?.records) || !Array.isArray(data?.summaries)) throw new Error('출결 응답 형식이 올바르지 않습니다');
    return data;
}

// '출결' 영역 그리기 (고른 학기 state.attendSem): 행 = 그 과목·학기 수강생(출결 기록만 남은 학생도 포함), 열 = 1~16주,
//   행 끝 = 결석 환산 · 출석 점수 (+ 출석 미달 배지). 불러오기 실패는 영역 안에 한 줄 오류로 보이고 던지지 않음
//   다시 그리는 동안 이전 표는 그대로 두되 state.attend 가 null 이라 눌러도 저장하지 않음 (다른 과목·학기면 '불러오는 중')
async function renderAttend() {
    const code = state.courseCode;
    const seq = renderSeq;
    const sem = state.attendSem ?? '';
    const box = $('attend-body');
    const stale = () => seq !== renderSeq || currentTab() !== 'course' || state.courseCode !== code || state.attendSem !== sem;
    state.attend = null;
    if (!sem) {
        box.innerHTML = '<p class="hint">고를 학기가 없습니다 (수강신청 학기를 정하거나 이 과목에 수강신청이 있어야 합니다)</p>';
        return;
    }
    const key = JSON.stringify([code, sem]);
    if (box.dataset.key !== key || !box.querySelector('table')) box.innerHTML = '<p class="hint">출결을 불러오는 중…</p>';
    box.dataset.key = key;
    try {
        const q = assessQuery(code, sem);
        const [data, enrolls] = await Promise.all([attendanceOf(q), api('GET', '/api/enrollments?' + q)]);
        if (stale()) return;
        const ctx = {
            code, sem,
            records: new Map(data.records.map((r) => [attendKey(r.studentId, r.week), r.status])),
            summaries: new Map(data.summaries.map((s) => [s.studentId, s])),
            seq: new Map(),  // 학번 → 마지막으로 보낸 요약 다시 받기 번호 (늦게 온 이전 응답이 덮지 않게)
        };
        const ids = [...new Set([...enrolls.map((e) => e.studentId), ...data.records.map((r) => r.studentId)])].sort();
        const weeks = Array.from({ length: ATTEND_WEEKS }, (_, i) => i + 1);
        const rows = ids.map((sid) => `
            <tr data-sid="${esc(sid)}" data-name="${esc(studentName(sid))}">
                <td class="att-name"><b>${esc(studentName(sid))}</b> <span class="hint">${esc(sid)}</span></td>
                ${weeks.map((w) => `<td class="att-td"><button type="button" class="att" data-w="${w}"></button></td>`).join('')}
                <td class="num att-eq"></td>
                <td class="num att-score"></td>
            </tr>`).join('');
        box.innerHTML = `
            <div class="table-wrap">
                <table class="attend-table"><thead><tr><th class="att-name">이름 · 학번</th>
                    ${weeks.map((w) => `<th class="att-td">${w}</th>`).join('')}
                    <th class="num">결석 환산</th><th class="num">출석 점수</th></tr></thead>
                <tbody>${rows || emptyRow(ATTEND_WEEKS + 3, '이 학기 수강생이 없습니다')}</tbody></table>
            </div>`;
        box.querySelectorAll('tr[data-sid]').forEach((tr) => {
            tr.querySelectorAll('button.att').forEach((b) => paintAttendCell(b, ctx.records.get(attendKey(tr.dataset.sid, b.dataset.w)) ?? ''));
            paintAttendSummary(ctx, tr);
        });
        state.attend = ctx;
    } catch (err) {
        if (!stale()) box.innerHTML = `<p class="attend-error">출결을 불러오지 못했습니다: ${esc(err.message)}</p>`;
    }
}

// 출결 칸 하나 표시: 상태 첫 글자와 색, 마우스를 올리면 'N주 상태' (없음이면 빈 칸)
function paintAttendCell(btn, status) {
    const label = `${btn.dataset.w}주 ${status || '없음'}`;
    btn.className = 'att' + (Object.hasOwn(ATTEND_CLASS, status) ? ' ' + ATTEND_CLASS[status] : '');
    btn.textContent = String(status).slice(0, 1);
    btn.title = label;
    btn.setAttribute('aria-label', `${btn.closest('tr').dataset.name} ${label}`);
}

// 행 끝 값: 결석 환산 · 출석 점수 (소수 둘째 자리). 서버 요약이 없거나 score 가 null(기록 없음)이면 '-'
//   shortage 면 '출석 미달' 경고 배지 (학사경고와 같은 badge F 색)
function paintAttendSummary(ctx, tr) {
    const s = ctx.summaries.get(tr.dataset.sid);
    const has = typeof s?.score === 'number';
    tr.querySelector('.att-eq').textContent = has ? (s.absentEquivalent ?? '-') : '-';
    tr.querySelector('.att-score').innerHTML = fmt(has ? s.score : null) + (has && s.shortage ? ' <span class="badge F">출석 미달</span>' : '');
}

// 평가 기준 점수표의 '출석' 열은 늘 읽기 전용 (출석부에서 자동 계산, 서버도 직접 입력을 409 로 막음)
function lockAttendColumn() {
    const i = state.assess ? state.assess.cols.findIndex((x) => x.name === ATTEND_ITEM) : -1;
    if (i < 0) return;
    $('assess-body').querySelectorAll(`input.score-cell[data-i="${i}"]`).forEach((input) => {
        input.disabled = true;
        input.title = ATTEND_LOCK_MSG;
    });
}

// 칸을 누르면 다음 상태로 바꾸고 바로 저장 (상태는 PUT, 없음은 DELETE). 저장 중인 칸은 다시 눌러도 무시
//   실패하면 이전 상태로 되돌리고 오류 토스트, 성공하면 그 학생의 행 끝 값과 평가 기준 점수표를 다시 그림
async function toggleAttend(btn) {
    const ctx = state.attend;
    if (!ctx || btn.dataset.busy) return;
    const tr = btn.closest('tr');
    const sid = tr.dataset.sid;
    const week = Number(btn.dataset.w);
    const key = attendKey(sid, week);
    const prev = ctx.records.get(key) ?? '';
    const next = nextAttendStatus(prev);
    const show = (s) => {
        if (s) ctx.records.set(key, s); else ctx.records.delete(key);
        paintAttendCell(btn, s);
    };
    show(next);
    btn.dataset.busy = '1';
    const id = { studentId: sid, courseCode: ctx.code, semester: ctx.sem, week };
    let saved = true;
    try {
        if (next) await api('PUT', '/api/attendance', { ...id, status: next });
        else await api('DELETE', '/api/attendance?' + new URLSearchParams(id));
    } catch (err) {
        show(prev);
        toast(err.message, true);
        saved = false;
    }
    delete btn.dataset.busy;
    if (state.attend !== ctx) return;
    if (saved) await refreshAttendRow(ctx, tr, sid, `${week}주 ${next || '기록 삭제'}`);
}

// 저장한 학생의 행 끝 값을 서버 요약으로 다시 그리고, 같은 과목·학기 평가 기준 점수표의 그 학생 '출석' 칸·최종 열도 다시 그림
//   같은 학생을 연달아 바꾸면 마지막 저장 뒤에 보낸 요청의 응답만 그림 (seq)
async function refreshAttendRow(ctx, tr, sid, what) {
    const seq = (ctx.seq.get(sid) ?? 0) + 1;
    ctx.seq.set(sid, seq);
    const latest = () => state.attend === ctx && ctx.seq.get(sid) === seq;
    try {
        const data = await attendanceOf(assessQuery(ctx.code, ctx.sem));
        if (!latest()) return;
        const s = data.summaries.find((x) => x.studentId === sid);
        if (s) ctx.summaries.set(sid, s); else ctx.summaries.delete(sid);
        paintAttendSummary(ctx, tr);
        const g = await refreshAssessAttend(ctx, sid, latest);
        const final = g === null ? '' : g ? ` · 최종 ${round2(g.score)}점 → ${g.letter}` : ' · 최종 성적 미완성';
        toast(`${studentName(sid) || sid} ${what} · 출석 점수 ${fmt(typeof s?.score === 'number' ? s.score : null)}${final}`);
    } catch (err) {
        toast(`저장했지만 다시 불러오지 못했습니다: ${err.message}`, true);
    }
}

// 평가 기준 점수표가 같은 과목·학기이고 '출석' 열(비율이 0보다 큼)이 있으면, 그 학생의 '출석' 칸을 서버가 다시 계산한 점수로 바꾸고
//   최종 열은 refreshFinals 로 다시 그림. 그 학생 성적(없으면 undefined)을 돌려주고, 다시 그릴 표가 없으면 null
async function refreshAssessAttend(ctx, sid, latest) {
    const a = state.assess;
    const i = a && a.code === ctx.code && a.sem === ctx.sem ? a.cols.findIndex((x) => x.name === ATTEND_ITEM) : -1;
    if (i < 0) return null;
    const scores = await api('GET', '/api/assessment-scores?' + assessQuery(a.code, a.sem));
    if (state.assess !== a || !latest()) return null;
    const row = [...$('assess-body').querySelectorAll('tr[data-sid]')].find((r) => r.dataset.sid === sid);
    const input = row?.querySelector(`input.score-cell[data-i="${i}"]`);
    if (input) {
        const v = String(scores.find((x) => x.studentId === sid && x.name === ATTEND_ITEM)?.score ?? '');
        input.value = v;
        input.dataset.prev = v;
    }
    return (await refreshFinals(a)).find((x) => x.studentId === sid);
}

$('attend-sem').onchange = () => { state.attendSem = $('attend-sem').value; renderAttend(); };
$('attend-body').onclick = (e) => {
    const b = e.target.closest('button.att');
    if (b) toggleAttend(b);
};

// ───── 강의시간 ─────

// 강의시간 문자열 → [{day, start, end}] (day: '월'~'일' 한 글자, 교시 start~end).
// 서버 src/course.cpp 의 parseSlot·splitSlots 와 같은 규칙: 칸은 공백·쉼표 구분,
// 한 칸 = 요일 + (n) 또는 (n~m), 교시 1~30 (최대 두 자리), 잘못된 칸·빈 문자열은 무시
function parseSchedule(str) {
    const slots = [];
    for (const text of String(str ?? '').split(/[ ,]/)) {
        const m = /^([월화수목금토일])\((\d{1,2})(?:~(\d{1,2}))?\)$/.exec(text);
        if (!m) continue;
        const start = Number(m[2]);
        const end = m[3] === undefined ? start : Number(m[3]);
        if (start < 1 || end > 30 || start > end) continue;
        slots.push({ day: m[1], start, end });
    }
    return slots;
}

// 두 강의시간(문자열)의 겹침 → [{day, start, end}] (겹치는 요일·교시 구간, 없으면 빈 배열).
// 같은 요일에 교시 구간이 하나라도 겹치면 겹침 (서버 schedulesOverlap 와 같음). 빈 강의시간은 겹치지 않음
function scheduleConflict(a, b) {
    const slotsB = parseSchedule(b);
    const spans = [];
    for (const x of parseSchedule(a)) {
        for (const y of slotsB) {
            if (x.day !== y.day || x.start > y.end || y.start > x.end) continue;
            spans.push({ day: x.day, start: Math.max(x.start, y.start), end: Math.min(x.end, y.end) });
        }
    }
    return spans;
}

// 겹침 구간 표시 글자: 월(3~4교시) / 월(3교시)
const formatSpan = (s) => `${s.day}(${s.start === s.end ? s.start : `${s.start}~${s.end}`}교시)`;

// ───────── 수강신청 ─────────

// 수강신청 탭 그리기. 맨 위에 신청 학기(또는 기간 아님)를 보이고 역할별 화면으로 나눔
//   학생: 자기 수강신청 (신청 학기에만 신청·취소)
//   관리자: 신청 학기 설정 + 고른 학생의 수강신청 (모든 학기에서 신청·취소)
//   교수: 담당 과목의 학기별 수강생 조회만
async function renderEnroll() {
    const seq = renderSeq;
    const role = state.me.role;
    const admin = role === 'admin';
    const professor = role === 'professor';

    $('enroll-admin').hidden = !admin;
    $('enroll-student-wrap').hidden = !admin;
    $('enroll-course-wrap').hidden = !professor;
    $('enroll-sem-input').hidden = !admin;
    $('enroll-sem-select').hidden = admin;

    if (professor) {
        const pick = $('enroll-course');
        fillSelect(pick, state.courses.filter(teaches).map((c) => ({
            value: c.code, label: `${c.name} (${c.code})`,
        })), '', pick.value);
        const code = pick.value;
        const [settings, list] = await Promise.all([
            api('GET', '/api/enrollment/settings'),
            code ? api('GET', '/api/enrollments?courseCode=' + encodeURIComponent(code)) : Promise.resolve([]),
        ]);
        if (seq !== renderSeq) return;
        const open = settings.semester ?? '';
        const period = $('enroll-period');
        period.textContent = open ? `수강신청 학기: ${open}` : '수강신청 기간이 아닙니다';
        period.classList.toggle('closed', !open);
        return renderEnrollProfessor(open, list);
    }

    let sid = state.me.studentId;
    if (admin) {
        const pick = $('enroll-student');
        fillSelect(pick, studentOptions(), '', pick.value);
        if (!pick.value) {
            const { semester: open = '' } = await api('GET', '/api/enrollment/settings');
            if (seq !== renderSeq) return;
            const period = $('enroll-period');
            period.textContent = open ? `수강신청 학기: ${open}` : '수강신청 기간이 아닙니다';
            period.classList.toggle('closed', !open);
            $('enroll-set-sem').value = open;
            state.enrollCtx = null;
            $('enroll-content').innerHTML = '<p class="hint">등록된 학생이 없습니다</p>';
            return;
        }
        sid = pick.value;
    }

    const [settings, mine] = await Promise.all([
        api('GET', '/api/enrollment/settings'),
        api('GET', '/api/enrollments?studentId=' + encodeURIComponent(sid)),
    ]);
    if (seq !== renderSeq) return;
    const open = settings.semester ?? '';
    const period = $('enroll-period');
    period.textContent = open ? `수강신청 학기: ${open}` : '수강신청 기간이 아닙니다';
    period.classList.toggle('closed', !open);
    if (admin) $('enroll-set-sem').value = open;
    return renderEnrollStudent(open, admin, mine, settings.latestGradeSemester ?? '');
}

// 학생(또는 관리자가 고른 학생)의 "개설 과목" + "신청 내역" 두 목록
async function renderEnrollStudent(open, admin, mine, latestGradeSemester) {
    const seq = renderSeq;
    const content = $('enroll-content');
    const sid = admin ? $('enroll-student').value : state.me.studentId;
    if (!sid) { state.enrollCtx = null; content.innerHTML = '<p class="hint">등록된 학생이 없습니다</p>'; return; }

    // 관리자는 신청 학기가 비어 있으면 전체 성적 중 가장 최근 학기를 기본값으로 사용
    // (기존 semesterList() 요청을 설정 API 응답에 포함해 요청 한 건을 줄임)
    let sem;
    if (admin) {
        const input = $('enroll-sem-input');
        if (!input.value.trim()) input.value = open || latestGradeSemester || '';
        sem = input.value.trim();
    } else {
        const sel = $('enroll-sem-select');
        const sems = visibleSemesters([open, ...mine.map((e) => e.semester)]);
        fillSelect(sel, sems.map((s) => ({ value: s, label: s })), '', sel.value || open);
        sem = sel.value;
    }
    if (!SEMESTER_RE.test(sem)) {
        state.enrollCtx = null;
        content.innerHTML = `<p class="hint">${admin ? '학기를 입력하세요 (예: 2026-2, 1·2 정규, 3 여름, 4 겨울)' : '표시할 학기가 없습니다'}</p>`;
        return;
    }
    state.enrollCtx = { studentId: sid, semester: sem };

    const counts = await api('GET', '/api/enrollments/counts?semester=' + encodeURIComponent(sem));
    if (seq !== renderSeq) return;
    const countOf = Object.fromEntries(counts.map((x) => [x.courseCode, x.count]));
    const inSem = mine.filter((e) => e.semester === sem).sort((a, b) => a.courseCode.localeCompare(b.courseCode));
    const applied = new Set(inSem.map((e) => e.courseCode));
    // 학생은 신청 학기에만 신청·취소 (그 학기가 지난 학기면 불가), 관리자는 모든 학기
    const locked = isLockedSemester(sem);
    const editable = admin || (open !== '' && sem === open && !locked);

    const course = (code) => state.courses.find((c) => c.code === code);
    // 선택 학기에 이미 신청한 과목들의 강의시간 (겹침 검사용). 서버 schedulesOverlap 가 최종 방어선
    const appliedCourses = inSem.map((e) => course(e.courseCode)).filter(Boolean);
    const applyCell = (c) => {
        const btn = (off) => `<button class="link" data-act="apply" data-code="${esc(c.code)}" ${off ? 'disabled' : ''}>신청</button>`;
        if (applied.has(c.code) || !editable) return btn(true);
        const hits = appliedCourses.map((o) => ({ o, spans: scheduleConflict(c.schedule, o.schedule) })).filter((h) => h.spans.length);
        if (!hits.length) return btn(false);
        const label = hits[0].o.name + (hits.length > 1 ? ` 외 ${hits.length - 1}` : '');
        const tip = hits.map((h) => `${h.o.name}: ${h.spans.map(formatSpan).join(', ')} 겹침`).join('\n');
        return `${btn(true)} <span class="badge overlap" title="${esc(tip)}">${esc(label)} 과목과 겹침</span>`;
    };

    const courseRows = state.courses.length ? state.courses.map((c) => `
        <tr>
            <td><b>${esc(c.name)}</b> <span class="hint">${esc(c.code)}</span></td>
            <td>${esc(c.professor) || '-'}</td>
            <td>${esc(c.schedule) || '-'}</td>
            <td class="num">${esc(c.credit)}</td>
            <td class="num">신청 ${esc(countOf[c.code] ?? 0)} / ${capacityText(c)}</td>
            <td class="actions">${applyCell(c)}</td>
        </tr>`).join('') : emptyRow(6, '등록된 과목이 없습니다');

    const totalCredits = inSem.reduce((t, e) => t + (Number(course(e.courseCode)?.credit) || 0), 0);
    const mineRows = inSem.length ? inSem.map((e) => {
        const c = course(e.courseCode);
        return `
        <tr>
            <td><b>${esc(c?.name ?? '')}</b> <span class="hint">${esc(e.courseCode)}</span></td>
            <td>${esc(c?.schedule) || '-'}</td>
            <td class="num">${esc(c?.credit ?? 0)}</td>
            <td>${e.hasGrade ? '성적 있음' : '-'}</td>
            <td class="actions"><button class="link danger" data-act="cancel" data-code="${esc(e.courseCode)}"
                ${e.hasGrade || !editable ? 'disabled' : ''}>취소</button></td>
        </tr>`;
    }).join('') : emptyRow(5, '이 학기에 신청한 과목이 없습니다');

    const who = admin ? `${esc(studentName(sid))} (${esc(sid)}) 신청 내역` : '내 신청 내역';
    content.innerHTML = `
        ${editable ? '' : `<p class="hint">${locked ? esc(pastSemesterMessage()) : open ? `신청·취소는 수강신청 학기(${esc(open)})에만 할 수 있습니다` : '지금은 수강신청 기간이 아닙니다'}</p>`}
        <h3 class="sub-title">개설 과목 · ${esc(sem)}</h3>
        <div class="table-wrap">
            <table><thead><tr><th>과목</th><th>교수</th><th>강의시간</th><th class="num">학점</th><th class="num">신청 / 정원</th><th></th></tr></thead>
            <tbody>${courseRows}</tbody></table>
        </div>
        <h3 class="sub-title">${who}</h3>
        <div class="table-wrap">
            <table><thead><tr><th>과목</th><th>강의시간</th><th class="num">학점</th><th>성적</th><th></th></tr></thead>
            <tbody>${mineRows}</tbody>
            <tfoot><tr><td colspan="2">학점 합계</td><td class="num"><b>${totalCredits}</b></td><td colspan="2"></td></tr></tfoot></table>
        </div>`;
}

// 교수: 담당 과목 + 학기를 골라 수강생 표 (학번·이름·학과·성적 유무). 신청·취소 버튼 없음
async function renderEnrollProfessor(open, list) {
    const seq = renderSeq;
    const content = $('enroll-content');
    state.enrollCtx = null;
    const pick = $('enroll-course');
    if (!pick.value) { content.innerHTML = '<p class="hint">담당 과목이 없습니다</p>'; return; }
    const code = pick.value;
    if (seq !== renderSeq) return;

    // 학기: 신청 학기·현재 학기 + 이 과목 수강신청에 나온 학기 (최신순, 지난 학기는 뺌)
    // 과목을 바꿨을 때 처음 보이는 학기: 신청 학기에 수강생이 있으면 그 학기, 없으면 (지난 학기 제외) 수강생이 있는 가장 최근 학기
    const sel = $('enroll-sem-select');
    const listSems = sortSemesters(list.map((e) => e.semester)).filter((s) => !isLockedSemester(s));
    let keep = sel.dataset.course === code ? sel.value : '';
    if (!keep) keep = open && listSems.includes(open) ? open : (listSems[0] || open || state.currentSem);
    fillSelect(sel, visibleSemesters([open, ...listSems]).map((s) => ({ value: s, label: s })), '', keep);
    sel.dataset.course = code;
    const sem = sel.value;

    const rows = list.filter((e) => e.semester === sem).sort((a, b) => a.studentId.localeCompare(b.studentId));
    const major = (id) => state.students.find((s) => s.id === id)?.major ?? '';
    content.innerHTML = `
        <h3 class="sub-title">${esc(courseName(code))} · ${sem ? esc(sem) : '-'} 수강생 ${rows.length}명</h3>
        <div class="table-wrap">
            <table><thead><tr><th>학번</th><th>이름</th><th>학과</th><th>성적</th></tr></thead>
            <tbody>${rows.length ? rows.map((e) => `
                <tr><td>${esc(e.studentId)}</td><td><b>${esc(studentName(e.studentId))}</b></td>
                <td>${esc(major(e.studentId))}</td><td>${e.hasGrade ? '있음' : '없음'}</td></tr>`).join('')
                : emptyRow(4, '수강생이 없습니다')}</tbody></table>
        </div>`;
}

// 선택을 바꾸면 수강신청 화면을 다시 그림
['enroll-student', 'enroll-course', 'enroll-sem-select', 'enroll-sem-input'].forEach((id) =>
    ($(id).onchange = () => renderEnroll().catch((e) => toast(e.message, true))));

// 신청·취소 버튼 (state.enrollCtx 의 학생·학기 기준)
$('enroll-content').onclick = async (e) => {
    const b = e.target.closest('button[data-act]');
    if (!b || !state.enrollCtx) return;
    const { studentId, semester } = state.enrollCtx;
    const courseCode = b.dataset.code;
    try {
        if (b.dataset.act === 'apply') {
            await api('POST', '/api/enrollments', { studentId, courseCode, semester });
            toast('신청했습니다');
        } else if (b.dataset.act === 'cancel') {
            await api('DELETE', '/api/enrollments?studentId=' + encodeURIComponent(studentId) +
                '&courseCode=' + encodeURIComponent(courseCode) + '&semester=' + encodeURIComponent(semester));
            toast('취소했습니다');
        } else return;
        await refreshAll();
    } catch (err) { toast(err.message, true); }
};

// 관리자: 신청 학기 저장 / 신청 마감 (빈 학기로 저장)
async function saveEnrollSemester(semester, msg) {
    try {
        await api('PUT', '/api/enrollment/settings', { semester });
        toast(msg);
        await refreshAll();
    } catch (err) { toast(err.message, true); }
}
$('enroll-set-save').onclick = () => {
    const sem = $('enroll-set-sem').value.trim();
    saveEnrollSemester(sem, sem ? `신청 학기를 ${sem} 로 정했습니다` : '수강신청을 마감했습니다');
};
$('enroll-set-close').onclick = () => saveEnrollSemester('', '수강신청을 마감했습니다');

// ───────── 학생 ─────────

// 학생 등록/수정 폼 항목. 수정할 때는 학번을 못 바꿈 (서버가 URL 의 학번으로 찾음)
const studentFields = (editing) => [
    { name: 'id', label: '학번', readonly: editing, placeholder: '예: 24101001' },
    { name: 'name', label: '이름' },
    { name: 'major', label: '학과' },
    { name: 'year', label: '학년', type: 'number' },
];

// 학생 표 그리기 (관리자). 검색어가 있으면 서버 검색 API 결과를, 없으면 전체 목록을 보여줌
// 전체 학생 GPA 일괄 조회와 학사경고 목록으로 GPA 열·경고 횟수를 채움
// 성적이 없는 학생은 서버가 gpa null 을 줌 → '-' (fmt)
async function renderStudents() {
    const seq = renderSeq;
    const q = $('student-search').value.trim();
    let list = state.students;
    if (q) {
        try { list = await api('GET', '/api/students/search?q=' + encodeURIComponent(q)); }
        catch (e) { toast(e.message, true); }
    }
    if (seq !== renderSeq) return;
    const body = $('student-body');
    if (!list.length) { body.innerHTML = emptyRow(7, q ? '검색 결과가 없습니다' : '등록된 학생이 없습니다'); return; }

    const [gpaRows, warnings] = await Promise.all([
        api('GET', '/api/stats/gpa/batch').catch(() => []),
        api('GET', '/api/stats/warnings').catch(() => []),
    ]);
    if (seq !== renderSeq) return;
    const gpaById = Object.fromEntries(gpaRows.map((row) => [row.studentId, gpaOf(row)]));
    // 학사경고 사유 약칭 (서버 reason 문자열 기준, 모르는 사유는 원문 그대로)
    const reasonShort = { '수강신청 미이행': '미신청', '평점평균 1.75 미만': '평점' };
    const warnList = {};
    warnings.forEach((w) => (warnList[w.studentId] ??= []).push(w));
    const warnBadge = (id) => {
        const ws = warnList[id];
        if (!ws) return '-';
        const kinds = [...new Set(ws.map((w) => reasonShort[w.reason] ?? w.reason))];
        const tip = ws.map((w) => `${w.semester} ${w.reason}${typeof w.gpa === 'number' ? ` (GPA ${w.gpa.toFixed(2)})` : ''}`).join('\n');
        return `<span class="badge F" title="${esc(tip).replace(/\n/g, '&#10;')}">${ws.length}회 · ${esc(kinds.join('·'))}</span>`;
    };
    body.innerHTML = list.map((s, i) => `
        <tr>
            <td>${esc(s.id)}</td>
            <td><b>${esc(s.name)}</b></td>
            <td>${esc(s.major)}</td>
            <td>${esc(s.year)}학년</td>
            <td class="num">${fmt(gpaById[s.id])}</td>
            <td>${warnBadge(s.id)}</td>
            <td class="actions">
                <button class="link" data-act="report" data-id="${esc(s.id)}">성적표</button>
                <button class="link" data-act="edit" data-id="${esc(s.id)}">수정</button>
                <button class="link danger" data-act="del" data-id="${esc(s.id)}">삭제</button>
            </td>
        </tr>`).join('');
}

// 검색칸에 입력할 때마다 표를 다시 그림
$('student-search').addEventListener('input', () => renderStudents().catch((e) => toast(e.message, true)));
$('student-add').onclick = () => openForm({
    title: '학생 등록',
    fields: studentFields(false),
    onSubmit: async (d) => {
        await api('POST', '/api/students', { ...d, year: Number(d.year) });
        toast('학생을 등록했습니다');
        await refreshAll();
    },
});
// 표 안 버튼(성적표/수정/삭제) 클릭을 한 곳에서 처리. data-act 로 어떤 버튼인지 구분
$('student-body').onclick = (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    const s = state.students.find((x) => x.id === b.dataset.id);
    if (!s) return;
    if (b.dataset.act === 'report') showStudentReport(s).catch((err) => toast(err.message, true));
    if (b.dataset.act === 'edit') openForm({
        title: '학생 수정',
        fields: studentFields(true),
        values: s,
        onSubmit: async (d) => {
            await api('PUT', '/api/students/' + encodeURIComponent(s.id), { ...d, year: Number(d.year) });
            toast('수정했습니다');
            await refreshAll();
        },
    });
    if (b.dataset.act === 'del') confirmDelete(
        `${s.name}(${s.id}) 학생을 삭제할까요?\n이 학생의 성적·수강신청·계정도 함께 삭제됩니다.`,
        () => api('DELETE', '/api/students/' + encodeURIComponent(s.id)));
};

// 학사경고 기준 평점 (GPA 그래프의 점선)
const WARNING_GPA = 1.75;
// 학기별 GPA 꺾은선 그래프 (인라인 SVG, 색은 style.css 의 .gpa-chart 규칙 = 기존 CSS 변수)
//   points: [{semester, gpa}] 학기 오름차순. x = 학기, y = 0~4.5, 점 위에 값(소수 둘째), 학사경고 1.75 점선
//   학기가 1개 이하면 빈 문자열 (그래프 숨김)
function gpaChartSvg(points) {
    if (points.length < 2) return '';
    // pad: 첫·마지막 점을 y축 글자와 오른쪽 끝에서 띄움. top: 위 여백에 범례를 두어 4.5 점의 값 글자와 겹치지 않게
    const W = 480, H = 230, left = 32, right = 12, top = 42, bottom = 28, pad = 30;
    const x = (i) => left + pad + (i * (W - left - right - 2 * pad)) / (points.length - 1);
    const y = (g) => top + (1 - Math.min(Math.max(g, 0), 4.5) / 4.5) * (H - top - bottom);
    const grid = [0, 1, 2, 3, 4, 4.5].map((g) => `
        <line class="grid" x1="${left}" x2="${W - right}" y1="${y(g)}" y2="${y(g)}"/>
        <text class="axis" x="${left - 8}" y="${y(g) + 4}" text-anchor="end">${g}</text>`).join('');
    // 기준선 + 오른쪽 위 범례 (점 값 글자와 겹치지 않게 그래프 위 여백에 둠)
    const limit = `
        <line class="limit" x1="${left}" x2="${W - right}" y1="${y(WARNING_GPA)}" y2="${y(WARNING_GPA)}"/>
        <line class="limit" x1="${W - right - 118}" x2="${W - right - 98}" y1="10" y2="10"/>
        <text class="limit-label" x="${W - right}" y="14" text-anchor="end">학사경고 기준 ${WARNING_GPA}</text>`;
    const line = `<polyline class="line" points="${points.map((p, i) => `${x(i)},${y(p.gpa)}`).join(' ')}"/>`;
    const dots = points.map((p, i) => `
        <circle class="dot${p.gpa < WARNING_GPA ? ' warn' : ''}" cx="${x(i)}" cy="${y(p.gpa)}" r="4"/>
        <text class="value" x="${x(i)}" y="${y(p.gpa) - 9}" text-anchor="middle">${fmt(p.gpa)}</text>
        <text class="axis" x="${x(i)}" y="${H - 8}" text-anchor="middle">${esc(p.semester)}</text>`).join('');
    return `
        <h3 class="sub-title">학기별 GPA 변화</h3>
        <svg class="gpa-chart" viewBox="0 0 ${W} ${H}" role="img" aria-label="학기별 GPA 변화 그래프">${grid}${limit}${line}${dots}</svg>`;
}

// 학생 한 명의 성적표 HTML: 요약(학번·전체 누적 GPA·전체 등록 학점·수강 건수) + 학기별 평점 표 + GPA 그래프
//   + 학기별 성적 목록 + 학사경고 + CSV 내려받기
// 성적표 창(관리자)과 내 성적 페이지(학생)가 같이 씀. s = {id, name}
// 평점·학점은 서버 /api/stats/summary/:id (gpaSummary) 값을 그대로 씀
//   summaryReq: 이미 보낸 summary 요청 (내 성적 화면이 같은 값을 시뮬레이션에도 써서 한 번만 받으려고 넘김, 없으면 여기서 받음)
//   학기별 과목 성적은 학기마다 접었다 펼치는 <details class="fold"> (기본 접힘, summary = 학기 · 학기 GPA · 등록 학점)
//   (재수강으로 지워진 성적·없는 과목은 서버가 이미 뺌, 계산 학점이 0 이면 gpa null = 등록된 성적 없음)
async function studentReportHtml(s, summaryReq = api('GET', '/api/stats/summary/' + encodeURIComponent(s.id))) {
    const id = encodeURIComponent(s.id);
    const [grades, summary, warnings] = await Promise.all([
        api('GET', '/api/grades?studentId=' + id),
        summaryReq,
        api('GET', '/api/stats/warnings/' + id),
    ]);
    grades.sort((a, b) => b.semester.localeCompare(a.semester) || a.courseCode.localeCompare(b.courseCode));
    const creditOf = (code) => Number(state.courses.find((c) => c.code === code)?.credit) || 0;
    const hasGpa = typeof summary.gpa === 'number';
    // 학기 → {gpa, credits}. 계산 학점이 0 인 학기(지워진 성적만 있음 등)는 평점 '-'
    const semInfo = Object.fromEntries(summary.semesters.map((x) => [x.semester, x]));
    const semGpa = (x) => (x && x.credits > 0 ? fmt(x.gpa) : '-');
        // 전공 평점·학점: 전공 학점이 0(또는 서버가 필드를 안 줌)이면 '-'
    const majorGpaText = (x) => (x && x.majorCredits > 0 ? fmt(x.majorGpa) : '-');
    const majorCreditText = (x) => (x && x.majorCredits > 0 ? esc(x.majorCredits) : '-');

    const summaryHtml = hasGpa ? `
        <h3 class="sub-title">학기별 평점</h3>
        <div class="table-wrap">
                        <table><thead><tr><th>학기</th><th class="num">평점평균</th><th class="num">등록 학점</th>
                <th class="num">전공 평점</th><th class="num">전공 학점</th></tr></thead>
            <tbody>${summary.semesters.map((x) => `
                <tr><td>${esc(x.semester)}</td><td class="num">${semGpa(x)}</td><td class="num">${esc(x.credits)}</td>
                <td class="num">${majorGpaText(x)}</td><td class="num">${majorCreditText(x)}</td></tr>`).join('')}
            </tbody>
            <tfoot><tr><td>전체 누적</td><td class="num"><b>${fmt(summary.gpa)}</b></td><td class="num"><b>${esc(summary.credits)}</b></td>
                <td class="num"><b>${majorGpaText(summary)}</b></td><td class="num"><b>${majorCreditText(summary)}</b></td></tr></tfoot></table>
        </div>
        ${gpaChartSvg(summary.semesters.filter((x) => x.credits > 0))}`
        : `<p class="hint">${grades.length ? '평점에 들어가는 성적이 없습니다 (재수강으로 지워진 성적만 있음)' : '등록된 성적이 없습니다'}</p>`;

    const semItems = sortSemesters(grades.map((g) => g.semester)).map((sem) => {
        const info = semInfo[sem];
        const rows = grades.filter((g) => g.semester === sem).map((g) => `
            <tr><td>${esc(g.courseCode)} ${esc(courseName(g.courseCode))}</td>
            <td class="num">${esc(creditOf(g.courseCode))}</td>
            <td class="num">${esc(g.score)}</td><td>${letterBadge(g.letter)}</td></tr>`).join('');
        return `
            <details class="fold" data-key="sem:${esc(sem)}">
                <summary>${esc(sem)} · 학기 GPA ${semGpa(info)} · 등록 학점 ${esc(info?.credits ?? '-')}</summary>
                <div class="table-wrap">
                    <table><thead><tr><th>과목</th><th class="num">학점</th><th class="num">점수</th><th>등급</th></tr></thead><tbody>${rows}</tbody></table>
                </div>
            </details>`;
    }).join('');
    const semHtml = semItems ? `<h3 class="sub-title">학기별 과목 성적</h3>${semItems}` : '';
    const warnHtml = warnings.length ? `
        <div class="table-wrap">
            <table><thead><tr><th>학기</th><th>사유</th><th class="num">평점</th><th class="num">신청 학점</th></tr></thead>
            <tbody>${warnings.map((w) => `
                <tr><td>${esc(w.semester)}</td><td><span class="badge F">${esc(w.reason)}</span></td>
                <td class="num">${w.gpa === null ? '-' : fmt(w.gpa)}</td><td class="num">${esc(w.credits)}</td></tr>`).join('')}
            </tbody></table>
        </div>` : '<p class="hint">학사경고 없음</p>';
    const stat = (label, v, small) => `<div><span>${label}</span><b${small ? ' class="small"' : ''}>${v}</b></div>`;
    return `
        <div class="report">
            <div class="stat-boxes">
                <div class="gpa-box">
                    <div class="gpa-col">
                        <span>전체 누적 GPA</span>
                        ${hasGpa ? `<b>${fmt(summary.gpa)}</b>` : '<b class="small">성적 없음</b>'}
                        <small>등록 학점 ${esc(summary.credits)}</small>
                    </div>
                    <div class="gpa-col">
                        <span>전공 GPA</span>
                        <b>${majorGpaText(summary)}</b>
                        <small>전공 학점 ${majorCreditText(summary)}</small>
                    </div>
                </div>
                ${stat('학번', esc(s.id), true)}
                ${stat('수강 건수', grades.length)}
            </div>
            ${summaryHtml}
            ${semHtml}
            <h3 class="sub-title">학사경고</h3>
            ${warnHtml}
            <div class="detail-actions"><a class="btn-link" href="/api/export/student/${id}">CSV 내려받기</a></div>
        </div>`;
}

// 학생 성적표 창 (보기 전용)
async function showStudentReport(s) {
    openDialog({ title: `${s.name} 성적표`, okText: '', bodyHtml: foldBarHtml() + await studentReportHtml(s) });
    syncFoldButton($('dlg-body'));
}

// ───────── 내 성적 (학생) ─────────

// 접었다 펼치는 영역(details.fold)을 한꺼번에 여닫는 버튼 줄. 접을 영역이 없으면 syncFoldButton 이 줄을 숨김
const foldBarHtml = () => '<div class="fold-bar" hidden><button type="button" data-fold-toggle>모두 펼치기</button></div>';
const foldRoot = (el) => el.closest('#myreport-body, #dlg-body');
// 버튼 글자를 지금 상태에 맞춤: 하나라도 닫혀 있으면 '모두 펼치기', 모두 열려 있으면 '모두 접기'
function syncFoldButton(root) {
    const bar = root?.querySelector('.fold-bar');
    if (!bar) return;
    const all = [...root.querySelectorAll('details.fold')];
    bar.hidden = !all.length;
    bar.querySelector('button').textContent = all.some((d) => !d.open) ? '모두 펼치기' : '모두 접기';
}
document.addEventListener('click', (e) => {
    const btn = e.target.closest('[data-fold-toggle]');
    const root = btn && foldRoot(btn);
    if (!root) return;
    const all = [...root.querySelectorAll('details.fold')];
    const open = all.some((d) => !d.open);
    all.forEach((d) => { d.open = open; });
    syncFoldButton(root);
});
// details 는 toggle 이벤트가 위로 올라가지 않아 capture 로 받음: 시뮬레이션 상자 펼침 상태 기억, 접는 영역이면 버튼 글자 갱신
document.addEventListener('toggle', (e) => {
    const d = e.target;
    if (!(d instanceof HTMLDetailsElement)) return;
    if (d.id === 'sim-box') mySim.open = d.open;
    else if (d.classList.contains('fold')) syncFoldButton(foldRoot(d));
}, true);

// ── 목표 GPA 시뮬레이션 (서버에 저장하지 않고 화면에서만 계산) ──

const GPA_MAX = 4.5;
const GPA_EPS = 1e-9;
// 등급 점수표 (낮은 점수부터, F = 0)
const GRADE_POINTS = [...Object.entries(LETTER_POINT), ['F', 0]].sort((a, b) => a[1] - b[1]);
const gradePointText = ([letter, p]) => `${letter}(${p.toFixed(1)})`;

// 목표 누적 GPA 를 위해 앞으로 필요한 평균 평점 (순수 함수)
//   gpa·credits: 지금 누적 평점·계산 학점 (gpa 가 null 이거나 credits 가 0 이면 성적 없음 = 학점 0)
//   target: 목표 누적 GPA, credit: 앞으로 들을 학점 (1 이상)
//   R = (target × (C + credit) − G × C) / credit
//   kind: 'impossible' (R > 4.5, maxGpa = 전부 A+ 를 받았을 때 누적), 'done' (R ≤ 0), 'need'
//   band: kind 가 'need' 일 때 R 이 놓인 등급 구간 ('B+(3.5) ~ A0(4.0) 사이', 정확히 등급 점수면 'A0(4.0) 이상')
function targetGpaNeed(gpa, credits, target, credit) {
    const C = typeof gpa === 'number' && credits > 0 ? credits : 0;
    const G = C > 0 ? gpa : 0;
    const R = (target * (C + credit) - G * C) / credit;
    const maxGpa = (G * C + GPA_MAX * credit) / (C + credit);
    if (R > GPA_MAX + GPA_EPS) return { kind: 'impossible', R, maxGpa, band: '' };
    if (R <= GPA_EPS) return { kind: 'done', R, maxGpa, band: '' };
    const exact = GRADE_POINTS.find(([, p]) => Math.abs(p - R) < GPA_EPS);
    const lower = [...GRADE_POINTS].reverse().find(([, p]) => p < R);
    const upper = GRADE_POINTS.find(([, p]) => p > R);
    const band = exact ? `${gradePointText(exact)} 이상` : `${gradePointText(lower)} ~ ${gradePointText(upper)} 사이`;
    return { kind: 'need', R, maxGpa, band };
}
// targetGpaNeed 결과 문구 (HTML). gpa = 지금 누적 평점 (null 이면 '-'), credit = 앞으로 들을 학점
function targetGpaHtml(res, gpa, credit) {
    if (res.kind === 'impossible') return `<span class="sim-bad">불가능 — 전부 A+ (${GPA_MAX}) 를 받아도 누적 ${fmt(res.maxGpa)}</span>`;
    if (res.kind === 'done') return `<span class="sim-good">이미 달성했습니다 (현재 ${fmt(gpa)})</span>`;
    return `앞으로 ${esc(credit)}학점 평균 평점 <b>${fmt(res.R)}</b> 이상 필요 <span class="hint">· ${esc(res.band)}</span>`;
}

// 시뮬레이션 상자 입력값·펼침 상태 (화면을 다시 그려도 유지, 다른 학생으로 로그인하면 초기화)
//   credits: null = 사용자가 건드리지 않음 (기본값 = 현재 학기 신청 학점 합)
const mySim = { owner: '', target: '', credits: null, open: false };

// 내 성적 화면 맨 위 '시뮬레이션' 상자 (접힘 기본). 안에 sim-section 을 하나씩 추가
//   summary: 서버 /api/stats/summary/:id 응답 (gpa, credits)
function simBoxHtml(summary) {
    if (mySim.owner !== state.me.studentId) Object.assign(mySim, { owner: state.me.studentId, target: '', credits: null, open: false });
    const hasGpa = typeof summary.gpa === 'number' && summary.credits > 0;
    const planned = state.myEnrollments.filter((e) => e.semester === state.currentSem)
        .reduce((t, e) => t + (Number(state.courses.find((c) => c.code === e.courseCode)?.credit) || 0), 0);
    const credits = mySim.credits ?? (planned > 0 ? String(planned) : '');
    return `
        <details id="sim-box"${mySim.open ? ' open' : ''}>
            <summary>시뮬레이션</summary>
            <section class="sim-section" id="sim-target" data-gpa="${hasGpa ? esc(summary.gpa) : ''}" data-credits="${hasGpa ? esc(summary.credits) : 0}">
                <h4 class="assess-head">목표 GPA</h4>
                <p class="hint">현재 누적 GPA ${hasGpa ? fmt(summary.gpa) : '-'} · 계산 학점 ${hasGpa ? esc(summary.credits) : 0} · 입력값은 서버에 저장하지 않고 화면에서만 계산합니다</p>
                <div class="sim-row">
                    <label>목표 누적 GPA <input type="number" class="sim-field" id="sim-target-gpa" min="0" max="4.5" step="any" placeholder="0~4.5" value="${esc(mySim.target)}"></label>
                    <label>앞으로 들을 학점 <input type="number" class="sim-field" id="sim-target-credits" min="1" step="1" placeholder="학점" value="${esc(credits)}"></label>
                </div>
                <p class="sim-result"></p>
            </section>
        </details>`;
}
// 목표 GPA 칸 두 개를 읽어 결과 줄을 다시 계산. 범위 밖·숫자가 아닌 입력은 계산에서 빼고 칸에 invalid 클래스
function updateSimTarget(sec) {
    const parse = (input, valid) => {
        const raw = input.value.trim();
        const n = Number(raw);
        const ok = raw !== '' && Number.isFinite(n) && valid(n);
        input.classList.toggle('invalid', !ok && (raw !== '' || input.validity.badInput));
        return ok ? n : null;
    };
    const target = parse(sec.querySelector('#sim-target-gpa'), (n) => n >= 0 && n <= GPA_MAX);
    const credit = parse(sec.querySelector('#sim-target-credits'), (n) => Number.isInteger(n) && n >= 1);
    const out = sec.querySelector('.sim-result');
    if (target === null || credit === null) {
        out.innerHTML = '<span class="hint">목표 GPA (0~4.5) 와 앞으로 들을 학점 (1 이상 정수) 을 입력하면 필요한 평균 평점을 계산합니다</span>';
        return;
    }
    const gpa = sec.dataset.gpa === '' ? null : Number(sec.dataset.gpa);
    out.innerHTML = targetGpaHtml(targetGpaNeed(gpa, Number(sec.dataset.credits), target, credit), gpa, credit);
}
$('tab-myreport').addEventListener('input', (e) => {
    const input = e.target.closest('#sim-target input');
    if (!input) return;
    if (input.id === 'sim-target-gpa') mySim.target = input.value;
    else mySim.credits = input.value;
    updateSimTarget(document.querySelector('#sim-target'));
});

// 내 성적 화면: 접기 버튼 줄 + 시뮬레이션 상자 + '진행 중 과목 (평가 항목)' (평가 기준·출결 기록이 있는 과목이 없으면 생략) + 성적표
//   요약(GPA·학기별 평점·그래프·학사경고)은 그대로 보이고, 과목 단위(진행 중 과목 카드·학기별 과목 성적)는 접힌 줄 (눌러서 펼침)
//   다시 그릴 때 펼쳐 둔 영역(data-key)은 그대로 펼침
async function renderMyReport() {
    const seq = renderSeq;
    const summaryReq = api('GET', '/api/stats/summary/' + encodeURIComponent(state.me.studentId));
    const [progress, report, summary] = await Promise.all([
        myAssessmentsHtml(),
        studentReportHtml({ id: state.me.studentId, name: state.me.name }, summaryReq),
        summaryReq,
    ]);
    if (seq !== renderSeq) return;
    const body = $('myreport-body');
    const opened = new Set([...body.querySelectorAll('details.fold[open]')].map((d) => d.dataset.key));
    body.innerHTML = foldBarHtml() + simBoxHtml(summary) + progress + report;
    body.querySelectorAll('details.fold').forEach((d) => { d.open = opened.has(d.dataset.key); });
    updateSimTarget(document.querySelector('#sim-target'));
    syncFoldButton(body);
}

// '진행 중 과목 (평가 항목)': 내가 수강신청했거나 항목 점수가 있는 (과목, 학기) 중 현재 학기 이후이고 평가 기준이 있는 것마다
//   분류별 비율·내 점수 표(비율 0인 분류는 뺌)와 한 줄 요약
//     최종 성적이 있으면 그 점수·등급
//     없으면 estimateLine (점수가 없는 분류 중 출석이 아닌 칸에 예상 점수 입력칸, 입력하면 simLine 으로 다시 계산, 서버로 보내지 않음)
//   평가 기준이 있는 과목이 없으면 빈 문자열
//   출결 기록이 있는 과목은 내 출결 요약 줄도 보임 (평가 기준이 없어도 그 과목을 보임, 기록이 없는 과목은 줄 없음)
//   출결을 못 받으면 이 영역 안에 한 줄 오류만 보이고 나머지는 그대로
async function myAssessmentsHtml() {
    const [scores, grades, attend] = await Promise.all([
        api('GET', '/api/assessment-scores'),
        api('GET', '/api/grades?studentId=' + encodeURIComponent(state.me.studentId)),
        myAttendance(),
    ]);
    const combos = new Map();
    [...state.myEnrollments, ...scores, ...attend.summaries.values()]
        .filter((e) => !isPastSemester(e.semester, state.currentSem))
        .forEach((e) => combos.set(JSON.stringify([e.courseCode, e.semester]), { code: e.courseCode, sem: e.semester }));
    const cache = new Map();
    const list = await Promise.all([...combos.values()].map(async (x) => {
        const { mode, items } = await assessmentsOf(x.code, x.sem, cache);
        return { ...x, mode, items };
    }));
    const shown = list.filter((x) => x.items.length || attend.summaries.has(JSON.stringify([x.code, x.sem])))
        .sort((a, b) => b.sem.localeCompare(a.sem) || a.code.localeCompare(b.code));
    const attendError = attend.error ? `<p class="attend-error">출결을 불러오지 못했습니다: ${esc(attend.error)}</p>` : '';
    if (!shown.length) return attendError ? `<div class="report"><h3 class="sub-title">진행 중 과목 (평가 항목)</h3>${attendError}</div>` : '';

    const cards = shown.map(({ code, sem, mode, items }) => {
        const att = attend.summaries.get(JSON.stringify([code, sem]));
        // 한 과목 = 접었다 펼치는 카드. summary = 과목명 + 한 줄 요약(fold-line, 예상 점수를 입력하면 갱신)
        const card = (line, bodyHtml) => `
            <details class="fold" data-key="c:${esc(code)}|${esc(sem)}">
                <summary>${esc(courseName(code))} <span class="hint">${esc(code)} · ${esc(sem)}</span> · <span class="fold-line">${line}</span></summary>
                ${bodyHtml}
            </details>`;
        if (!items.length) return card(`출석 점수 <b>${fmt(att.score)}</b>`, `<p class="attend-line">${attendSummaryLine(att)}</p>`);
        const cols = items.filter((a) => a.weight > 0);
        const mine = (name) => scores.find((s) => s.courseCode === code && s.semester === sem && s.name === name);
        const final = grades.find((g) => g.courseCode === code && g.semester === sem);
        const simulate = (a) => !final && !mine(a.name) && a.name !== ATTEND_ITEM;
        const parts = cols.map((a) => ({ weight: a.weight, score: mine(a.name)?.score ?? null }));
        const finalLine = final ? `최종 성적 <b>${esc(round2(final.score))}점</b> ${letterBadge(final.letter)}` : '';
        const line = finalLine || estimateLine(parts, mode, cols.some(simulate));
        const shortLine = finalLine || estimateLine(parts, mode, false, true);
        const rows = cols.map((a) => {
            const s = mine(a.name);
            const cell = s ? esc(s.score)
                : simulate(a) ? '<input type="number" class="sim-input" min="0" max="100" step="any" placeholder="예상">' : '-';
            return `
            <tr data-w="${esc(a.weight)}"${s ? ` data-score="${esc(s.score)}"` : ''}>
                <td>${esc(a.name)}</td><td class="num">${esc(a.weight)}</td><td class="num">${cell}</td></tr>`;
        }).join('');
        return card(shortLine, `
            <div class="table-wrap">
                <table data-mode="${esc(mode)}"><thead><tr><th>항목</th><th class="num">비율(%)</th><th class="num">내 점수</th></tr></thead>
                <tbody>${rows}</tbody>
                <tfoot><tr><td colspan="3" class="estimate sim-line">${line}</td></tr>
                ${att ? `<tr><td colspan="3" class="estimate">${attendSummaryLine(att)}</td></tr>` : ''}</tfoot></table>
            </div>`);
    }).join('');
    return `<div class="report"><h3 class="sub-title">진행 중 과목 (평가 항목)</h3>${attendError}${cards}</div>`;
}

// 최종 성적이 없는 과목의 요약 줄 (미확정)
//   parts: 비율이 0보다 큰 분류마다 {weight, score} (score = 받은 점수 또는 유효한 예상 점수, 없으면 null)
//   모두 차면 예상 최종 = Σ(점수×비율) / 100 (소수 둘째 자리 반올림), 점수 기준 등급 (scoreToLetter 경계)
//   빈칸이 남으면 현재까지 환산 = Σ(점수×비율) / Σ(점수가 있는 분류의 비율), canSimulate(예상 점수 칸이 있음)면 빈칸 안내를 붙임
//   상대평가 과목이면 실제 등급은 순위로 정해진다는 안내를 덧붙임
//   short: 접힌 카드 머리글(summary)용 짧은 줄 (안내 문구 없이 환산·예상 최종과 등급만)
function estimateLine(parts, mode, canSimulate, short = false) {
    const done = parts.filter((p) => p.score !== null);
    const total = parts.reduce((t, p) => t + p.weight, 0);
    const doneWeight = done.reduce((t, p) => t + p.weight, 0);
    const sum = done.reduce((t, p) => t + p.score * p.weight, 0);
    let line;
    if (parts.length && done.length === parts.length) {
        const est = round2(sum / 100);
        line = short ? `예상 최종 <b>${est.toFixed(2)}점</b> (${letterBadge(scoreToLetter(est))})`
            : `예상 최종 <b>${est.toFixed(2)}점</b> (점수 기준 등급 ${letterBadge(scoreToLetter(est))}, 미확정)`;
    } else {
        if (doneWeight > 0) {
            const est = round2(sum / doneWeight);
            line = short ? `현재까지 환산 <b>${est}점</b> (${letterBadge(scoreToLetter(est))})`
                : `현재까지 환산 <b>${est}점</b> (예상 등급 ${letterBadge(scoreToLetter(est))}, 미확정)
                <span class="hint">· 점수가 입력된 비율 ${doneWeight} / ${total}</span>`;
        } else {
            line = '<span class="hint">아직 입력된 점수가 없습니다</span>';
        }
        if (canSimulate && !short) line += ' <span class="hint">· 빈칸에 예상 점수를 넣으면 최종 점수를 계산합니다</span>';
    }
    if (mode === 'relative' && !short) line += ` <span class="hint">· 상대평가 과목이라 실제 등급은 수강생 순위로 정해집니다 (${RELATIVE_RULES})</span>`;
    return line;
}

// 예상 점수 칸이 있는 과목 표 하나의 요약 줄을 화면 값으로 다시 계산 (받은 점수 = 행의 data-score, 예상 점수 = 입력칸)
//   0~100 밖이거나 숫자가 아닌 입력은 계산에서 빼고 칸에 invalid 클래스. short 는 estimateLine 의 short
function simLine(table, short = false) {
    const parts = [...table.querySelectorAll('tbody tr[data-w]')].map((tr) => {
        const weight = Number(tr.dataset.w);
        const input = tr.querySelector('input.sim-input');
        if (!input) return { weight, score: tr.dataset.score === undefined ? null : Number(tr.dataset.score) };
        const raw = input.value.trim();
        const n = Number(raw);
        const ok = raw !== '' && Number.isFinite(n) && n >= 0 && n <= 100;
        input.classList.toggle('invalid', !ok && (raw !== '' || input.validity.badInput));
        return { weight, score: ok ? n : null };
    });
    return estimateLine(parts, table.dataset.mode, true, short);
}
// 예상 점수를 입력할 때마다 그 과목 요약 줄(표 아래 줄 + 카드 머리글)만 다시 그림
//   (서버로 보내지 않고, 화면을 다시 그리면 사라짐. 카드를 다시 만들지 않으므로 펼침 상태는 그대로)
$('tab-myreport').addEventListener('input', (e) => {
    const input = e.target.closest('input.sim-input');
    if (!input) return;
    const table = input.closest('table');
    table.querySelector('td.sim-line').innerHTML = simLine(table);
    const fold = table.closest('details');
    if (fold) fold.querySelector('.fold-line').innerHTML = simLine(table, true);
});

// 내 출결 요약 (학생, 서버가 쿼리 없이 본인 기록 전체를 줌): {summaries: (과목, 학기) → 서버 요약, error}
//   score 가 null(기록 없음)인 요약은 뺌. 실패하면 빈 목록과 오류 문구 (던지지 않음)
async function myAttendance() {
    try {
        const data = await attendanceOf('');
        const list = data.summaries.filter((s) => typeof s.score === 'number');
        return { summaries: new Map(list.map((s) => [JSON.stringify([s.courseCode, s.semester]), s])), error: '' };
    } catch (err) {
        return { summaries: new Map(), error: err.message };
    }
}
// 출결 요약 한 줄: 출석 N · 지각 N · 결석 N · 출석 점수 NN.NN (서버 값 그대로), shortage 면 '출석 미달' 경고 배지
const attendSummaryLine = (s) =>
    `출석 ${esc(s.present)} · 지각 ${esc(s.late)} · 결석 ${esc(s.absent)} · 출석 점수 <b>${fmt(s.score)}</b>` +
    (s.shortage ? ' <span class="badge F">출석 미달</span>' : '');

// ───────── 성적 ─────────

// 성적 폼·필터의 학생/과목 선택 항목 (교수는 담당 과목만, 학생 목록은 서버가 걸러 준 그대로)
const studentOptions = () => state.students.map((s) => ({ value: s.id, label: `${s.name} (${s.id})` }));
const courseOptions = () => gradeCourses().map((c) => ({ value: c.code, label: `${c.name} (${c.code})` }));

// 성적 표 그리기. 필터 선택 상자를 최신 목록으로 채우고, 고른 조건을 쿼리로 붙여 성적을 받아옴
// 수정/삭제 버튼에는 성적을 구분하는 키 [학번, 과목코드, 학기] 를 JSON 으로 담아 둠
// 평가 항목으로 계산되는 (과목, 학기) 성적은 수정·삭제 대신 '평가 항목' 표시 (서버도 409 로 막음)
// 관리자 외에는 지난 학기 성적의 수정·삭제 대신 '지난 학기' 표시 (서버도 403 으로 막음)
//   표에 나온 (과목, 학기) 조합을 /api/assessments/batch 한 번으로 조회 (잠긴 지난 학기는 제외)
async function renderGrades() {
    const seq = renderSeq;
    const fs = $('grade-f-student'), fc = $('grade-f-course'), fm = $('grade-f-semester');
    // 교수에게는 지난 학기를 뺀 목록 (현재 학기는 항상 포함). 관리자·학생 화면은 그대로
    const all = await semesterList();
    if (seq !== renderSeq) return;
    const semesters = state.me.role === 'professor' ? visibleSemesters(all) : all;
    fillSelect(fs, studentOptions(), '전체 학생', fs.value);
    fillSelect(fc, courseOptions(), isAdmin() ? '전체 과목' : '전체 담당 과목', fc.value);
    fillSelect(fm, semesters.map((s) => ({ value: s, label: s })), '전체 학기', fm.value);

    const params = new URLSearchParams();
    if (fs.value) params.set('studentId', fs.value);
    if (fc.value) params.set('courseCode', fc.value);
    if (fm.value) params.set('semester', fm.value);
    const qs = params.toString();
    const grades = await api('GET', '/api/grades' + (qs ? '?' + qs : ''));
    if (seq !== renderSeq) return;
    grades.sort((a, b) => b.semester.localeCompare(a.semester) || a.studentId.localeCompare(b.studentId));

    // 관리자는 10건씩 페이지로 나누고, 교수는 기존처럼 전체 목록을 표시
    const paginated = isAdmin();
    const totalPages = Math.max(1, Math.ceil(grades.length / GRADE_PAGE_SIZE));
    if (paginated) gradePage = Math.min(Math.max(1, gradePage), totalPages);
    else gradePage = 1;

    const offset = paginated ? (gradePage - 1) * GRADE_PAGE_SIZE : 0;
    const visibleGrades = paginated ? grades.slice(offset, offset + GRADE_PAGE_SIZE) : grades;
    const pairs = [...new Map(visibleGrades
        .filter((g) => !isLockedSemester(g.semester))
        .map((g) => [JSON.stringify([g.courseCode, g.semester]), {
            courseCode: g.courseCode, semester: g.semester,
        }])).values()];
    const assessmentRows = pairs.length
        ? await api('POST', '/api/assessments/batch', { pairs }).catch(() => [])
        : [];
    if (seq !== renderSeq) return;
    const assessedByPair = new Map(assessmentRows.map((a) => [
        JSON.stringify([a.courseCode, a.semester]), Array.isArray(a.items) && a.items.length > 0,
    ]));
    const assessed = visibleGrades.map((g) => isLockedSemester(g.semester) ? false
        : assessedByPair.get(JSON.stringify([g.courseCode, g.semester])) ?? false);

    $('grade-body').innerHTML = visibleGrades.length ? visibleGrades.map((g, i) => {
        const key = esc(JSON.stringify([g.studentId, g.courseCode, g.semester]));
        const actions = isLockedSemester(g.semester)
            ? '<span class="tag" title="지난 학기 성적은 관리자만 고칠 수 있습니다">지난 학기</span>'
            : assessed[i]
            ? '<span class="tag" title="평가 항목 점수로 자동 계산되는 성적입니다 (과목 페이지에서 항목 점수를 입력하세요)">평가 항목</span>'
            : `<button class="link" data-act="edit" data-k='${key}'>수정</button>
                <button class="link danger" data-act="del" data-k='${key}'>삭제</button>`;
        return `
        <tr>
            <td>${esc(g.semester)}</td>
            <td>${esc(g.studentId)}</td>
            <td>${esc(studentName(g.studentId))}</td>
            <td>${esc(g.courseCode)} ${esc(courseName(g.courseCode))}</td>
            <td class="num">${esc(g.score)}</td>
            <td>${letterBadge(g.letter)}</td>
            <td class="actions">${actions}</td>
        </tr>`;
    }).join('') : emptyRow(7, '조건에 맞는 성적이 없습니다');

    const pager = $('grade-pagination');
    if (pager) {
        pager.hidden = !paginated;
        pager.innerHTML = paginated ? `
            <button type="button" data-page="prev" ${gradePage <= 1 ? 'disabled' : ''}>이전</button>
            <span>${gradePage} / ${totalPages} 페이지 · 전체 ${grades.length}건</span>
            <button type="button" data-page="next" ${gradePage >= totalPages ? 'disabled' : ''}>다음</button>
        ` : '';
    }
}

// 필터를 바꾸면 표를 다시 그림
['grade-f-student', 'grade-f-course', 'grade-f-semester'].forEach((id) =>
    ($(id).onchange = () => {
        gradePage = 1;
        renderGrades().catch((e) => toast(e.message, true));
    }));

// 관리자 성적 목록 페이지 이동
$('grade-pagination').onclick = (e) => {
    const button = e.target.closest('button[data-page]');
    if (!button || button.disabled) return;
    gradePage += button.dataset.page === 'next' ? 1 : -1;
    renderGrades().catch((err) => toast(err.message, true));
};

// 성적 등록/수정 폼 항목. 수정할 때는 학생·과목·학기를 못 바꾸고 점수(또는 등급)만 고침
// (서버가 PUT 본문의 학번·과목코드·학기로 대상을 찾기 때문)
// 입력 방식: 점수(등급은 서버가 점수로 정함) / 등급(서버 letterToScore 의 대표 점수로 저장). bindGradeMode 가 칸을 바꿔 보임
const gradeFields = (editing) => [
    { name: 'studentId', label: '학생', options: studentOptions(), readonly: editing },
    { name: 'courseCode', label: '과목', options: courseOptions(), readonly: editing },
    { name: 'semester', label: '학기', readonly: editing, placeholder: '예: 2026-1 (1·2 정규, 3 여름, 4 겨울)' },
    { name: 'mode', label: '입력 방식', options: [{ value: 'score', label: '점수' }, { value: 'letter', label: '등급' }] },
    { name: 'score', label: '점수 (0~100, 등급은 자동: 95↑ A+ · 90↑ A0 · 85↑ B+ · … · 60↑ D0 · 그 아래 F)', type: 'number', step: 'any', min: 0, max: 100 },
    { name: 'letter', label: '등급 (대표 점수로 저장: A+ 95 · A0 90 · B+ 85 · … · D0 60 · F 0)', options: asOptions(GRADE_LETTERS) },
    { name: 'pf', label: '합격 여부 (P/F 과목, 평점에 들어가지 않음)', options: [
        { value: 'P', label: 'P (합격)' }, { value: 'NP', label: 'NP (불합격)' },
    ] },
];
// 입력 방식에 따라 점수 칸 / 등급 선택 상자 중 하나만 보임. 숨긴 칸은 disabled 라 폼 값(FormData)에서 빠짐
//   고른 과목이 P/F 과목이면 입력 방식·점수·등급 칸을 숨기고 합격 여부(P/NP)만 보임
function bindGradeMode() {
    const body = $('dlg-body');
    const mode = body.querySelector('select[name=mode]');
    const score = body.querySelector('input[name=score]');
    const letter = body.querySelector('select[name=letter]');
    const pf = body.querySelector('select[name=pf]');
    // 수정 폼은 과목 선택 상자가 disabled 라 같은 이름의 숨은 칸에 값이 있음
    const course = body.querySelector('input[type=hidden][name=courseCode]') ?? body.querySelector('select[name=courseCode]');
    const show = (el, on) => { el.disabled = !on; el.closest('.field').hidden = !on; };
    const sync = () => {
        const passFail = isPassFailCourse(course.value);
        const byLetter = mode.value === 'letter';
        show(mode, !passFail);
        show(score, !passFail && !byLetter);
        show(letter, !passFail && byLetter);
        show(pf, passFail);
    };
    // 방식·과목을 바꾸면 이전 검사 오류 문구는 지움
    const changed = () => { sync(); body.querySelector('.form-error').textContent = ''; };
    mode.onchange = changed;
    if (course.tagName === 'SELECT') course.addEventListener('change', changed);
    sync();
}
// 성적 폼 값 검사 (서버와 같은 문구): 잘못되면 이유, 괜찮으면 빈 문자열
const validateGrade = (d) => {
    if (!SEMESTER_RE.test((d.semester ?? '').trim())) return SEMESTER_RULES;
    if (isLockedSemester(d.semester.trim())) return pastSemesterMessage();
    if (d.pf !== undefined) return PF_LETTERS.includes(d.pf) ? '' : 'P/F 과목은 P(합격) 또는 NP(불합격)로 입력하세요';
    if (d.mode === 'letter') {
        return GRADE_LETTERS.includes(d.letter) ? '' : '지원하지 않는 성적 등급입니다 (A+ A0 B+ B0 C+ C0 D+ D0 F)';
    }
    const score = Number(d.score);
    if ((d.score ?? '').trim() === '' || !Number.isFinite(score) || score < 0 || score > 100) return SCORE_RULES;
    return '';
};
// 폼 값 → API 본문: 점수 방식이면 {score}, 등급 방식이면 {letter}, P/F 과목이면 {letter: 'P' | 'NP'}
const gradeBody = (d) => {
    const key = { studentId: d.studentId, courseCode: d.courseCode, semester: d.semester.trim() };
    if (d.pf !== undefined) return { ...key, letter: d.pf };
    return d.mode === 'letter' ? { ...key, letter: d.letter } : { ...key, score: Number(d.score) };
};
// 저장 결과 문구 (서버가 돌려준 성적 g 기준): 점수 방식 "NN점 → 등급", 등급 방식 "등급 A+ (95점)"
//   재수강 A0 제한 등으로 반영 등급이 입력 등급과 다르면 "→ 반영 등급" 을 붙임
//   P/F 과목은 "P (합격)" / "NP (불합격)"
const gradeResultText = (d, g) => {
    if (d.pf !== undefined) return g.letter === 'P' ? 'P (합격)' : `${g.letter} (불합격)`;
    return d.mode === 'letter'
        ? `등급 ${d.letter} (${round2(g.score)}점)${g.letter !== d.letter ? ` → ${g.letter}` : ''}`
        : `${round2(g.score)}점 → ${g.letter}`;
};

// 서버 kNotEnrolled 와 같은 문구 (성적 등록에서 수강신청이 없을 때 돌려줌)
const NOT_ENROLLED_MSG = '수강신청하지 않은 학생입니다 (학번·과목·학기를 확인하세요)';

// 성적 등록 (수강신청한 학생·과목·학기만 가능). 고를 학생이나 과목이 없으면 안내만 띄움
$('grade-add').onclick = () => {
    if (!state.students.length || !courseOptions().length) {
        toast(isAdmin() ? '학생과 과목을 먼저 등록하세요' : '담당 과목 또는 수강생이 없습니다', true);
        return;
    }
    openForm({
        title: '성적 등록',
        fields: gradeFields(false),
        values: { mode: 'score' },
        validate: validateGrade,
        onOpen: () => {},
        onSubmit: async (d) => {
            enrollGuide.hidden = true;
            openQuickButton.hidden = true;
            try {
                const g = await api('POST', '/api/grades', gradeBody(d));
                toast(`성적을 등록했습니다: ${gradeResultText(d, g)}`);
            } catch (err) {
                // 수강신청이 없어서 막힌 경우에만 빠른 성적 등록 안내 (오류 문구는 그대로 폼에 표시)
                if (err.message === NOT_ENROLLED_MSG) {
                    enrollGuide.hidden = false;
                    openQuickButton.hidden = false;
                }
                throw err;
            }
            await refreshAll();
        },
    });
    bindGradeMode();

    const body = $('dlg-body');
    const courseSelect = body.querySelector('select[name="courseCode"]');
    const semesterInput = body.querySelector('input[name="semester"]');
    const saveButton = $('dlg-ok');

    const guide = document.createElement('div');
    guide.className = 'form-error';
    guide.style.color = '#d97706';
    body.appendChild(guide);

    const moveButton = document.createElement('button');
    moveButton.type = 'button';
    moveButton.textContent = '평가 기준 화면 열기';
    moveButton.hidden = true;
    body.appendChild(moveButton);

    // 수강신청이 없다는 오류가 났을 때만 보이는 안내와, 입력값을 유지한 채 빠른 성적 등록을 여는 버튼
    const enrollGuide = document.createElement('div');
    enrollGuide.className = 'form-error';
    enrollGuide.style.color = '#d97706';
    enrollGuide.textContent = "수강신청이 없습니다. '빠른 성적 등록' 으로 수강 정보와 성적을 한 번에 등록할 수 있습니다";
    enrollGuide.hidden = true;
    body.appendChild(enrollGuide);

    const openQuickButton = document.createElement('button');
    openQuickButton.type = 'button';
    openQuickButton.textContent = '빠른 성적 등록으로 열기';
    openQuickButton.hidden = true;
    body.appendChild(openQuickButton);

    openQuickButton.onclick = () => {
        const values = Object.fromEntries(new FormData($('dlg-form')));
        openQuickGradeForm(values);
    };

    // 학생·과목·학기·점수를 고치면 이전 오류에 맞춘 안내는 숨김
    const hideEnrollGuide = () => { enrollGuide.hidden = true; openQuickButton.hidden = true; };
    body.addEventListener('input', hideEnrollGuide);
    body.addEventListener('change', hideEnrollGuide);

    let checkId = 0;

    const checkAssessment = async () => {
        const id = ++checkId;
        const code = courseSelect.value;
        const semester = semesterInput.value.trim();

        guide.textContent = '';
        moveButton.hidden = true;
        saveButton.disabled = false;

        if (!code || !SEMESTER_RE.test(semester)) return;

        try {
            const { items } = await assessmentsOf(code, semester, new Map());

            if (id !== checkId) return;

            if (items.length > 0) {
                guide.textContent =
                    '평가 기준으로 성적을 계산하는 과목입니다.\n평가 기준 화면에서 점수를 입력하세요.';
                moveButton.hidden = false;
                saveButton.disabled = true;
            }
        } catch (err) {
            if (id === checkId) toast(err.message, true);
        }
    };

    courseSelect.addEventListener('change', checkAssessment);
    semesterInput.addEventListener('input', checkAssessment);

    moveButton.onclick = () => {
        const code = courseSelect.value;
        const semester = semesterInput.value.trim();

        dlg.close();
        showTab('course', code, semester);
    };

    checkAssessment();
};


// 빠른 성적 등록 버튼 추가 (기존 성적 등록 버튼 유지)
const quickGradeButton = $('grade-add').cloneNode(true);
quickGradeButton.id = 'grade-quick-add';
quickGradeButton.textContent = '빠른 성적 등록';

// 기존 성적 등록 버튼 바로 옆에 배치
$('grade-add').after(quickGradeButton);

// 빠른 성적 등록
quickGradeButton.onclick = () => {
    if (!state.students.length || !courseOptions().length) {
        toast(isAdmin()
            ? '학생과 과목을 먼저 등록하세요'
            : '담당 과목 또는 수강생이 없습니다', true);
        return;
    }
    openQuickGradeForm();
};

// 빠른 성적 등록 폼 열기. values 로 학생·과목·학기·입력 방식·점수(등급)를 미리 채울 수 있음
//   (성적 등록 폼에서 "수강신청이 없습니다" 안내의 버튼으로 넘어올 때 입력값을 그대로 가져옴)
function openQuickGradeForm(values = { mode: 'score' }) {
    openForm({
        title: '빠른 성적 등록',
        fields: gradeFields(false),
        values,
        validate: validateGrade,

        onSubmit: async (d) => {
            // 빠른 등록 전용 API 호출
            const g = await api(
                'POST',
                '/api/grades/quick',
                gradeBody(d)
            );

            // 수강 정보 자동 생성 여부에 따라 결과 안내
            if (g.enrollmentCreated) {
                toast(`수강 정보 자동 생성 및 성적 등록 완료: ${gradeResultText(d, g)}`);
            } else {
                toast(`성적을 등록했습니다: ${gradeResultText(d, g)}`);
            }

            await refreshAll();
        },
    });

    bindGradeMode();
}




// 성적 표 안 버튼 처리. 수정은 서버에서 그 성적을 다시 받아 폼에 채움
$('grade-body').onclick = async (e) => {
    const b = e.target.closest('button[data-act]');
    if (!b) return;
    const [studentId, courseCode, semester] = JSON.parse(b.dataset.k);
    const q = new URLSearchParams({ studentId, courseCode, semester });
    if (b.dataset.act === 'edit') {
        let g;
        try { [g] = await api('GET', '/api/grades?' + q); }
        catch (err) { toast(err.message, true); return; }
        if (!g) { toast('성적을 찾을 수 없습니다', true); return; }
        openForm({
            title: '성적 수정',
            fields: gradeFields(true),
            values: { ...g, mode: 'score', letter: GRADE_LETTERS.includes(g.letter) ? g.letter : scoreToLetter(g.score),
                pf: PF_LETTERS.includes(g.letter) ? g.letter : 'P' },
            validate: validateGrade,
            onSubmit: async (d) => {
                const saved = await api('PUT', '/api/grades', gradeBody(d));
                toast(`수정했습니다: ${gradeResultText(d, saved)}`);
                await refreshAll();
            },
        });
        bindGradeMode();
    }
    if (b.dataset.act === 'del') confirmDelete(
        `${studentName(studentId)} · ${courseName(courseCode)} · ${semester} 성적을 삭제할까요?`,
        () => api('DELETE', '/api/grades?' + q));
};

// ───────── 계정 (관리자) ─────────

// 계정 폼 항목. 이름은 교수·관리자, 학생 선택은 학생 역할일 때만 보임 (bindRoleToggle)
// 수정할 때는 아이디를 못 바꾸고, 비밀번호는 바꿀 때만 입력
const accountFields = (editing) => [
    { name: 'username', label: '아이디 (영문·숫자·_ 3~20자)', readonly: editing },
    { name: 'role', label: '역할', options: Object.entries(ROLE_LABEL).map(([value, label]) => ({ value, label })) },
    { name: 'name', label: '이름 (교수·관리자)', optional: true },
    { name: 'studentId', label: '학생', options: studentOptions() },
    {
        name: 'password', label: '비밀번호 (8~64자)', type: 'password', autocomplete: 'new-password',
        optional: editing, placeholder: editing ? '바꿀 때만 입력' : '',
    },
];
// 폼 값 → API 본문. 학생 계정은 이름 없이 학번만, 교수·관리자는 학번 없이 이름만
const accountBody = (d) => {
    const student = d.role === 'student';
    return { role: d.role, name: student ? '' : (d.name ?? '').trim(), studentId: student ? (d.studentId ?? '') : '', password: d.password ?? '' };
};
// 역할 선택에 따라 이름 / 학생 칸을 바꿔 보임
function bindRoleToggle() {
    const body = $('dlg-body');
    const role = body.querySelector('select[name=role]');
    const nameField = body.querySelector('[name=name]').closest('.field');
    const studentField = body.querySelector('[name=studentId]').closest('.field');
    const sync = () => {
        nameField.hidden = role.value === 'student';
        studentField.hidden = role.value !== 'student';
    };
    role.onchange = sync;
    sync();
}

// 계정 표 그리기 (해시·salt 는 서버가 내보내지 않음)
async function renderAccounts() {
    const seq = renderSeq;
    const accounts = await api('GET', '/api/accounts');
    if (seq !== renderSeq) return;
    state.accounts = accounts;
    $('account-body').innerHTML = state.accounts.length ? state.accounts.map((a) => `
        <tr>
            <td><b>${esc(a.username)}</b>${a.username === state.me.username ? ' <span class="tag">나</span>' : ''}</td>
            <td>${esc(ROLE_LABEL[a.role] ?? a.role)}</td>
            <td>${esc(a.name) || '-'}</td>
            <td>${esc(a.studentId) || '-'}</td>
            <td class="actions">
                <button class="link" data-act="edit" data-u="${esc(a.username)}">수정</button>
                <button class="link danger" data-act="del" data-u="${esc(a.username)}">삭제</button>
            </td>
        </tr>`).join('') : emptyRow(5, '계정이 없습니다');
}

$('account-add').onclick = () => {
    openForm({
        title: '계정 만들기',
        fields: accountFields(false),
        values: { role: 'professor' },
        onSubmit: async (d) => {
            await api('POST', '/api/accounts', { username: d.username.trim(), ...accountBody(d) });
            toast('계정을 만들었습니다');
            await refreshAll();
        },
    });
    bindRoleToggle();
};

// 표 안 버튼(수정/삭제). 내 계정을 고친 뒤에는 내 정보를 다시 받아 화면(탭·이름)을 새로 그림
$('account-body').onclick = (e) => {
    const b = e.target.closest('button[data-act]');
    if (!b) return;
    const a = state.accounts.find((x) => x.username === b.dataset.u);
    if (!a) return;
    if (b.dataset.act === 'edit') {
        openForm({
            title: '계정 수정',
            fields: accountFields(true),
            values: { ...a, name: a.role === 'student' ? '' : a.name },
            onSubmit: async (d) => {
                await api('PUT', '/api/accounts/' + encodeURIComponent(a.username), accountBody(d));
                toast('수정했습니다');
                if (a.username === state.me.username) startApp(await api('GET', '/api/me'));
                else await refreshAll();
            },
        });
        bindRoleToggle();
    }
    if (b.dataset.act === 'del') confirmDelete(
        `${a.username} 계정을 삭제할까요?`,
        () => api('DELETE', '/api/accounts/' + encodeURIComponent(a.username)));
};

// ───────── 시작 ─────────

// 처음 열면 로그인 상태를 확인: 로그인돼 있으면 앱을, 아니면(401) 로그인 화면을 띄움
api('GET', '/api/me').then(startApp).catch(() => { if (!state.me) showLogin(); });
