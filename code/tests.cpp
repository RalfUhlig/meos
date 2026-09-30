/************************************************************************
    MeOS - Orienteering Software
    Copyright (C) 2009-2023 Melin Software HB

    Melin Software HB - software@melin.nu - www.melin.nu
    Eksoppsvägen 16, SE-75646 UPPSALA, Sweden

************************************************************************/
#include "stdafx.h"

#include "testmeos.h"
#include "gdioutput.h"
#include "gdistructures.h"
#include "oEvent.h"
#include "timeconstants.hpp"

namespace {

// The competition of all tests: one course with three controls and a class that uses
// it, created through the pages as a user would.
class CompetitionTest : public TestMeOS {
public:
  CompetitionTest(TestMeOS &tm, const char *name) : TestMeOS(tm, name) {}

protected:
  void createCompetition() const {
    showTab(TCmpTab);
    press("NewCmp");
    input("Name", "Test Cup");
    input("Date", "2026-09-30");
    input("FirstStart", "09:00:00");
    press("NoEntries");
    press("FIndividual");
    assertEquals(L"Test Cup", oe().getName());

    showTab(TCourseTab);
    press("Add");
    input("Name", "Course A");
    input("Controls", "31 32 33");
    input("Length", "2500");
    press("Save");
    pCourse course = oe().getCourse(L"Course A");
    assertTrue("course created", course != nullptr);
    assertEquals(3, course->getNumControls());

    showTab(TClassTab);
    press("Add");
    input("Name", "H21");
    selectString("Courses", "Course A");
    press("Save");
    pClass cls = oe().getClass(L"H21");
    assertTrue("class created", cls != nullptr);
    assertTrue("class uses the course", cls->getCourse() == course);
  }

  // Enters a runner on the runner page.
  void enterRunner(const char *name, const char *club, int cardNo) const {
    showTab(TRunnerTab);
    press("Add");
    input("Name", name);
    input("Club", club);
    selectString("RClass", "H21");
    input("CardNo", itos(cardNo).c_str());
    press("Save");
  }

  pRunner runner(const wchar_t *name) const {
    pRunner r = oe().getRunnerByName(name);
    if (!r)
      throw meosAssertionFailure(wstring(L"Runner not found: ") + name);
    return r;
  }

  // The vertical position of a text on the page, -1 if it is missing.
  int textY(const wstring &text) const {
    for (const TextInfo &ti : gdi().getTL()) {
      if (ti.text == text)
        return ti.getY();
    }
    return -1;
  }
};

class TestEnterRunner : public CompetitionTest {
public:
  TestEnterRunner(TestMeOS &tm, const char *name) : CompetitionTest(tm, name) {}
  TestMeOS *newInstance() const override { return new TestEnterRunner(*this); }

  void run() const override {
    createCompetition();
    enterRunner("Anna Berg", "OK Test", 500001);

    assertEquals(1, oe().getNumRunners());
    pRunner r = runner(L"Anna Berg");
    assertEquals(L"OK Test", r->getClub());
    assertEquals(L"H21", r->getClass(false));
    assertEquals(500001, r->getCardNo());
    assertEquals("name in the form", "Anna Berg", getText("Name"));
  }
};

class TestReadCard : public CompetitionTest {
public:
  TestReadCard(TestMeOS &tm, const char *name) : CompetitionTest(tm, name) {}
  TestMeOS *newInstance() const override { return new TestReadCard(*this); }

  void run() const override {
    createCompetition();
    enterRunner("Anna Berg", "OK Test", 500001);
    enterRunner("Bo Ek", "OK Test", 500002);

    showTab(TSITab);
    // Start 10:00:00, finish 10:25:00 (seconds since midnight).
    insertCard(500001, "S-36000;31-36300;32-36720;33-37140;F-37500");
    // Control 32 missing.
    insertCard(500002, "S-36060;31-36400;33-37300;F-37620");

    pRunner anna = runner(L"Anna Berg");
    assertTrue("card of Anna Berg", anna->getCard() != nullptr);
    assertEquals(StatusOK, anna->getStatus());
    assertEquals(25 * 60 * timeConstSecond, anna->getRunningTime(false));

    pRunner bo = runner(L"Bo Ek");
    assertTrue("card of Bo Ek", bo->getCard() != nullptr);
    assertEquals(StatusMP, bo->getStatus());
  }
};

class TestResultList : public CompetitionTest {
public:
  TestResultList(TestMeOS &tm, const char *name) : CompetitionTest(tm, name) {}
  TestMeOS *newInstance() const override { return new TestResultList(*this); }

  void run() const override {
    createCompetition();
    enterRunner("Anna Berg", "OK Test", 500001);
    enterRunner("Bo Ek", "IK Lauf", 500002);
    enterRunner("Cleo Dahl", "OK Test", 500003);

    showTab(TSITab);
    insertCard(500001, "S-36000;31-36300;32-36720;33-37140;F-37500"); // 25:00
    insertCard(500002, "S-36060;31-36400;33-37300;F-37620");          // control 32 missing
    insertCard(500003, "S-36120;31-36400;32-36800;33-37200;F-37500"); // 23:00

    showTab(TListTab);
    press("ResultIndividual");

    checkString("Anna Berg");
    checkString("Bo Ek");
    checkString("Cleo Dahl");
    checkString("25:00");
    checkString("23:00");
    const int cleo = textY(L"Cleo Dahl");
    const int anna = textY(L"Anna Berg");
    const int bo = textY(L"Bo Ek");
    assertTrue("Cleo Dahl before Anna Berg", cleo < anna);
    assertTrue("Anna Berg before Bo Ek", anna < bo);
    assertEquals(1, runner(L"Cleo Dahl")->getPlace());
    assertEquals(2, runner(L"Anna Berg")->getPlace());
  }
};

} // namespace

void registerTests(TestMeOS &tm) {
  tm.registerTest(TestEnterRunner(tm, "Enter runner"));
  tm.registerTest(TestReadCard(tm, "Read card"));
  tm.registerTest(TestResultList(tm, "Result list"));
}
