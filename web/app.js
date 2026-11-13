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
let gradePage = 1;
const GRADE_PAGE_SIZE = 10;

const state = {
    me: null, students: [], courses: [], stats: {}, accounts: [], enrollCtx: null,
    myEnrollments: [], courseCode: null, courseSem: null, postFilter: 'all', coursePosts: [],
    assessSem: null, assess: null, attendSem: null, attend: null, currentSem: '',
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

// 제목·내용을 채우고 창을 띄움
// validate 가 있으면 브라우저 기본 검사(required·min·pattern)를 끄고 validate 문구만 보임 (서버와 같은 문구)
function openDialog({ title, bodyHtml, onSubmit, validate = null, okText = '저장' }) {
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
$('dlg-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    if (!dlgSubmit) return;
    const data = Object.fromEntries(new FormData($('dlg-form')));
    const box = $('dlg-body').querySelector('.form-error');
    const invalid = dlgValidate?.(data);
    if (invalid) {
        if (box) box.textContent = invalid;
        toast(invalid, true);
        return;
    }
    try {
        await dlgSubmit(data);
        dlg.close();
    } catch (err) {
        if (box) box.textContent = err.message;
    }
});

// 입력 폼 창 만들기
//   fields: [{name, label, type, readonly, optional, step, min, max, pattern, hint, placeholder, autocomplete, options:[{value,label}]}]
//     min·max·pattern·hint 는 입력칸 검사용 (hint 는 pattern 이 안 맞을 때 브라우저가 보여 줄 설명)
//     options 가 있으면 선택 상자, type 이 'textarea' 면 여러 줄 입력칸, 그 외는 입력칸
//     readonly 는 수정할 때 키(학번 등)를 못 바꾸게 할 때 사용
//     optional 이면 빈칸으로 저장할 수 있음 (그 외 입력칸은 필수)
//   values: 처음 채워둘 값, onSubmit: 저장할 때 실행할 함수 (폼 값 객체를 받음)
//   validate: 저장 전에 폼 값을 검사하는 함수 (잘못되면 이유 문자열을 돌려줌, 서버 호출 전에 막음)
function openForm({ title, fields, values = {}, onSubmit, validate }) {
    const html = fields.map((f) => {
        const v = esc(values[f.name] ?? '');
        let input;
        if (f.options) {
            input = `<select name="${f.name}" ${f.readonly ? 'disabled' : ''}>` +
                f.options.map((o) =>
                    `<option value="${esc(o.value)}" ${String(o.value) === String(values[f.name]) ? 'selected' : ''}>${esc(o.label)}</option>`
                ).join('') + '</select>';
            // disabled 항목은 FormData 에서 빠지므로 숨은 값으로 보냄
            if (f.readonly) input += `<input type="hidden" name="${f.name}" value="${v}">`;
        } else if (f.type === 'textarea') {
            // 시작 태그 바로 뒤 줄바꿈 하나는 HTML 이 버리므로, 값이 줄바꿈으로 시작해도 남도록 하나를 먼저 넣음
            input = `<textarea name="${f.name}" rows="6" ${f.placeholder ? `placeholder="${esc(f.placeholder)}"` : ''}
                ${f.optional ? '' : 'required'}>\n${v}</textarea>`;
        } else {
            input = `<input name="${f.name}" type="${f.type || 'text'}" value="${v}" ${f.step ? `step="${f.step}"` : ''}
                ${f.min !== undefined ? `min="${f.min}"` : ''} ${f.max !== undefined ? `max="${f.max}"` : ''}
                ${f.pattern ? `pattern="${esc(f.pattern)}"` : ''} ${f.hint ? `title="${esc(f.hint)}"` : ''}
                ${f.readonly ? 'readonly' : ''} ${f.placeholder ? `placeholder="${esc(f.placeholder)}"` : ''}
                ${f.autocomplete ? `autocomplete="${f.autocomplete}"` : ''} ${f.optional ? '' : 'required'}>`;
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

// 탭 버튼·화면을 바꾸고 그 탭을 다시 그림 (탭을 열 때마다 서버에서 최신 값을 받음)
//   code: 과목 페이지면 그 과목코드. 다른 과목으로 바뀌면 학기·분류·평가 기준·출결 학기 선택을 처음으로 되돌림
function showTab(name, code, assessSemester = null) {
    if (code && code !== state.courseCode) {
        state.courseCode = code;
        state.courseSem = null;
        state.postFilter = 'all';
        state.assessSem = null;
        state.attendSem = null;
    }

    if (name === 'course' && assessSemester) {
        state.assessSem = assessSemester;
    }

    document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('active',
        b.dataset.tab === (name === 'course' ? 'courses' : name)));
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.id === 'tab-' + name));
    refreshAll().catch((e) => toast(e.message, true));
}
$('tabs').onclick = (e) => {
    const b = e.target.closest('button[data-tab]');
    if (b && !b.hidden) showTab(b.dataset.tab);
};

// 학생·과목 목록과 현재 학기를 서버에서 받아 state 에 저장 (학생 목록은 서버가 역할에 맞게 걸러 줌)
// 학생이면 자기 수강신청도 받음 (서버가 자기 것만 줌) → myCourses 가 사용
async function loadBase() {
    let cur;
    [state.students, state.courses, state.myEnrollments, cur] = await Promise.all([
        api('GET', '/api/students'),
        api('GET', '/api/courses'),
        state.me.role === 'student' ? api('GET', '/api/enrollments') : [],
        api('GET', '/api/semester/current'),
    ]);
    state.currentSem = cur.semester ?? '';
}
// 등록·수정·삭제 뒤에 호출: 목록을 다시 받고 지금 탭을 다시 그림
// 과목 페이지를 보던 중 그 과목을 볼 수 없게 되면(과목 삭제·권한 변경) 수강과목 탭으로 돌아감
async function refreshAll() {
    if (!state.me) return;
    await loadBase();
    if (currentTab() === 'course' && !myCourses().some((c) => c.code === state.courseCode)) {
        showTab('courses');
        return;
    }
    await renderers[currentTab()]();
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
        'assess-sem', 'assess-body', 'attend-sem', 'attend-body'].forEach((id) => ($(id).innerHTML = ''));
    ['student-search', 'enroll-sem-input', 'enroll-set-sem'].forEach((id) => ($(id).value = ''));
    delete $('enroll-sem-select').dataset.course;
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
const courseFormValues = (c) => ({ ...c, remote: c.remote ? 'Y' : 'N' });

// 과목 목록 그리기 (myCourses: 교수는 담당 과목, 학생은 수강신청한 과목만)
// 과목마다 통계 API 도 같이 호출해 state.stats 에 저장 (못 받으면 null)
// 한 줄에 과목 정보(왼쪽)와 최근 학기 수강 인원·평균(오른쪽). 성적이 없으면 평균은 '-'
//   최근 학기 = 수강신청이 1건 이상 있는 가장 늦은 학기 (신청 기록이 없으면 학기 없이 "수강 0")
async function renderCourses() {
    const list = $('course-list');
    const courses = myCourses();
    const stats = await Promise.all(courses.map((c) =>
        api('GET', '/api/stats/course/' + encodeURIComponent(c.code)).catch(() => null)));
    state.stats = Object.fromEntries(courses.map((c, i) => [c.code, stats[i]]));
    if (!courses.length) {
        const msg = { professor: '담당 과목이 없습니다', student: '수강신청한 과목이 없습니다' }[state.me.role] ?? '등록된 과목이 없습니다';
        list.innerHTML = `<li class="empty">${msg}</li>`;
        return;
    }
    list.innerHTML = courses.map((c) => {
        const st = state.stats[c.code];
        const has = st && st.count > 0;
        const sem = st?.latestSemester ?? '';
        return `<li class="course-row" data-code="${esc(c.code)}">
            <div class="course-info">
                <div class="course-name">${esc(c.name)}${c.area ? ` <span class="tag">${esc(c.area)}</span>` : ''}</div>
                <div class="course-line">${esc(c.code)} · ${esc(c.professor) || '-'} · ${esc(c.category)}</div>
                <div class="course-line">${esc(c.schedule) || '-'} · ${esc(c.credit)}학점 · ${esc(c.language)}${c.remote ? ' <span class="tag">원격</span>' : ''}</div>
            </div>
            <div class="course-meta">
                <span class="stat-line">${sem ? esc(sem) + ' ' : ''}수강 <b>${sem ? esc(st.latestEnrolled) : 0}</b> / ${capacityText(c)}</span>
                <span class="stat-line">${has ? `평균 <b>${fmt(st.average, 1)}</b> (${fmt(st.max, 0)} / ${fmt(st.min, 0)})` : '평균 -'}</span>
            </div>
        </li>`;
    }).join('');
}

// 과목 상세 창 (보기 전용): 전체 항목 + 통계 상자 + CSV / 수정 / 삭제 버튼
//   CSV 는 관리자·담당 교수만, 수정·삭제는 관리자만
function showCourseDetail(c, st) {
    const has = st && st.count > 0;
    const sem = st?.latestSemester ?? '';
    const rows = [
        ['이수구분', esc(c.category)],
        ['학점', esc(c.credit)],
        ['담당 교수', esc(c.professor) || '-'],
        ['강의시간', esc(c.schedule) || '-'],
        ['강의언어', esc(c.language)],
        ['교양영역', esc(c.area) || '-'],
        ['정원', c.capacity > 0 ? esc(c.capacity) + '명' : '제한 없음'],
        ['원격강좌', c.remote ? '예' : '아니오'],
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
                ${stat('최근 학기 수강', sem ? esc(st.latestEnrolled) + '명' : '-', sem ? esc(sem) : '')}
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
//   처음 보이는 학기: 이전 선택 → 게시물이 있는 가장 최근 학기 → (관리자·교수) 신청 학기(지난 학기면 제외) → 가장 최근 학기
//   관리자·담당 교수는 아래 '평가 기준'·'출결' 영역도 그림 (같은 학기 목록, 평가 기준 처음 학기: 이전 선택 → 신청 학기 → 게시물 학기,
//   출결 처음 학기: 이전 선택 → 평가 기준 학기). 출결 영역은 불러오기 실패를 영역 안에 보이고 던지지 않음
async function renderCoursePage() {
    const code = state.courseCode;
    const c = pageCourse();
    if (!c) { showTab('courses'); return; }
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
    if (currentTab() !== 'course' || state.courseCode !== code) return;

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
    if (!sems.includes(state.courseSem)) state.courseSem = sems.find((s) => posts.some((p) => p.semester === s)) ?? fallback;
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
        showCourseDetail(c, await api('GET', '/api/stats/course/' + encodeURIComponent(c.code)));
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
    if (currentTab() !== 'course' || state.courseCode !== code || state.assessSem !== sem) return;
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
    const sem = state.attendSem ?? '';
    const box = $('attend-body');
    const stale = () => currentTab() !== 'course' || state.courseCode !== code || state.attendSem !== sem;
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

// ───────── 수강신청 ─────────

// 수강신청 탭 그리기. 맨 위에 신청 학기(또는 기간 아님)를 보이고 역할별 화면으로 나눔
//   학생: 자기 수강신청 (신청 학기에만 신청·취소)
//   관리자: 신청 학기 설정 + 고른 학생의 수강신청 (모든 학기에서 신청·취소)
//   교수: 담당 과목의 학기별 수강생 조회만
async function renderEnroll() {
    const role = state.me.role;
    const { semester: open = '' } = await api('GET', '/api/enrollment/settings');
    const period = $('enroll-period');
    period.textContent = open ? `수강신청 학기: ${open}` : '수강신청 기간이 아닙니다';
    period.classList.toggle('closed', !open);
    $('enroll-admin').hidden = role !== 'admin';
    $('enroll-student-wrap').hidden = role !== 'admin';
    $('enroll-course-wrap').hidden = role !== 'professor';
    $('enroll-sem-input').hidden = role !== 'admin';
    $('enroll-sem-select').hidden = role === 'admin';
    if (role === 'admin') $('enroll-set-sem').value = open;
    if (role === 'professor') return renderEnrollProfessor(open);
    return renderEnrollStudent(open, role === 'admin');
}

// 학생(또는 관리자가 고른 학생)의 "개설 과목" + "신청 내역" 두 목록
async function renderEnrollStudent(open, admin) {
    const content = $('enroll-content');
    let sid = state.me.studentId;
    if (admin) {
        const pick = $('enroll-student');
        fillSelect(pick, studentOptions(), '', pick.value);
        if (!pick.value) { state.enrollCtx = null; content.innerHTML = '<p class="hint">등록된 학생이 없습니다</p>'; return; }
        sid = pick.value;
    }
    const mine = await api('GET', '/api/enrollments?studentId=' + encodeURIComponent(sid));

    // 학기: 관리자는 직접 입력(비면 신청 학기, 그것도 비면 가장 최근 성적 학기),
    //       학생은 신청 학기 + 자기 수강신청에 나온 학기 중에서 선택
    let sem;
    if (admin) {
        const input = $('enroll-sem-input');
        if (!input.value.trim()) input.value = open || (await semesterList())[0] || '';
        sem = input.value.trim();
    } else {
        const sel = $('enroll-sem-select');
        const sems = sortSemesters([open, ...mine.map((e) => e.semester)]);
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
    const countOf = Object.fromEntries(counts.map((x) => [x.courseCode, x.count]));
    const inSem = mine.filter((e) => e.semester === sem).sort((a, b) => a.courseCode.localeCompare(b.courseCode));
    const applied = new Set(inSem.map((e) => e.courseCode));
    // 학생은 신청 학기에만 신청·취소 (그 학기가 지난 학기면 불가), 관리자는 모든 학기
    const locked = isLockedSemester(sem);
    const editable = admin || (open !== '' && sem === open && !locked);

    const courseRows = state.courses.length ? state.courses.map((c) => `
        <tr>
            <td><b>${esc(c.name)}</b> <span class="hint">${esc(c.code)}</span></td>
            <td>${esc(c.professor) || '-'}</td>
            <td>${esc(c.schedule) || '-'}</td>
            <td class="num">${esc(c.credit)}</td>
            <td class="num">신청 ${esc(countOf[c.code] ?? 0)} / ${capacityText(c)}</td>
            <td class="actions"><button class="link" data-act="apply" data-code="${esc(c.code)}"
                ${applied.has(c.code) || !editable ? 'disabled' : ''}>신청</button></td>
        </tr>`).join('') : emptyRow(6, '등록된 과목이 없습니다');

    const course = (code) => state.courses.find((c) => c.code === code);
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
async function renderEnrollProfessor(open) {
    const content = $('enroll-content');
    state.enrollCtx = null;
    const pick = $('enroll-course');
    fillSelect(pick, state.courses.filter(teaches).map((c) => ({ value: c.code, label: `${c.name} (${c.code})` })), '', pick.value);
    if (!pick.value) { content.innerHTML = '<p class="hint">담당 과목이 없습니다</p>'; return; }
    const code = pick.value;
    const list = await api('GET', '/api/enrollments?courseCode=' + encodeURIComponent(code));

    // 학기: 신청 학기 + 이 과목 수강신청에 나온 학기 (최신순)
    // 과목을 바꿨을 때 처음 보이는 학기: 신청 학기에 수강생이 있으면 그 학기, 없으면 수강생이 있는 가장 최근 학기
    const sel = $('enroll-sem-select');
    const listSems = sortSemesters(list.map((e) => e.semester));
    let keep = sel.dataset.course === code ? sel.value : '';
    if (!keep) keep = open && listSems.includes(open) ? open : (listSems[0] || open);
    fillSelect(sel, sortSemesters([open, ...listSems]).map((s) => ({ value: s, label: s })), '', keep);
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
// 학생마다 GPA API 를 한 번씩 불러서 GPA 열을 채우고, 학사경고 목록으로 학생별 경고 횟수를 셈
// 성적이 없는 학생은 서버가 gpa null 을 줌 → '-' (fmt)
async function renderStudents() {
    const q = $('student-search').value.trim();
    let list = state.students;
    if (q) {
        try { list = await api('GET', '/api/students/search?q=' + encodeURIComponent(q)); }
        catch (e) { toast(e.message, true); }
    }
    const body = $('student-body');
    if (!list.length) { body.innerHTML = emptyRow(7, q ? '검색 결과가 없습니다' : '등록된 학생이 없습니다'); return; }

    const [gpas, warnings] = await Promise.all([
        Promise.all(list.map((s) =>
            api('GET', '/api/stats/gpa/' + encodeURIComponent(s.id)).then(gpaOf).catch(() => null))),
        api('GET', '/api/stats/warnings').catch(() => []),
    ]);
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
            <td class="num">${fmt(gpas[i])}</td>
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
//   (재수강으로 지워진 성적·없는 과목은 서버가 이미 뺌, 계산 학점이 0 이면 gpa null = 등록된 성적 없음)
async function studentReportHtml(s) {
    const id = encodeURIComponent(s.id);
    const [grades, summary, warnings] = await Promise.all([
        api('GET', '/api/grades?studentId=' + id),
        api('GET', '/api/stats/summary/' + id),
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

    const semHtml = sortSemesters(grades.map((g) => g.semester)).map((sem) => {
        const info = semInfo[sem];
        const rows = grades.filter((g) => g.semester === sem).map((g) => `
            <tr><td>${esc(g.courseCode)} ${esc(courseName(g.courseCode))}</td>
            <td class="num">${esc(creditOf(g.courseCode))}</td>
            <td class="num">${esc(g.score)}</td><td>${letterBadge(g.letter)}</td></tr>`).join('');
        const semesterCredits = info?.credits ?? '-';
        return `
            <h3 class="sub-title">${esc(sem)}</h3>
            <div class="semester-summary-line">
                <span class="semester-gpa">GPA ${semGpa(info)}</span>
                <span class="semester-credit">등록 학점 ${esc(semesterCredits)}학점</span>
            </div>
            <div class="table-wrap">
                <table><thead><tr><th>과목</th><th class="num">학점</th><th class="num">점수</th><th>등급</th></tr></thead><tbody>${rows}</tbody></table>
            </div>`;
    }).join('');
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
                ${stat('학번', esc(s.id), true)}
                ${hasGpa ? stat('전체 누적 GPA', fmt(summary.gpa)) : stat('전체 누적 GPA', '성적 없음', true)}
                ${stat('전체 등록 학점', esc(summary.credits))}
                ${stat('전공 GPA', majorGpaText(summary))}
                ${stat('전공 학점', majorCreditText(summary))}
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
    openDialog({ title: `${s.name} 성적표`, okText: '', bodyHtml: await studentReportHtml(s) });
}

// ───────── 내 성적 (학생) ─────────

// 내 성적 화면: 맨 위 '진행 중 과목 (평가 항목)' (평가 기준·출결 기록이 있는 과목이 없으면 생략) + 성적표
async function renderMyReport() {
    const [progress, report] = await Promise.all([
        myAssessmentsHtml(),
        studentReportHtml({ id: state.me.studentId, name: state.me.name }),
    ]);
    $('myreport-body').innerHTML = progress + report;
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
        const head = `<h4 class="assess-head">${esc(courseName(code))} <span class="hint">${esc(code)} · ${esc(sem)}</span></h4>`;
        const att = attend.summaries.get(JSON.stringify([code, sem]));
        if (!items.length) return head + `<p class="attend-line">${attendSummaryLine(att)}</p>`;
        const cols = items.filter((a) => a.weight > 0);
        const mine = (name) => scores.find((s) => s.courseCode === code && s.semester === sem && s.name === name);
        const final = grades.find((g) => g.courseCode === code && g.semester === sem);
        const simulate = (a) => !final && !mine(a.name) && a.name !== ATTEND_ITEM;
        const line = final
            ? `최종 성적 <b>${esc(round2(final.score))}점</b> ${letterBadge(final.letter)}`
            : estimateLine(cols.map((a) => ({ weight: a.weight, score: mine(a.name)?.score ?? null })), mode, cols.some(simulate));
        const rows = cols.map((a) => {
            const s = mine(a.name);
            const cell = s ? esc(s.score)
                : simulate(a) ? '<input type="number" class="sim-input" min="0" max="100" step="any" placeholder="예상">' : '-';
            return `
            <tr data-w="${esc(a.weight)}"${s ? ` data-score="${esc(s.score)}"` : ''}>
                <td>${esc(a.name)}</td><td class="num">${esc(a.weight)}</td><td class="num">${cell}</td></tr>`;
        }).join('');
        return `
            ${head}
            <div class="table-wrap">
                <table data-mode="${esc(mode)}"><thead><tr><th>항목</th><th class="num">비율(%)</th><th class="num">내 점수</th></tr></thead>
                <tbody>${rows}</tbody>
                <tfoot><tr><td colspan="3" class="estimate sim-line">${line}</td></tr>
                ${att ? `<tr><td colspan="3" class="estimate">${attendSummaryLine(att)}</td></tr>` : ''}</tfoot></table>
            </div>`;
    }).join('');
    return `<div class="report"><h3 class="sub-title">진행 중 과목 (평가 항목)</h3>${attendError}${cards}</div>`;
}

// 최종 성적이 없는 과목의 요약 줄 (미확정)
//   parts: 비율이 0보다 큰 분류마다 {weight, score} (score = 받은 점수 또는 유효한 예상 점수, 없으면 null)
//   모두 차면 예상 최종 = Σ(점수×비율) / 100 (소수 둘째 자리 반올림), 점수 기준 등급 (scoreToLetter 경계)
//   빈칸이 남으면 현재까지 환산 = Σ(점수×비율) / Σ(점수가 있는 분류의 비율), canSimulate(예상 점수 칸이 있음)면 빈칸 안내를 붙임
//   상대평가 과목이면 실제 등급은 순위로 정해진다는 안내를 덧붙임
function estimateLine(parts, mode, canSimulate) {
    const done = parts.filter((p) => p.score !== null);
    const total = parts.reduce((t, p) => t + p.weight, 0);
    const doneWeight = done.reduce((t, p) => t + p.weight, 0);
    const sum = done.reduce((t, p) => t + p.score * p.weight, 0);
    let line;
    if (parts.length && done.length === parts.length) {
        const est = round2(sum / 100);
        line = `예상 최종 <b>${est.toFixed(2)}점</b> (점수 기준 등급 ${letterBadge(scoreToLetter(est))}, 미확정)`;
    } else {
        if (doneWeight > 0) {
            const est = round2(sum / doneWeight);
            line = `현재까지 환산 <b>${est}점</b> (예상 등급 ${letterBadge(scoreToLetter(est))}, 미확정)
                <span class="hint">· 점수가 입력된 비율 ${doneWeight} / ${total}</span>`;
        } else {
            line = '<span class="hint">아직 입력된 점수가 없습니다</span>';
        }
        if (canSimulate) line += ' <span class="hint">· 빈칸에 예상 점수를 넣으면 최종 점수를 계산합니다</span>';
    }
    if (mode === 'relative') line += ` <span class="hint">· 상대평가 과목이라 실제 등급은 수강생 순위로 정해집니다 (${RELATIVE_RULES})</span>`;
    return line;
}

// 예상 점수 칸이 있는 과목 표 하나의 요약 줄을 화면 값으로 다시 계산 (받은 점수 = 행의 data-score, 예상 점수 = 입력칸)
//   0~100 밖이거나 숫자가 아닌 입력은 계산에서 빼고 칸에 invalid 클래스
function simLine(table) {
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
    return estimateLine(parts, table.dataset.mode, true);
}
// 예상 점수를 입력할 때마다 그 과목 요약 줄만 다시 그림 (서버로 보내지 않고, 화면을 다시 그리면 사라짐)
$('tab-myreport').addEventListener('input', (e) => {
    const input = e.target.closest('input.sim-input');
    if (!input) return;
    const table = input.closest('table');
    table.querySelector('td.sim-line').innerHTML = simLine(table);
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
//   표에 나온 (과목, 학기) 조합마다 /api/assessments 를 한 번씩만 부름 (잠긴 지난 학기는 부르지 않음)
async function renderGrades() {
    const fs = $('grade-f-student'), fc = $('grade-f-course'), fm = $('grade-f-semester');
    const semesters = await semesterList();
    fillSelect(fs, studentOptions(), '전체 학생', fs.value);
    fillSelect(fc, courseOptions(), isAdmin() ? '전체 과목' : '전체 담당 과목', fc.value);
    fillSelect(fm, semesters.map((s) => ({ value: s, label: s })), '전체 학기', fm.value);

    const params = new URLSearchParams();
    if (fs.value) params.set('studentId', fs.value);
    if (fc.value) params.set('courseCode', fc.value);
    if (fm.value) params.set('semester', fm.value);
    const qs = params.toString();
    const grades = await api('GET', '/api/grades' + (qs ? '?' + qs : ''));
    grades.sort((a, b) => b.semester.localeCompare(a.semester) || a.studentId.localeCompare(b.studentId));

    // 관리자는 10건씩 페이지로 나누고, 교수는 기존처럼 전체 목록을 표시
    const paginated = isAdmin();
    const totalPages = Math.max(1, Math.ceil(grades.length / GRADE_PAGE_SIZE));
    if (paginated) gradePage = Math.min(Math.max(1, gradePage), totalPages);
    else gradePage = 1;

    const offset = paginated ? (gradePage - 1) * GRADE_PAGE_SIZE : 0;
    const visibleGrades = paginated ? grades.slice(offset, offset + GRADE_PAGE_SIZE) : grades;
    const cache = new Map();
    const assessed = await Promise.all(visibleGrades.map((g) => isLockedSemester(g.semester) ? false
        : assessmentsOf(g.courseCode, g.semester, cache).then((a) => a.items.length > 0)));

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
];
// 입력 방식에 따라 점수 칸 / 등급 선택 상자 중 하나만 보임. 숨긴 칸은 disabled 라 폼 값(FormData)에서 빠짐
function bindGradeMode() {
    const body = $('dlg-body');
    const mode = body.querySelector('select[name=mode]');
    const score = body.querySelector('input[name=score]');
    const letter = body.querySelector('select[name=letter]');
    const sync = () => {
        const byLetter = mode.value === 'letter';
        score.disabled = byLetter;
        score.closest('.field').hidden = byLetter;
        letter.disabled = !byLetter;
        letter.closest('.field').hidden = !byLetter;
    };
    // 방식을 바꾸면 이전 방식의 검사 오류 문구는 지움
    mode.onchange = () => { sync(); body.querySelector('.form-error').textContent = ''; };
    sync();
}
// 성적 폼 값 검사 (서버와 같은 문구): 잘못되면 이유, 괜찮으면 빈 문자열
const validateGrade = (d) => {
    if (!SEMESTER_RE.test((d.semester ?? '').trim())) return SEMESTER_RULES;
    if (isLockedSemester(d.semester.trim())) return pastSemesterMessage();
    if (d.mode === 'letter') {
        return GRADE_LETTERS.includes(d.letter) ? '' : '지원하지 않는 성적 등급입니다 (A+ A0 B+ B0 C+ C0 D+ D0 F)';
    }
    const score = Number(d.score);
    if ((d.score ?? '').trim() === '' || !Number.isFinite(score) || score < 0 || score > 100) return SCORE_RULES;
    return '';
};
// 폼 값 → API 본문: 점수 방식이면 {score}, 등급 방식이면 {letter}
const gradeBody = (d) => {
    const key = { studentId: d.studentId, courseCode: d.courseCode, semester: d.semester.trim() };
    return d.mode === 'letter' ? { ...key, letter: d.letter } : { ...key, score: Number(d.score) };
};
// 저장 결과 문구 (서버가 돌려준 성적 g 기준): 점수 방식 "NN점 → 등급", 등급 방식 "등급 A+ (95점)"
//   재수강 A0 제한 등으로 반영 등급이 입력 등급과 다르면 "→ 반영 등급" 을 붙임
const gradeResultText = (d, g) => (d.mode === 'letter'
    ? `등급 ${d.letter} (${round2(g.score)}점)${g.letter !== d.letter ? ` → ${g.letter}` : ''}`
    : `${round2(g.score)}점 → ${g.letter}`);

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
            const g = await api('POST', '/api/grades', gradeBody(d));
            toast(`성적을 등록했습니다: ${gradeResultText(d, g)}`);
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

    openForm({
        title: '빠른 성적 등록',
        fields: gradeFields(false),
        values: { mode: 'score' },
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
};




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
            values: { ...g, mode: 'score', letter: GRADE_LETTERS.includes(g.letter) ? g.letter : scoreToLetter(g.score) },
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
    state.accounts = await api('GET', '/api/accounts');
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
