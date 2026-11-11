// 담당: C (원준)
// 서버 API 를 호출해서 화면에 표시 (프레임워크 없이 순수 JS)

const $ = (id) => document.getElementById(id);
const state = { students: [], courses: [] };

// ───────── 공용 도우미 ─────────

function esc(v) {
    return String(v ?? '').replace(/[&<>"']/g, (c) =>
        ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

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

let toastTimer;
function toast(msg, isError = false) {
    const el = $('toast');
    el.textContent = msg;
    el.className = 'show' + (isError ? ' error' : '');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (el.className = ''), 2500);
}

const fmt = (n, d = 2) => (typeof n === 'number' ? n.toFixed(d) : '-');
const gpaOf = (r) => (typeof r === 'number' ? r : r?.gpa);
const letterBadge = (l) => l ? `<span class="badge ${esc(l[0])}">${esc(l)}</span>` : '-';
const studentName = (id) => state.students.find((s) => s.id === id)?.name ?? '';
const courseName = (code) => state.courses.find((c) => c.code === code)?.name ?? '';
const emptyRow = (cols, msg) => `<tr><td class="empty" colspan="${cols}">${msg}</td></tr>`;

// ───────── 대화상자 ─────────

const dlg = $('dlg');
let dlgSubmit = null;

function openDialog({ title, bodyHtml, onSubmit, okText = '저장' }) {
    $('dlg-title').textContent = title;
    $('dlg-body').innerHTML = bodyHtml;
    $('dlg-ok').textContent = okText;
    $('dlg-ok').style.display = onSubmit ? '' : 'none';
    dlgSubmit = onSubmit;
    dlg.showModal();
}

$('dlg-cancel').onclick = () => dlg.close();
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

// fields: [{name, label, type, readonly, options:[{value,label}]}]
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
                ${f.readonly ? 'readonly' : ''} ${f.placeholder ? `placeholder="${esc(f.placeholder)}"` : ''} required>`;
        }
        return `<div class="field"><label>${f.label}</label>${input}</div>`;
    }).join('') + '<div class="form-error"></div>';
    openDialog({ title, bodyHtml: html, onSubmit });
    const first = $('dlg-body').querySelector('input:not([readonly]):not([type=hidden])');
    if (first) first.focus();
}

async function confirmDelete(msg, fn) {
    if (!confirm(msg)) return;
    try {
        await fn();
        toast('삭제했습니다');
        await refreshAll();
    } catch (e) { toast(e.message, true); }
}

// ───────── 탭 ─────────

function showTab(name) {
    document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.id === 'tab-' + name));
    renderers[name]();
}
$('tabs').onclick = (e) => { if (e.target.dataset.tab) showTab(e.target.dataset.tab); };

const renderers = {
    students: renderStudents,
    courses: renderCourses,
    grades: renderGrades,
    ranking: renderRanking,
};
const currentTab = () => document.querySelector('#tabs button.active').dataset.tab;

async function loadBase() {
    [state.students, state.courses] = await Promise.all([api('GET', '/api/students'), api('GET', '/api/courses')]);
}
async function refreshAll() {
    await loadBase();
    await renderers[currentTab()]();
}

async function semesterList() {
    const grades = await api('GET', '/api/grades');
    return [...new Set(grades.map((g) => g.semester))].sort().reverse();
}

function fillSelect(el, items, allLabel, keep) {
    el.innerHTML = (allLabel ? `<option value="">${allLabel}</option>` : '') +
        items.map((i) => `<option value="${esc(i.value)}">${esc(i.label)}</option>`).join('');
    if (keep && [...el.options].some((o) => o.value === keep)) el.value = keep;
}

// ───────── 학생 ─────────

const studentFields = (editing) => [
    { name: 'id', label: '학번', readonly: editing, placeholder: '예: 24101001' },
    { name: 'name', label: '이름' },
    { name: 'major', label: '학과' },
    { name: 'year', label: '학년', type: 'number' },
];

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

const courseFields = (editing) => [
    { name: 'code', label: '과목 코드', readonly: editing, placeholder: '예: CS101' },
    { name: 'name', label: '과목명' },
    { name: 'credit', label: '학점', type: 'number' },
    { name: 'professor', label: '담당 교수' },
];

async function renderCourses() {
    const body = $('course-body');
    if (!state.courses.length) { body.innerHTML = emptyRow(8, '등록된 과목이 없습니다'); return; }
    const stats = await Promise.all(state.courses.map((c) =>
        api('GET', '/api/stats/course/' + encodeURIComponent(c.code)).catch(() => null)));
    body.innerHTML = state.courses.map((c, i) => {
        const st = stats[i];
        const has = st && st.count > 0;
        return `<tr>
            <td>${esc(c.code)}</td>
            <td><b>${esc(c.name)}</b></td>
            <td class="num">${esc(c.credit)}</td>
            <td>${esc(c.professor)}</td>
            <td class="num">${has ? st.count + '명' : '-'}</td>
            <td class="num">${has ? fmt(st.average, 1) : '-'}</td>
            <td class="num">${has ? `${fmt(st.max, 0)} / ${fmt(st.min, 0)}` : '-'}</td>
            <td>
                <a href="/api/export/course/${encodeURIComponent(c.code)}"><button class="link">CSV</button></a>
                <button class="link" data-act="edit" data-code="${esc(c.code)}">수정</button>
                <button class="link danger" data-act="del" data-code="${esc(c.code)}">삭제</button>
            </td></tr>`;
    }).join('');
}

$('course-add').onclick = () => openForm({
    title: '과목 등록',
    fields: courseFields(false),
    onSubmit: async (d) => {
        await api('POST', '/api/courses', { ...d, credit: Number(d.credit) });
        toast('과목을 등록했습니다');
        await refreshAll();
    },
});
$('course-body').onclick = (e) => {
    const b = e.target.closest('button[data-act]');
    if (!b) return;
    const c = state.courses.find((x) => x.code === b.dataset.code);
    if (b.dataset.act === 'edit') openForm({
        title: '과목 수정',
        fields: courseFields(true),
        values: c,
        onSubmit: async (d) => {
            await api('PUT', '/api/courses/' + encodeURIComponent(c.code), { ...d, credit: Number(d.credit) });
            toast('수정했습니다');
            await refreshAll();
        },
    });
    if (b.dataset.act === 'del') confirmDelete(
        `${c.name}(${c.code}) 과목을 삭제할까요?\n이 과목의 성적도 함께 삭제됩니다.`,
        () => api('DELETE', '/api/courses/' + encodeURIComponent(c.code)));
};

// ───────── 성적 ─────────

const studentOptions = () => state.students.map((s) => ({ value: s.id, label: `${s.name} (${s.id})` }));
const courseOptions = () => state.courses.map((c) => ({ value: c.code, label: `${c.name} (${c.code})` }));

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

['grade-f-student', 'grade-f-course', 'grade-f-semester'].forEach((id) => ($(id).onchange = renderGrades));

const gradeFields = (editing) => [
    { name: 'studentId', label: '학생', options: studentOptions(), readonly: editing },
    { name: 'courseCode', label: '과목', options: courseOptions(), readonly: editing },
    { name: 'semester', label: '학기', readonly: editing, placeholder: '예: 2026-1' },
    { name: 'score', label: '점수 (0~100)', type: 'number', step: 'any' },
];

$('grade-add').onclick = () => {
    if (!state.students.length || !state.courses.length) { toast('학생과 과목을 먼저 등록하세요', true); return; }
    openForm({
        title: '성적 등록',
        fields: gradeFields(false),
        values: { semester: '2026-1' },
        onSubmit: async (d) => {
            await api('POST', '/api/grades', { ...d, score: Number(d.score) });
            toast('성적을 등록했습니다');
            await refreshAll();
        },
    });
};
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
                await api('PUT', '/api/grades', { ...d, score: Number(d.score) });
                toast('수정했습니다');
                await refreshAll();
            },
        });
    }
    if (b.dataset.act === 'del') confirmDelete(
        `${studentName(studentId)} · ${courseName(courseCode)} · ${semester} 성적을 삭제할까요?`,
        () => api('DELETE', '/api/grades?' + q));
};

// ───────── 석차 ─────────

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
$('rank-semester').onchange = renderRanking;

// ───────── 시작 ─────────

loadBase().then(renderStudents).catch((e) => toast(e.message, true));
