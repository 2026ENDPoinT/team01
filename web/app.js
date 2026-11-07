// 담당: C (원준)
// 서버 API 를 호출해서 화면에 표시

async function loadStudents() {
    const res = await fetch('/api/students');
    const students = await res.json();

    const list = document.getElementById('student-list');
    list.innerHTML = '';
    for (const s of students) {
        const li = document.createElement('li');
        li.textContent = `${s.id} ${s.name} (${s.major} ${s.year}학년)`;
        list.appendChild(li);
    }
    if (students.length === 0) {
        list.innerHTML = '<li>등록된 학생이 없습니다</li>';
    }
}

loadStudents();
