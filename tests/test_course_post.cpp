// 담당: 조장 (Eno74)
// 과목 게시물: 값 검사, 등록/수정/삭제, 과목 삭제·코드 변경 연쇄 처리, 저장 → 불러오기(본문 줄바꿈)
#include <string>

#include "check.h"
#include "course_post.h"
#include "grade_manager.h"
#include "test_support.h"

namespace {

CoursePost basePost() { return makePost("100001", "2026-1", "material", 1, "수업자료"); }

size_t countByCourse(const GradeManager& gm, const std::string& courseCode) {
    size_t count = 0;
    for (const CoursePost& p : gm.getPosts()) {
        if (p.courseCode == courseCode) count++;
    }
    return count;
}

}  // namespace

TEST_CASE(post_isValid_accepts_default_and_good_optional_values) {
    CHECK(basePost().isValid());

    CoursePost p = basePost();
    p.link = "https://a.b";
    CHECK(p.isValid());

    p = basePost();
    p.dueDate = "2026-10-20";
    CHECK(p.isValid());
    p.dueDate = "2026-10-20 13:00";
    CHECK(p.isValid());
}

TEST_CASE(post_isValid_rejects_bad_values) {
    CoursePost p = basePost();
    p.week = 0;
    CHECK(!p.isValid());
    p.week = 17;
    CHECK(!p.isValid());

    p = basePost();
    p.category = "exam";
    CHECK(!p.isValid());

    p = basePost();
    p.title = "  ";
    CHECK(!p.isValid());
    p.title = "첫 줄\n둘째 줄";
    CHECK(!p.isValid());

    p = basePost();
    p.link = "javascript:alert(1)";
    CHECK(!p.isValid());
    p.link = "https://a b";
    CHECK(!p.isValid());

    p = basePost();
    p.dueDate = "2026-13-01";
    CHECK(!p.isValid());
    p.dueDate = "2026-10-20 24:00";
    CHECK(!p.isValid());
    p.dueDate = "20261020";
    CHECK(!p.isValid());

    p = basePost();
    p.semester = "2026-5";
    CHECK(!p.isValid());

    p = basePost();
    p.courseCode = "10000";
    CHECK(!p.isValid());
}

TEST_CASE(addPost_assigns_ids_and_rejects_unknown_course_or_bad_value) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addCourse(makeCourse("100001", "과목1")));

    CHECK_EQ(gm.addPost(makePost("100001", "2026-1", "material", 1, "첫째")), 1);
    CHECK_EQ(gm.addPost(makePost("100001", "2026-1", "final", 15, "둘째")), 2);
    CHECK_EQ(gm.addPost(makePost("999999", "2026-1", "material", 1, "없는 과목")), 0);
    CHECK_EQ(gm.addPost(makePost("100001", "2026-1", "material", 17, "잘못된 주차")), 0);
    CHECK_EQ(gm.getPosts().size(), static_cast<size_t>(2));
}

TEST_CASE(updatePost_and_removePost) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addCourse(makeCourse("100001", "과목1")));
    const int id = gm.addPost(makePost("100001", "2026-1", "material", 1, "처음 제목"));
    CHECK_EQ(id, 1);

    CoursePost edited = *gm.findPost(id);
    edited.title = "바뀐 제목";
    CHECK(gm.updatePost(edited));
    CHECK_EQ(gm.findPost(id)->title, std::string("바뀐 제목"));

    CoursePost missing = edited;
    missing.id = 99;
    CHECK(!gm.updatePost(missing));

    CoursePost bad = edited;
    bad.week = 0;
    CHECK(!gm.updatePost(bad));
    CHECK_EQ(gm.findPost(id)->title, std::string("바뀐 제목"));

    CHECK(!gm.removePost(99));
    CHECK(gm.removePost(id));
    CHECK(gm.findPost(id) == nullptr);
}

TEST_CASE(removeCourse_and_updateCourse_cascade_to_posts) {
    TempDir dir;
    GradeManager gm(dir.str());
    CHECK(gm.addCourse(makeCourse("100001", "과목1")));
    CHECK(gm.addCourse(makeCourse("100002", "과목2")));
    CHECK(gm.addPost(makePost("100001", "2026-1", "material", 1, "가")) > 0);
    CHECK(gm.addPost(makePost("100001", "2026-1", "assignment", 2, "나")) > 0);
    CHECK(gm.addPost(makePost("100002", "2026-1", "midterm", 8, "다")) > 0);

    CHECK(gm.updateCourse("100001", makeCourse("100003", "과목1")));
    CHECK_EQ(countByCourse(gm, "100001"), static_cast<size_t>(0));
    CHECK_EQ(countByCourse(gm, "100003"), static_cast<size_t>(2));

    CHECK(gm.removeCourse("100003"));
    CHECK_EQ(countByCourse(gm, "100003"), static_cast<size_t>(0));
    CHECK_EQ(countByCourse(gm, "100002"), static_cast<size_t>(1));
    CHECK_EQ(gm.getPosts().size(), static_cast<size_t>(1));
}

TEST_CASE(posts_survive_save_and_load_with_line_breaks) {
    TempDir dir;
    const std::string body = "첫 줄\n둘째 줄, 쉼표 \"따옴표\" 백슬래시\\n 그대로";
    {
        GradeManager gm(dir.str());
        CHECK(gm.addCourse(makeCourse("100001", "과목1")));
        CoursePost p = makePost("100001", "2026-1", "assignment", 3, "제목, 쉼표");
        p.body = body;
        p.link = "https://a.b/c?d=1";
        p.dueDate = "2026-10-20 13:00";
        p.author = "kimcs";
        p.createdAt = "2026-10-01 09:00";
        CHECK_EQ(gm.addPost(p), 1);
        CHECK(gm.addPost(makePost("100001", "2026-1", "material", 1, "둘째")) > 0);
        CHECK(gm.removePost(1));
        CHECK_EQ(gm.addPost(p), 3);
        CHECK(gm.save());
    }

    GradeManager loaded(dir.str());
    CHECK(loaded.load());
    CHECK_EQ(loaded.getPosts().size(), static_cast<size_t>(2));
    const CoursePost* p = loaded.findPost(3);
    CHECK(p != nullptr);
    if (p != nullptr) {
        CHECK_EQ(p->body, body);
        CHECK_EQ(p->title, std::string("제목, 쉼표"));
        CHECK_EQ(p->link, std::string("https://a.b/c?d=1"));
        CHECK_EQ(p->dueDate, std::string("2026-10-20 13:00"));
        CHECK_EQ(p->author, std::string("kimcs"));
        CHECK_EQ(p->createdAt, std::string("2026-10-01 09:00"));
        CHECK_EQ(p->week, 3);
    }
    // 새 id 는 최대 id + 1
    CHECK_EQ(loaded.addPost(makePost("100001", "2026-1", "final", 15, "셋째")), 4);
}
