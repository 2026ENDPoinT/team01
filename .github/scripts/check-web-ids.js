#!/usr/bin/env node
// web/app.js 가 참조하는 정적 id 가 web/index.html 에 모두 있는지 검사
// (index.html 에서 id 를 지우거나 바꾸면 화면 전체가 TypeError 로 멎기 때문에 CI 에서 미리 잡음)
// 외부 패키지 없이 fs/정규식만 사용. 없는 id 가 있으면 id·줄 번호를 출력하고 exit 1
//
// 검사 대상
//   1) $('id'), $("id"), document.getElementById('id')   (따옴표 리터럴 인자만)
//   2) ['a', 'b'].forEach((x) => ... $(x) ...)             (id 문자열 배열을 돌며 $(x) 로 쓰는 곳)
// 건너뜀: 템플릿 리터럴(`...${}`)·변수 인자, 한 줄 주석(//) 줄

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', '..');
const HTML_PATH = path.join(ROOT, 'web', 'index.html');
const JS_PATH = path.join(ROOT, 'web', 'app.js');

// 예외 목록: index.html 에 없어도 통과시킬 id → 이유 (정적 분석으로 확인할 수 없는 것만 적는다)
// 현재 예외 없음. 예) 'grade-quick-add': 'app.js 가 버튼을 복제해 id 를 붙임 (index.html 에 없음)'
//   (grade-quick-add 는 `.id = ...` 대입이라 이 스크립트가 참조로 잡지 않으므로 실제로는 불필요)
const EXCEPTIONS = {};

// 문자열에서 offset 위치의 줄 번호 (1부터)
const lineOf = (text, offset) => text.slice(0, offset).split('\n').length;

const html = fs.readFileSync(HTML_PATH, 'utf8');
const rawJs = fs.readFileSync(JS_PATH, 'utf8');

// 한 줄 주석 줄은 같은 길이의 공백으로 바꿔 줄 번호·offset 을 유지한 채 제외
const js = rawJs.replace(/^[ \t]*\/\/.*$/gm, (m) => ' '.repeat(m.length));

// (1) index.html 의 id="..."
const htmlIds = new Set();
for (const m of html.matchAll(/\bid\s*=\s*["']([^"']+)["']/g)) htmlIds.add(m[1]);

// (2) app.js 의 id 참조: id → [줄 번호, ...]
const refs = new Map();
const addRef = (id, line) => {
    if (!refs.has(id)) refs.set(id, []);
    refs.get(id).push(line);
};

// $('x'), $("x"), getElementById('x')  ($( 앞이 식별자 문자면 다른 함수이므로 제외)
const direct = /(?<![\w$])\$\(\s*(['"])([^'"`$\\]+)\1\s*\)|\bgetElementById\(\s*(['"])([^'"`$\\]+)\3\s*\)/g;
for (const m of js.matchAll(direct)) addRef(m[2] ?? m[4], lineOf(js, m.index));

// (3) ['a', 'b', ...].forEach((x) => ... $(x) ...) : 배열 원소를 id 로 보고 검사
//     forEach 콜백 안(다음 200자 이내)에서 $(x) 로 쓰는 배열만 대상
const arrayLoop = /\[((?:\s*(['"])[^'"`$\\\n]+\2\s*,?)+\s*)\]\s*\.forEach\(\s*\(?\s*(\w+)\s*\)?\s*=>/g;
for (const m of js.matchAll(arrayLoop)) {
    const after = js.slice(m.index + m[0].length, m.index + m[0].length + 200);
    if (!new RegExp('(?<![\\w$])\\$\\(\\s*' + m[3] + '\\s*\\)').test(after)) continue;
    const bodyStart = m.index + m[0].indexOf('[') + 1;
    for (const s of m[1].matchAll(/(['"])([^'"`$\\\n]+)\1/g)) {
        addRef(s[2], lineOf(js, bodyStart + s.index));
    }
}

// 없는 id 보고
const missing = [...refs.keys()].filter((id) => !htmlIds.has(id) && !(id in EXCEPTIONS));
if (missing.length) {
    console.error('web/index.html 에 없는 id 를 web/app.js 가 참조합니다:');
    for (const id of missing) {
        const lines = [...new Set(refs.get(id))].join(', ');
        console.error(`  - ${id}  (web/app.js ${lines}번째 줄)`);
    }
    process.exit(1);
}
console.log(`OK: web/app.js 의 id 참조 ${refs.size}개가 모두 web/index.html 에 있습니다`);
