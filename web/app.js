// 담당: C (원준)
// 서버 API 를 호출해서 화면에 표시 (프레임워크 없이 순수 JS)

// id 로 요소 찾기 (document.getElementById 줄임)
const $ = (id) => document.getElementById(id);
// 여러 탭이 같이 쓰는 학생·과목 목록 (loadBase 로 채움, 이름 표시·선택 상자에 사용)
const state = { students: [], courses: [] };

// ───────── 공용 도우미 ─────────

// HTML 에 끼워 넣을 값의 특수문자를 바꿔서 이름 등에 든 태그가 실행되지 않게 함
function esc(v) {
    return String(v ?? '').replace(/[&<>"']/g, (c) =>
        ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// 서버 API 호출. body 가 있으면 JSON 으로 보내고, 응답 JSON 을 돌려줌
// 실패(4xx/5xx)하면 서버가 준 {"error"} 메시지로 Error 를 던짐 → 호출한 쪽에서 토스트/폼에 표시
async function api(method, url, body) {
    const opt = { method, headers: {} };
    if (body !== undefined) {
        opt.headers['Content-Type'] = 'application/json';
        opt.body = JSON.stringify(body);
    }
    const res = await fetch(url, opt);
    const data = await res.json().catch(() => ({}));
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

// ───────── 대화상자 ─────────

// 등록·수정 폼과 성적표 창이 같이 쓰는 <dialog> 하나
// dlgSubmit: 저장 버튼을 눌렀을 때 실행할 함수 (없으면 저장 버튼을 숨김 = 보기 전용 창)
const dlg = $('dlg');
let dlgSubmit = null;

// 제목·내용을 채우고 창을 띄움
function openDialog({ title, bodyHtml, onSubmit, okText = '저장' }) {
    $('dlg-title').textContent = title;
    $('dlg-body').innerHTML = bodyHtml;
    $('dlg-ok').textContent = okText;
    $('dlg-ok').style.display = onSubmit ? '' : 'none';
    dlgSubmit = onSubmit;
    dlg.showModal();
}

$('dlg-cancel').onclick = () => dlg.close();
// 저장: 폼 값을 객체로 모아 dlgSubmit 에 넘김. 성공하면 창을 닫고, 실패하면 창 안에 오류 메시지 표시
$('dlg-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    if (!dlgSubmit) return;
    const data = Object.fromEntries(new FormData($('dlg-form')));
    try {
        await dlgSubmit(data);
        dlg.close();
    } catch (err) {
        const box = $('dlg-body').querySelector('.form-error');
        if (box) box.textContent = err.message;
    }
});

// 입력 폼 창 만들기
//   fields: [{name, label, type, readonly, optional, step, placeholder, options:[{value,label}]}]
//     options 가 있으면 선택 상자, 없으면 입력칸. readonly 는 수정할 때 키(학번 등)를 못 바꾸게 할 때 사용
//     optional 이면 빈칸으로 저장할 수 있음 (그 외 입력칸은 필수)
//   values: 처음 채워둘 값, onSubmit: 저장할 때 실행할 함수 (폼 값 객체를 받음)
function openForm({ title, fields, values = {}, onSubmit }) {
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
        } else {
            input = `<input name="${f.name}" type="${f.type || 'text'}" value="${v}" ${f.step ? `step="${f.step}"` : ''}
                ${f.readonly ? 'readonly' : ''} ${f.placeholder ? `placeholder="${esc(f.placeholder)}"` : ''} ${f.optional ? '' : 'required'}>`;
        }
        return `<div class="field"><label>${f.label}</label>${input}</div>`;
    }).join('') + '<div class="form-error"></div>';
    openDialog({ title, bodyHtml: html, onSubmit });
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

// 탭 버튼·화면을 바꾸고 그 탭을 다시 그림 (탭을 열 때마다 서버에서 최신 값을 받음)
function showTab(name) {
    document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.id === 'tab-' + name));
    renderers[name]();
}
$('tabs').onclick = (e) => { if (e.target.dataset.tab) showTab(e.target.dataset.tab); };

// 탭 이름 → 그 탭을 그리는 함수
const renderers = {
    students: renderStudents,
    courses: renderCourses,
    grades: renderGrades,
    ranking: renderRanking,
};
// 지금 열린 탭 이름
const currentTab = () => document.querySelector('#tabs button.active').dataset.tab;

// 학생·과목 목록을 서버에서 받아 state 에 저장
async function loadBase() {
    [state.students, state.courses] = await Promise.all([api('GET', '/api/students'), api('GET', '/api/courses')]);
}
// 등록·수정·삭제 뒤에 호출: 목록을 다시 받고 지금 탭을 다시 그림
async function refreshAll() {
    await loadBase();
    await renderers[currentTab()]();
}

// 성적에 나온 학기 목록 (최신 학기가 위로). 학기 선택 상자에 사용
async function semesterList() {
    const grades = await api('GET', '/api/grades');
    return [...new Set(grades.map((g) => g.semester))].sort().reverse();
}

// 선택 상자 채우기. allLabel 이 있으면 맨 위에 "전체" 항목(값 '')을 넣고,
// keep(이전 선택값)이 새 목록에도 있으면 그대로 선택해 둠
function fillSelect(el, items, allLabel, keep) {
    el.innerHTML = (allLabel ? `<option value="">${allLabel}</option>` : '') +
        items.map((i) => `<option value="${esc(i.value)}">${esc(i.label)}</option>`).join('');
    if (keep && [...el.options].some((o) => o.value === keep)) el.value = keep;
}

// ───────── 학생 ─────────

// 학생 등록/수정 폼 항목. 수정할 때는 학번을 못 바꿈 (서버가 URL 의 학번으로 찾음)
const studentFields = (editing) => [
    { name: 'id', label: '학번', readonly: editing, placeholder: '예: 24101001' },
    { name: 'name', label: '이름' },
    { name: 'major', label: '학과' },
    { name: 'year', label: '학년', type: 'number' },
];

// 학생 표 그리기. 검색어가 있으면 서버 검색 API 결과를, 없으면 전체 목록을 보여줌
// 학생마다 GPA API 를 한 번씩 불러서 GPA 열을 채움
async function renderStudents() {
    const q = $('student-search').value.trim();
    let list = state.students;
    if (q) {
        try { list = await api('GET', '/api/students/search?q=' + encodeURIComponent(q)); }
        catch (e) { toast(e.message, true); }
    }
    const body = $('student-body');
    if (!list.length) { body.innerHTML = emptyRow(6, q ? '검색 결과가 없습니다' : '등록된 학생이 없습니다'); return; }

    const gpas = await Promise.all(list.map((s) =>
        api('GET', '/api/stats/gpa/' + encodeURIComponent(s.id)).then(gpaOf).catch(() => null)));
    body.innerHTML = list.map((s, i) => `
        <tr>
            <td>${esc(s.id)}</td>
            <td><b>${esc(s.name)}</b></td>
            <td>${esc(s.major)}</td>
            <td>${esc(s.year)}학년</td>
            <td class="num">${fmt(gpas[i])}</td>
            <td>
                <button class="link" data-act="report" data-id="${esc(s.id)}">성적표</button>
                <button class="link" data-act="edit" data-id="${esc(s.id)}">수정</button>
                <button class="link danger" data-act="del" data-id="${esc(s.id)}">삭제</button>
            </td>
        </tr>`).join('');
}

// 검색칸에 입력할 때마다 표를 다시 그림
$('student-search').addEventListener('input', renderStudents);
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
    if (b.dataset.act === 'report') showStudentReport(s);
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
        `${s.name}(${s.id}) 학생을 삭제할까요?\n이 학생의 성적도 함께 삭제됩니다.`,
        () => api('DELETE', '/api/students/' + encodeURIComponent(s.id)));
};

// 학생 한 명의 성적표 창: 요약(학번·GPA·수강 건수) + 학기별 성적 + CSV 내려받기 (보기 전용)
async function showStudentReport(s) {
    const [grades, gpaAll] = await Promise.all([
        api('GET', '/api/grades?studentId=' + encodeURIComponent(s.id)),
        api('GET', '/api/stats/gpa/' + encodeURIComponent(s.id)).then(gpaOf).catch(() => null),
    ]);
    grades.sort((a, b) => b.semester.localeCompare(a.semester) || a.courseCode.localeCompare(b.courseCode));
    const rows = grades.length ? grades.map((g) => `
        <tr><td>${esc(g.semester)}</td><td>${esc(g.courseCode)} ${esc(courseName(g.courseCode))}</td>
        <td class="num">${esc(g.score)}</td><td>${letterBadge(g.letter)}</td></tr>`).join('')
        : emptyRow(4, '성적이 없습니다');
    openDialog({
        title: `${s.name} 성적표`,
        okText: '',
        bodyHtml: `
            <div class="summary">
                <div><span>학번</span><b style="font-size:16px">${esc(s.id)}</b></div>
                <div><span>전체 GPA</span><b>${fmt(gpaAll)}</b></div>
                <div><span>수강 건수</span><b>${grades.length}</b></div>
            </div>
            <table><thead><tr><th>학기</th><th>과목</th><th class="num">점수</th><th>등급</th></tr></thead><tbody>${rows}</tbody></table>
            <div class="dlg-actions"><a href="/api/export/student/${encodeURIComponent(s.id)}"><button type="button" class="primary">CSV 내려받기</button></a></div>`,
    });
}

// ───────── 과목 ─────────

// 과목 등록/수정 폼 항목. 과목코드는 숫자 6자리 (예: 109004)
//   수정 때 과목코드를 바꾸면 서버가 그 과목의 성적에 적힌 코드도 같이 바꿈 (GradeManager::updateCourse)
//   이수구분·강의언어 목록은 서버(course.cpp)의 courseCategories / courseLanguages 와 같아야 함
const COURSE_CATEGORIES = ['전공필수', '전공선택', '교양필수', '교양선택', '일반선택'];
const COURSE_LANGUAGES = ['한국어전용', '영어전용', '모듈강좌(M)'];
const asOptions = (list) => list.map((v) => ({ value: v, label: v }));
const courseFields = () => [
    { name: 'code', label: '과목 코드 (숫자 6자리)', placeholder: '예: 109004' },
    { name: 'name', label: '과목명' },
    { name: 'category', label: '이수구분', options: asOptions(COURSE_CATEGORIES) },
    { name: 'credit', label: '학점 (1~4)', type: 'number' },
    { name: 'professor', label: '담당 교수', optional: true },
    { name: 'schedule', label: '강의시간', optional: true, placeholder: '예: 월(1~4) 또는 화(2~3) 목(2~3)' },
    { name: 'language', label: '강의언어', options: asOptions(COURSE_LANGUAGES) },
    { name: 'area', label: '교양영역 (교양 과목만)', optional: true, placeholder: '예: 인문, 자연과학' },
    { name: 'capacity', label: '학기당 정원 (0 = 제한 없음)', type: 'number' },
    { name: 'remote', label: '원격강좌', options: [{ value: 'N', label: '아니오' }, { value: 'Y', label: '예' }] },
];
// 폼 값 → API 본문 (숫자·참거짓 변환)
const courseBody = (d) => ({ ...d, credit: Number(d.credit), capacity: Number(d.capacity), remote: d.remote === 'Y' });
// API 값 → 폼 처음 값 (remote 는 선택 상자 값 Y/N 으로)
const courseFormValues = (c) => ({ ...c, remote: c.remote ? 'Y' : 'N' });

// 과목 표 그리기. 과목마다 통계 API 를 불러 수강 인원(전체 학기)·평균·최고/최저를 채움 (성적이 없으면 '-')
async function renderCourses() {
    const body = $('course-body');
    if (!state.courses.length) { body.innerHTML = emptyRow(11, '등록된 과목이 없습니다'); return; }
    const stats = await Promise.all(state.courses.map((c) =>
        api('GET', '/api/stats/course/' + encodeURIComponent(c.code)).catch(() => null)));
    body.innerHTML = state.courses.map((c, i) => {
        const st = stats[i];
        const has = st && st.count > 0;
        return `<tr>
            <td>${esc(c.code)}</td>
            <td><b>${esc(c.name)}</b>${c.area ? `<span class="sub">${esc(c.area)}</span>` : ''}</td>
            <td>${esc(c.category)}</td>
            <td class="num">${esc(c.credit)}</td>
            <td>${esc(c.professor)}</td>
            <td>${esc(c.schedule) || '-'}${c.remote ? ' <span class="tag">원격</span>' : ''}</td>
            <td>${esc(c.language)}</td>
            <td class="num">${c.capacity > 0 ? c.capacity + '명' : '제한 없음'}</td>
            <td class="num">${has ? st.count + '명' : '-'}</td>
            <td class="num">${has ? `${fmt(st.average, 1)} <span class="sub">${fmt(st.max, 0)} / ${fmt(st.min, 0)}</span>` : '-'}</td>
            <td>
                <a href="/api/export/course/${encodeURIComponent(c.code)}"><button class="link">CSV</button></a>
                <button class="link" data-act="edit" data-code="${esc(c.code)}">수정</button>
                <button class="link danger" data-act="del" data-code="${esc(c.code)}">삭제</button>
            </td></tr>`;
    }).join('');
}

$('course-add').onclick = () => openForm({
    title: '과목 등록',
    fields: courseFields(),
    values: { category: '전공선택', credit: 3, language: '한국어전용', capacity: 0, remote: 'N' },
    onSubmit: async (d) => {
        await api('POST', '/api/courses', courseBody(d));
        toast('과목을 등록했습니다');
        await refreshAll();
    },
});
// 과목 표 안 버튼(수정/삭제) 처리. CSV 는 링크라서 여기서 처리하지 않음
$('course-body').onclick = (e) => {
    const b = e.target.closest('button[data-act]');
    if (!b) return;
    const c = state.courses.find((x) => x.code === b.dataset.code);
    if (b.dataset.act === 'edit') openForm({
        title: '과목 수정',
        fields: courseFields(),
        values: courseFormValues(c),
        onSubmit: async (d) => {
            await api('PUT', '/api/courses/' + encodeURIComponent(c.code), courseBody(d));
            toast('수정했습니다');
            await refreshAll();
        },
    });
    if (b.dataset.act === 'del') confirmDelete(
        `${c.name}(${c.code}) 과목을 삭제할까요?\n이 과목의 성적도 함께 삭제됩니다.`,
        () => api('DELETE', '/api/courses/' + encodeURIComponent(c.code)));
};

// ───────── 성적 ─────────

// 성적 폼·필터의 학생/과목 선택 항목
const studentOptions = () => state.students.map((s) => ({ value: s.id, label: `${s.name} (${s.id})` }));
const courseOptions = () => state.courses.map((c) => ({ value: c.code, label: `${c.name} (${c.code})` }));

// 성적 표 그리기. 필터 선택 상자를 최신 목록으로 채우고, 고른 조건을 쿼리로 붙여 성적을 받아옴
// 수정/삭제 버튼에는 성적을 구분하는 키 [학번, 과목코드, 학기] 를 JSON 으로 담아 둠
async function renderGrades() {
    const fs = $('grade-f-student'), fc = $('grade-f-course'), fm = $('grade-f-semester');
    const semesters = await semesterList();
    fillSelect(fs, studentOptions(), '전체 학생', fs.value);
    fillSelect(fc, courseOptions(), '전체 과목', fc.value);
    fillSelect(fm, semesters.map((s) => ({ value: s, label: s })), '전체 학기', fm.value);

    const params = new URLSearchParams();
    if (fs.value) params.set('studentId', fs.value);
    if (fc.value) params.set('courseCode', fc.value);
    if (fm.value) params.set('semester', fm.value);
    const grades = await api('GET', '/api/grades' + (params.size ? '?' + params : ''));
    grades.sort((a, b) => b.semester.localeCompare(a.semester) || a.studentId.localeCompare(b.studentId));

    $('grade-body').innerHTML = grades.length ? grades.map((g) => `
        <tr>
            <td>${esc(g.semester)}</td>
            <td>${esc(g.studentId)}</td>
            <td>${esc(studentName(g.studentId))}</td>
            <td>${esc(g.courseCode)} ${esc(courseName(g.courseCode))}</td>
            <td class="num">${esc(g.score)}</td>
            <td>${letterBadge(g.letter)}</td>
            <td>
                <button class="link" data-act="edit" data-k='${esc(JSON.stringify([g.studentId, g.courseCode, g.semester]))}'>수정</button>
                <button class="link danger" data-act="del" data-k='${esc(JSON.stringify([g.studentId, g.courseCode, g.semester]))}'>삭제</button>
            </td>
        </tr>`).join('') : emptyRow(7, '조건에 맞는 성적이 없습니다');
}

// 필터를 바꾸면 표를 다시 그림
['grade-f-student', 'grade-f-course', 'grade-f-semester'].forEach((id) => ($(id).onchange = renderGrades));

// 성적 등록/수정 폼 항목. 수정할 때는 학생·과목·학기를 못 바꾸고 점수만 고침
// (서버가 PUT 본문의 학번·과목코드·학기로 대상을 찾기 때문). 등급은 서버가 점수로 정함
const gradeFields = (editing) => [
    { name: 'studentId', label: '학생', options: studentOptions(), readonly: editing },
    { name: 'courseCode', label: '과목', options: courseOptions(), readonly: editing },
    { name: 'semester', label: '학기', readonly: editing, placeholder: '예: 2026-1' },
    { name: 'score', label: '점수 (0~100, 등급은 자동: 95↑ A+ · 90↑ A0 · 85↑ B+ · … · 60↑ D0 · 그 아래 F)', type: 'number', step: 'any' },
];

// 성적 등록. 학생과 과목이 하나도 없으면 등록할 수 없으므로 안내만 띄움
$('grade-add').onclick = () => {
    if (!state.students.length || !state.courses.length) { toast('학생과 과목을 먼저 등록하세요', true); return; }
    openForm({
        title: '성적 등록',
        fields: gradeFields(false),
        values: { semester: '2026-1' },
        onSubmit: async (d) => {
            const g = await api('POST', '/api/grades', { ...d, score: Number(d.score) });
            toast(`성적을 등록했습니다: ${g.score}점 → ${g.letter}`);
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
        const [g] = await api('GET', '/api/grades?' + q);
        openForm({
            title: '성적 수정',
            fields: gradeFields(true),
            values: g,
            onSubmit: async (d) => {
                const updated = await api('PUT', '/api/grades', { ...d, score: Number(d.score) });
                toast(`수정했습니다: ${updated.score}점 → ${updated.letter}`);
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
    const rows = await api('GET', '/api/stats/ranking' + (sel.value ? '?semester=' + encodeURIComponent(sel.value) : ''));
    const medal = ['🥇', '🥈', '🥉'];
    $('rank-body').innerHTML = rows.length ? rows.map((r) => `
        <tr>
            <td>${medal[r.rank - 1] ?? ''} <b>${r.rank}</b></td>
            <td>${esc(r.studentId)}</td>
            <td><b>${esc(r.name)}</b></td>
            <td class="num">${fmt(r.gpa)}</td>
            <td style="width:30%"><div class="bar"><i style="width:${Math.min(100, (r.gpa / 4.5) * 100)}%"></i></div></td>
        </tr>`).join('') : emptyRow(5, '표시할 학생이 없습니다');
}
// 학기를 바꾸면 석차를 다시 그림
$('rank-semester').onchange = renderRanking;

// ───────── 시작 ─────────

// 처음 열면 학생·과목을 받아오고 학생 탭을 그림
loadBase().then(renderStudents).catch((e) => toast(e.message, true));
