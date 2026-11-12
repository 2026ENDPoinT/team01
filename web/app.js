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
const state = {
    me: null, students: [], courses: [], stats: {}, accounts: [], enrollCtx: null,
    myEnrollments: [], courseCode: null, courseSem: null, postFilter: 'all', coursePosts: [],
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
// GPA 응답에서 숫자만 꺼냄 ({gpa: 3.5} 또는 3.5 둘 다 처리)
const gpaOf = (r) => (typeof r === 'number' ? r : r?.gpa);
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
// 중복을 지운 학기 목록 (최신 학기가 위로, 빈 값 제외)
const sortSemesters = (list) => [...new Set(list.filter(Boolean))].sort().reverse();

// ───────── 역할 ─────────

const ROLE_LABEL = { admin: '관리자', professor: '교수', student: '학생' };
const isAdmin = () => state.me?.role === 'admin';
// 담당 과목 판정 (서버 teaches 와 같은 규칙): 교수 계정이고, 과목 담당 교수를 , 로 나눈 이름 중 하나가 내 이름
const teaches = (c) => state.me?.role === 'professor' &&
    String(c.professor ?? '').split(',').map((s) => s.trim()).includes(state.me.name);
// 성적 탭에서 고를 수 있는 과목: 관리자는 전체, 교수는 담당 과목만
const gradeCourses = () => (isAdmin() ? state.courses : state.courses.filter(teaches));
// 수강과목 목록·과목 탭에 보이는 과목: 관리자는 전체, 교수는 담당 과목, 학생은 수강신청한 과목
const myCourses = () => {
    if (isAdmin()) return state.courses;
    if (state.me?.role === 'professor') return state.courses.filter(teaches);
    if (state.me?.role !== 'student') return [];
    const mine = new Set(state.myEnrollments.map((e) => e.courseCode));
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
function openDialog({ title, bodyHtml, onSubmit, validate = null, okText = '저장' }) {
    $('dlg-title').textContent = title;
    $('dlg-body').innerHTML = bodyHtml;
    $('dlg-ok').textContent = okText;
    $('dlg-ok').style.display = onSubmit ? '' : 'none';
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

// 탭 이름 → 그 탭을 그리는 함수 (course = 과목 탭, 어떤 과목인지는 state.courseCode)
const renderers = {
    courses: renderCourses,
    enroll: renderEnroll,
    students: renderStudents,
    grades: renderGrades,
    myreport: renderMyReport,
    ranking: renderRanking,
    accounts: renderAccounts,
    course: renderCoursePage,
};
// 지금 열린 탭 이름 (보이는 화면 section 의 id 'tab-이름' 에서 꺼냄. 과목 탭 버튼이 다시 그려져도 그대로)
const currentTab = () => document.querySelector('main .tab.active').id.slice('tab-'.length);

// 탭 버튼·화면을 바꾸고 그 탭을 다시 그림 (탭을 열 때마다 서버에서 최신 값을 받음)
//   code: 과목 탭이면 그 과목코드. 다른 과목으로 바뀌면 학기·분류 선택을 처음으로 되돌림
function showTab(name, code) {
    if (code && code !== state.courseCode) {
        state.courseCode = code;
        state.courseSem = null;
        state.postFilter = 'all';
    }
    document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('active',
        b.dataset.tab === 'course' ? name === 'course' && b.dataset.code === state.courseCode : b.dataset.tab === name));
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.id === 'tab-' + name));
    refreshAll().catch((e) => toast(e.message, true));
}
$('tabs').onclick = (e) => {
    const b = e.target.closest('button[data-tab]');
    if (b && !b.hidden) showTab(b.dataset.tab, b.dataset.code);
};

// 학생·과목 목록을 서버에서 받아 state 에 저장 (학생 목록은 서버가 역할에 맞게 걸러 줌)
// 학생이면 자기 수강신청도 받음 (서버가 자기 것만 줌) → myCourses 가 사용
async function loadBase() {
    [state.students, state.courses, state.myEnrollments] = await Promise.all([
        api('GET', '/api/students'),
        api('GET', '/api/courses'),
        state.me.role === 'student' ? api('GET', '/api/enrollments') : [],
    ]);
}
// 상단 과목 탭을 myCourses() 순서대로 다시 채움. 지금 과목 페이지의 과목 탭은 active
function renderCourseTabs() {
    const onCourse = currentTab() === 'course';
    $('course-tabs').innerHTML = myCourses().map((c) =>
        `<button data-tab="course" data-code="${esc(c.code)}"${onCourse && c.code === state.courseCode ? ' class="active"' : ''}>${esc(c.name)}</button>`
    ).join('');
}
// 등록·수정·삭제 뒤에 호출: 목록을 다시 받고 지금 탭을 다시 그림
// 과목 페이지를 보던 중 그 과목을 볼 수 없게 되면(과목 삭제·권한 변경) 수강과목 탭으로 돌아감
async function refreshAll() {
    if (!state.me) return;
    await loadBase();
    renderCourseTabs();
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
    state.students = [];
    state.courses = [];
    state.stats = {};
    state.accounts = [];
    state.enrollCtx = null;
    state.myEnrollments = [];
    state.courseCode = null;
    state.courseSem = null;
    state.postFilter = 'all';
    state.coursePosts = [];
    ['course-list', 'student-body', 'grade-body', 'rank-body', 'enroll-content', 'myreport-body', 'account-body',
        'grade-f-student', 'grade-f-course', 'grade-f-semester', 'rank-semester',
        'enroll-student', 'enroll-course', 'enroll-sem-select',
        'course-tabs', 'course-hero', 'course-filter', 'course-posts', 'course-sem'].forEach((id) => ($(id).innerHTML = ''));
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
    $('login-username').focus();
}

// 로그인한 계정으로 앱 시작: 역할에 허용된 탭만 보이고 첫 탭(수강과목)을 엶 (과목 탭은 refreshAll 이 채움)
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
// 과목 폼 값 검사 (서버 course.cpp 규칙과 같게): 잘못되면 이유, 괜찮으면 빈 문자열
const validateCourse = (d) => {
    if (!/^\d{6}$/.test(d.code ?? '')) return '과목 코드는 숫자 6자리여야 합니다';
    if (!(d.name ?? '').trim()) return '과목명을 입력하세요 (공백만으로는 안 됩니다)';
    const credit = Number(d.credit);
    if (d.credit === '' || !Number.isInteger(credit) || credit < 1 || credit > 4) return '학점은 1~4 사이의 정수여야 합니다';
    if (d.capacity !== '' && (!Number.isInteger(Number(d.capacity)) || Number(d.capacity) < 0)) return '정원은 0 이상의 정수여야 합니다 (0 = 제한 없음)';
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

// 행을 누르면 그 과목의 과목 탭 (상세 창은 과목 페이지의 "과목 정보" 버튼)
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
// 게시물을 추가·수정·삭제할 수 있는지: 관리자 또는 담당 교수
const canEditPosts = (c) => isAdmin() || teaches(c);
// 마감·시험일이 지났는지 (날짜만 있으면 그날 23:59 기준, 시각은 브라우저 지역 시간)
function isExpired(due) {
    if (!due) return false;
    const t = new Date((due.length === 10 ? due + ' 23:59' : due).replace(' ', 'T'));
    return !Number.isNaN(t.getTime()) && t < new Date();
}

// 과목 페이지 그리기: 게시물과 학기 목록을 받고 학기를 정한 뒤 drawPosts 로 그림
//   학기 목록: 관리자·교수는 게시물 학기 + 이 과목 수강신청 학기 + 신청 학기, 학생은 자기가 수강신청한 학기
//   처음 보이는 학기: 이전 선택 → 게시물이 있는 가장 최근 학기 → (관리자·교수) 신청 학기 → 가장 최근 학기
async function renderCoursePage() {
    const code = state.courseCode;
    const c = pageCourse();
    if (!c) { showTab('courses'); return; }
    $('post-add').hidden = !canEditPosts(c);
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

    let sems, fallback;
    if (staff) {
        const [list, settings] = staffInfo;
        sems = sortSemesters([...posts.map((p) => p.semester), ...list.map((e) => e.semester), settings.semester]);
        fallback = settings.semester || sems[0] || '';
    } else {
        sems = sortSemesters(state.myEnrollments.filter((e) => e.courseCode === code).map((e) => e.semester));
        fallback = sems[0] || '';
    }
    if (!sems.includes(state.courseSem)) state.courseSem = sems.find((s) => posts.some((p) => p.semester === s)) ?? fallback;
    fillSelect($('course-sem'), sems.map((s) => ({ value: s, label: s })), '', state.courseSem);

    state.coursePosts = posts;
    drawPosts();
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
    const editable = canEditPosts(c);
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
    // 학생은 신청 학기에만 신청·취소, 관리자는 모든 학기
    const editable = admin || (open !== '' && sem === open);

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
        ${editable ? '' : `<p class="hint">${open ? `신청·취소는 수강신청 학기(${esc(open)})에만 할 수 있습니다` : '지금은 수강신청 기간이 아닙니다'}</p>`}
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
// 성적이 한 건도 없는 학생은 GPA 를 0.00 대신 '-' 로 표시 (전체 성적을 한 번만 받아 학번별로 셈)
async function renderStudents() {
    const q = $('student-search').value.trim();
    let list = state.students;
    if (q) {
        try { list = await api('GET', '/api/students/search?q=' + encodeURIComponent(q)); }
        catch (e) { toast(e.message, true); }
    }
    const body = $('student-body');
    if (!list.length) { body.innerHTML = emptyRow(7, q ? '검색 결과가 없습니다' : '등록된 학생이 없습니다'); return; }

    const [gpas, warnings, allGrades] = await Promise.all([
        Promise.all(list.map((s) =>
            api('GET', '/api/stats/gpa/' + encodeURIComponent(s.id)).then(gpaOf).catch(() => null))),
        api('GET', '/api/stats/warnings').catch(() => []),
        api('GET', '/api/grades').catch(() => null),
    ]);
    const hasGrade = new Set((allGrades ?? []).map((g) => g.studentId));
    const warnCount = {};
    warnings.forEach((w) => (warnCount[w.studentId] = (warnCount[w.studentId] ?? 0) + 1));
    body.innerHTML = list.map((s, i) => `
        <tr>
            <td>${esc(s.id)}</td>
            <td><b>${esc(s.name)}</b></td>
            <td>${esc(s.major)}</td>
            <td>${esc(s.year)}학년</td>
            <td class="num">${allGrades && !hasGrade.has(s.id) ? '-' : fmt(gpas[i])}</td>
            <td>${warnCount[s.id] ? `<span class="badge F">${warnCount[s.id]}회</span>` : '-'}</td>
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

// 학생 한 명의 성적표 HTML: 요약(학번·전체 GPA·누적 등록 학점·수강 건수) + 학기별 성적(학기 GPA·등록 학점) + 학사경고 + CSV 내려받기
// 성적표 창(관리자)과 내 성적 페이지(학생)가 같이 씀. s = {id, name}
// 학기 GPA 는 서버(/api/stats/gpa/:id?semester=)가 계산한 값을 그대로 쓰고, 등록 학점은 과목 학점(credit)의 합
// 재수강으로 지워진 성적(등급 R)은 GPA·학점에서 빠지므로 학점 합에서도 뺌
async function studentReportHtml(s) {
    const id = encodeURIComponent(s.id);
    const [grades, gpaAll, warnings] = await Promise.all([
        api('GET', '/api/grades?studentId=' + id),
        api('GET', '/api/stats/gpa/' + id).then(gpaOf).catch(() => null),
        api('GET', '/api/stats/warnings/' + id),
    ]);
    grades.sort((a, b) => b.semester.localeCompare(a.semester) || a.courseCode.localeCompare(b.courseCode));
    const creditOf = (code) => Number(state.courses.find((c) => c.code === code)?.credit) || 0;
    const semesters = sortSemesters(grades.map((g) => g.semester));
    const semGpas = await Promise.all(semesters.map((sem) =>
        api('GET', `/api/stats/gpa/${id}?semester=${encodeURIComponent(sem)}`).then(gpaOf).catch(() => null)));
    let totalCredits = 0;
    const semHtml = semesters.map((sem, i) => {
        const inSem = grades.filter((g) => g.semester === sem);
        const counted = inSem.filter((g) => g.letter !== 'R');
        const credits = counted.reduce((t, g) => t + creditOf(g.courseCode), 0);
        totalCredits += credits;
        const gpaText = counted.length ? fmt(semGpas[i]) : '-';
        const rows = inSem.map((g) => `
            <tr><td>${esc(g.courseCode)} ${esc(courseName(g.courseCode))}</td>
            <td class="num">${esc(creditOf(g.courseCode))}</td>
            <td class="num">${esc(g.score)}</td><td>${letterBadge(g.letter)}</td></tr>`).join('');
        return `
            <h3 class="sub-title">${esc(sem)} <span class="hint">학기 GPA ${gpaText} · 등록 학점 ${credits}</span></h3>
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
                ${grades.length ? stat('전체 GPA', fmt(gpaAll)) : stat('전체 GPA', '성적 없음', true)}
                ${stat('누적 등록 학점', totalCredits)}
                ${stat('수강 건수', grades.length)}
            </div>
            ${grades.length ? semHtml : '<p class="hint">등록된 성적이 없습니다</p>'}
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

async function renderMyReport() {
    $('myreport-body').innerHTML = await studentReportHtml({ id: state.me.studentId, name: state.me.name });
}

// ───────── 성적 ─────────

// 성적 폼·필터의 학생/과목 선택 항목 (교수는 담당 과목만, 학생 목록은 서버가 걸러 준 그대로)
const studentOptions = () => state.students.map((s) => ({ value: s.id, label: `${s.name} (${s.id})` }));
const courseOptions = () => gradeCourses().map((c) => ({ value: c.code, label: `${c.name} (${c.code})` }));

// 성적 표 그리기. 필터 선택 상자를 최신 목록으로 채우고, 고른 조건을 쿼리로 붙여 성적을 받아옴
// 수정/삭제 버튼에는 성적을 구분하는 키 [학번, 과목코드, 학기] 를 JSON 으로 담아 둠
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

    $('grade-body').innerHTML = grades.length ? grades.map((g) => {
        const key = esc(JSON.stringify([g.studentId, g.courseCode, g.semester]));
        return `
        <tr>
            <td>${esc(g.semester)}</td>
            <td>${esc(g.studentId)}</td>
            <td>${esc(studentName(g.studentId))}</td>
            <td>${esc(g.courseCode)} ${esc(courseName(g.courseCode))}</td>
            <td class="num">${esc(g.score)}</td>
            <td>${letterBadge(g.letter)}</td>
            <td class="actions">
                <button class="link" data-act="edit" data-k='${key}'>수정</button>
                <button class="link danger" data-act="del" data-k='${key}'>삭제</button>
            </td>
        </tr>`;
    }).join('') : emptyRow(7, '조건에 맞는 성적이 없습니다');
}

// 필터를 바꾸면 표를 다시 그림
['grade-f-student', 'grade-f-course', 'grade-f-semester'].forEach((id) =>
    ($(id).onchange = () => renderGrades().catch((e) => toast(e.message, true))));

// 성적 등록/수정 폼 항목. 수정할 때는 학생·과목·학기를 못 바꾸고 점수만 고침
// (서버가 PUT 본문의 학번·과목코드·학기로 대상을 찾기 때문). 등급은 서버가 점수로 정함
const gradeFields = (editing) => [
    { name: 'studentId', label: '학생', options: studentOptions(), readonly: editing },
    { name: 'courseCode', label: '과목', options: courseOptions(), readonly: editing },
    { name: 'semester', label: '학기', readonly: editing, placeholder: '예: 2026-1 (1·2 정규, 3 여름, 4 겨울)', pattern: '\\d{4}-[1-4]', hint: '학기는 YYYY-N 형식이어야 합니다 (N: 1~4)' },
    { name: 'score', label: '점수 (0~100, 등급은 자동: 95↑ A+ · 90↑ A0 · 85↑ B+ · … · 60↑ D0 · 그 아래 F)', type: 'number', step: 'any', min: 0, max: 100 },
];
// 성적 폼 값 검사: 잘못되면 이유, 괜찮으면 빈 문자열
const validateGrade = (d) => {
    if (!SEMESTER_RE.test((d.semester ?? '').trim())) return '학기는 YYYY-N 형식이어야 합니다 (N: 1~4, 예: 2026-1)';
    const score = Number(d.score);
    if (d.score === '' || !Number.isFinite(score) || score < 0 || score > 100) return '점수는 0~100 사이의 숫자여야 합니다';
    return '';
};

// 성적 등록 (수강신청한 학생·과목·학기만 가능). 고를 학생이나 과목이 없으면 안내만 띄움
$('grade-add').onclick = () => {
    if (!state.students.length || !courseOptions().length) {
        toast(isAdmin() ? '학생과 과목을 먼저 등록하세요' : '담당 과목 또는 수강생이 없습니다', true);
        return;
    }
    openForm({
        title: '성적 등록',
        fields: gradeFields(false),
        validate: validateGrade,
        onSubmit: async (d) => {
            await api('POST', '/api/grades', { ...d, score: Number(d.score) });
            toast(`성적을 등록했습니다: ${Number(d.score)}점`);
            await refreshAll();
        },
    });
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
            values: g,
            validate: validateGrade,
            onSubmit: async (d) => {
                await api('PUT', '/api/grades', { ...d, score: Number(d.score) });
                toast(`수정했습니다: ${Number(d.score)}점`);
                await refreshAll();
            },
        });
    }
    if (b.dataset.act === 'del') confirmDelete(
        `${studentName(studentId)} · ${courseName(courseCode)} · ${semester} 성적을 삭제할까요?`,
        () => api('DELETE', '/api/grades?' + q));
};

// ───────── 석차 ─────────

// 석차 표 그리기. 고른 학기(없으면 전체)의 석차를 받아 1~3등은 메달, GPA 는 4.5 만점 기준 막대로 표시
async function renderRanking() {
    const sel = $('rank-semester');
    fillSelect(sel, (await semesterList()).map((s) => ({ value: s, label: s })), '전체 학기', sel.value);
    const semQuery = sel.value ? '?semester=' + encodeURIComponent(sel.value) : '';
    // 고른 학기(없으면 전체)에 성적이 있는 학생: 성적이 없는 학생은 GPA 0.00 대신 '-' 로 표시
    const [rows, graded] = await Promise.all([
        api('GET', '/api/stats/ranking' + semQuery),
        api('GET', '/api/grades' + semQuery).catch(() => null),
    ]);
    const hasGrade = new Set((graded ?? []).map((g) => g.studentId));
    const medal = ['🥇', '🥈', '🥉'];
    $('rank-body').innerHTML = rows.length ? rows.map((r) => `
        <tr>
            <td>${medal[r.rank - 1] ?? ''} <b>${esc(r.rank)}</b></td>
            <td>${esc(r.studentId)}</td>
            <td><b>${esc(r.name)}</b></td>
            <td class="num">${graded && !hasGrade.has(r.studentId) ? '-' : fmt(r.gpa)}</td>
            <td class="bar-cell"><div class="bar"><i style="width:${Math.min(100, (r.gpa / 4.5) * 100)}%"></i></div></td>
        </tr>`).join('') : emptyRow(5, '표시할 학생이 없습니다');
}
// 학기를 바꾸면 석차를 다시 그림
$('rank-semester').onchange = () => renderRanking().catch((e) => toast(e.message, true));

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
