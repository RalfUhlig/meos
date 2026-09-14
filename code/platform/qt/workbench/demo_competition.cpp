/************************************************************************
    MeOS - Orienteering Software
    Linux port: a small competition for the GUI workbench and the load test.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "stdafx.h"

#include "demo_competition.h"

#include <map>
#include <set>

#include "oEvent.h"

namespace demo_competition {

const std::vector<Entry> &entries() {
  static const std::vector<Entry> list = {
    {L"Anna Lindqvist", L"OK Älvdal", L"D21", 2100001, L"10:00:00", L"10:41:17"},
    {L"Björg Østergård", L"Skogsmyra SK", L"D21", 2100002, L"10:02:00", L"10:39:45"},
    {L"Chloé Dubois", L"CO Vallée Verte", L"D21", 2100003, L"10:04:00", L"10:52:03"},
    {L"Dörthe Müller", L"TV Waldhügel", L"D21", 2100004, L"10:06:00", L""},
    {L"Erik Sjöberg", L"OK Älvdal", L"H21", 2100005, L"10:01:00", L"10:37:30"},
    {L"François Lefèvre", L"CO Vallée Verte", L"H21", 2100006, L"10:03:00", L"10:36:59"},
    {L"Günther Weiß", L"TV Waldhügel", L"H21", 2100007, L"10:05:00", L"10:48:12"},
    {L"Håkon Nærø", L"Skogsmyra SK", L"H21", 2100008, L"10:07:00", L"10:44:01"},
    {L"Iveta Nováková", L"OK Älvdal", L"D45", 2100009, L"10:10:00", L"10:55:40"},
    {L"Jürgen Schäfer", L"TV Waldhügel", L"H45", 2100010, L"10:11:00", L"10:49:22"},
    {L"Kåre Åsheim", L"Skogsmyra SK", L"H45", 2100011, L"10:13:00", L"10:47:58"},
    {L"Łukasz Żółw", L"CO Vallée Verte", L"H45", 2100012, L"10:15:00", L"11:02:10"},
  };
  return list;
}

int numClasses() {
  std::set<std::wstring> classes;
  for (const Entry &e : entries())
    classes.insert(e.className);
  return int(classes.size());
}

int numClubs() {
  std::set<std::wstring> clubs;
  for (const Entry &e : entries())
    clubs.insert(e.club);
  return int(clubs.size());
}

void create(oEvent &oe) {
  oe.newCompetition(L"Workbench Cup");
  oe.setZeroTime(L"09:00:00", false);

  std::map<std::wstring, int> classIds;
  for (const Entry &e : entries()) {
    if (!classIds.count(e.className))
      classIds[e.className] = oe.addClass(e.className)->getId();
  }

  for (const Entry &e : entries()) {
    pRunner r = oe.addRunner(e.name, e.club, classIds[e.className], e.cardNo, L"", true);
    r->setStartTimeS(e.start);
    if (*e.finish) {
      r->setFinishTimeS(e.finish);
      r->setStatus(StatusOK, true, oBase::ChangeType::Update);
    }
    else
      r->setStatus(StatusDNS, true, oBase::ChangeType::Update);
    r->synchronize();
  }
  oe.reEvaluateAll(std::set<int>(), true);
}

} // namespace demo_competition
