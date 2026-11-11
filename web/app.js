// 담당: C (원준)
// 서버 API 를 호출해서 화면에 표시 (프레임워크 없이 순수 JS)

// id 로 요소 찾기 (document.getElementById 줄임)
const $ = (id) => document.getElementById(id);
// 과목 목록과 과목별 통계 (loadCourses 로 채움, 같은 순서로 대응)
const state = { courses: [], stats: [] };

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

// ───────── 대화상자 ─────────

// 등록·수정 폼과 과목 상세 창이 같이 쓰는 <dialog> 하나
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

// ───────── 과목 ─────────

// 과목 목록을 서버에서 받아 state 에 저장 (과목마다 통계 API 도 같이 호출, 못 받으면 null)
async function loadCourses() {
    state.courses = await api('GET', '/api/courses');
    state.stats = await Promise.all(state.courses.map((c) =>
        api('GET', '/api/stats/course/' + encodeURIComponent(c.code)).catch(() => null)));
}
// 등록·수정·삭제 뒤에 호출: 목록을 다시 받고 다시 그림
async function refreshAll() {
    await loadCourses();
    renderCourses();
}

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

// 과목 목록 그리기: 한 줄에 과목 정보(왼쪽)와 수강 인원·평균(오른쪽). 성적이 없으면 평균은 '-'
function renderCourses() {
    const list = $('course-list');
    if (!state.courses.length) { list.innerHTML = '<li class="empty">등록된 과목이 없습니다</li>'; return; }
    list.innerHTML = state.courses.map((c, i) => {
        const st = state.stats[i];
        const has = st && st.count > 0;
        return `<li class="course-row" data-code="${esc(c.code)}">
            <div class="course-info">
                <div class="course-name">${esc(c.name)}${c.area ? ` <span class="tag">${esc(c.area)}</span>` : ''}</div>
                <div class="course-line">${esc(c.code)} · ${esc(c.professor) || '-'} · ${esc(c.category)}</div>
                <div class="course-line">${esc(c.schedule) || '-'} · ${esc(c.credit)}학점 · ${esc(c.language)}${c.remote ? ' <span class="tag">원격</span>' : ''}</div>
            </div>
            <div class="course-meta">
                <span class="stat-line">수강 <b>${st ? esc(st.count) : 0}</b> / ${c.capacity > 0 ? esc(c.capacity) : '제한 없음'}</span>
                <span class="stat-line">${has ? `평균 <b>${fmt(st.average, 1)}</b> (${fmt(st.max, 0)} / ${fmt(st.min, 0)})` : '평균 -'}</span>
            </div>
        </li>`;
    }).join('');
}

// 과목 상세 창 (보기 전용): 전체 항목 + 통계 상자 + CSV / 수정 / 삭제 버튼
function showCourseDetail(c, st) {
    const has = st && st.count > 0;
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
    const stat = (label, v) => `<div><span>${label}</span><b>${v}</b></div>`;
    openDialog({
        title: `${c.name} (${c.code})`,
        bodyHtml: `<dl class="detail-grid">${rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('')}</dl>
            <div class="stat-boxes">
                ${stat('수강 인원', has ? esc(st.count) + '명' : '-')}
                ${stat('평균', has ? fmt(st.average, 1) : '-')}
                ${stat('최고', has ? fmt(st.max, 0) : '-')}
                ${stat('최저', has ? fmt(st.min, 0) : '-')}
            </div>
            <div class="detail-actions">
                <a class="btn-link" href="/api/export/course/${encodeURIComponent(c.code)}">CSV 내려받기</a>
                <button type="button" data-act="edit">수정</button>
                <button type="button" class="danger" data-act="del">삭제</button>
            </div>`,
    });
    $('dlg-body').querySelector('[data-act=edit]').onclick = () => { dlg.close(); editCourse(c); };
    $('dlg-body').querySelector('[data-act=del]').onclick = () => { dlg.close(); deleteCourse(c); };
}

// 과목 수정 폼
function editCourse(c) {
    openForm({
        title: '과목 수정',
        fields: courseFields(),
        values: courseFormValues(c),
        onSubmit: async (d) => {
            await api('PUT', '/api/courses/' + encodeURIComponent(c.code), courseBody(d));
            toast('수정했습니다');
            await refreshAll();
        },
    });
}

// 과목 삭제 (확인 후)
function deleteCourse(c) {
    confirmDelete(
        `${c.name}(${c.code}) 과목을 삭제할까요?\n이 과목의 성적도 함께 삭제됩니다.`,
        () => api('DELETE', '/api/courses/' + encodeURIComponent(c.code)));
}

// 행을 누르면 그 과목의 상세 창
$('course-list').onclick = (e) => {
    const row = e.target.closest('.course-row');
    if (!row) return;
    const i = state.courses.findIndex((x) => x.code === row.dataset.code);
    if (i >= 0) showCourseDetail(state.courses[i], state.stats[i]);
};

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

// ───────── 시작 ─────────

// 처음 열면 과목을 받아 목록을 그림
refreshAll().catch((e) => toast(e.message, true));
