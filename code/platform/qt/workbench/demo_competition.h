/************************************************************************
    MeOS - Orienteering Software
    Linux port: a small competition for the GUI workbench and the load test.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#pragma once

#include <string>
#include <vector>

class oEvent;

namespace demo_competition {

struct Entry {
  const wchar_t *name;
  const wchar_t *club;
  const wchar_t *className;
  int cardNo;
  const wchar_t *start;
  const wchar_t *finish; // Empty: did not start
};

// Fictional runners, with names and clubs outside ASCII.
const std::vector<Entry> &entries();
int numClasses();
int numClubs();

// Replaces the competition in oe with the demo competition (classes without courses,
// start and finish times, status OK or DNS).
void create(oEvent &oe);

} // namespace demo_competition
