# meos
MeOS - A Much Easier Orienteering System

Source code for the MeOS project (www.melin.nu/meos)

## This branch

`local` is the personally used version of this fork: upstream MeOS plus the changes below.
`master` is a plain mirror of [melinsoftware/meos](https://github.com/melinsoftware/meos).
See [docs/UPSTREAM_SYNC.md](docs/UPSTREAM_SYNC.md) for how the branches fit together.

### Features

Personal additions, not proposed upstream.

- **Multiple races per competitor** — a rework of the existing MeOS option, which used to create
  the additional entry by itself from a guessed class and could attach an invented course to
  that class. When a card is read for somebody who already has a result, the readout now says
  who was found and what they already have, and lets the operator pick the class and course of
  the additional race. A rental card handed out twice becomes visible before anything is
  written, and "Another competitor" leads back to selecting one by name. With the option off,
  nothing of this happens.
- **Card owner from the competition, not the runner database** — a card whose number an entry of
  the competition already carries is no longer looked up in the runner database. For a rental
  card the database names whoever the card was once registered to, and a card read while
  another tab was open could silently enter that person and give them the result. The
  additional race is now also recognised for a card read in the background, which then waits
  for the operator on the SportIdent page.
- **Course and class kept together on readout** — the readout dialogs ask for the course also for
  a new competitor from an unknown card and for one without a class, so a walk-in who only knows
  the course can be entered by it. Picking a course that belongs to exactly one class selects
  that class; a course of no class keeps the class, also when the class is changed afterwards.
  In a forked class the fork the card fits best is preselected instead of the one that would
  follow from the start number. With multiple races per competitor allowed, the course the card
  completes — searched over all courses, those of no class included — is proposed together with
  its class, for the operator to confirm. Without that option, a completed course of another
  class stays what it is in a regular competition: a mispunch.
- **Course based results** — a new MeOS feature switch (Teams and forking): courses forked from
  one IOF XML 3.0 course family are ranked as a single course in course result lists, including
  the time behind, and the card readout reports the place within class and course instead of the
  class place. Course identity is untouched, so a swapped fork is still a mispunch. Lists need no
  extra element: with a course sort order the group header follows the ranking on its own. The
  switch is left out of "all features" so that it stays a deliberate choice.
- **Courses sorted by name** — the course sort orders (course result, class and course result,
  course start time, also with a result module) list the courses by name, with numbers compared
  by value (301, 302, …, 310), instead of in the order they were created. With course families
  merged, a family is placed by its family name. No new sort order is introduced, so a list
  definition still loads in upstream MeOS — it is just ordered by creation there.
- **Duplicate** on the runner tab prepares a second entry for a competitor by hand, for a repeat
  start announced at the entry desk; the readout then picks it up without asking.

Smaller: the card readout line shows the course when it differs from the class name, a card
stored unpaired names the competitor its number belongs to, and the German translation of the
affected strings was completed.

### Fixes

Fixes of upstream behaviour, kept on `bugfix/*` branches.

- A readout no longer builds a course out of the punches for a class the operator configured
  without one — it used to attach that course to the class, changing it for everyone in it.
- Overwriting an existing result with a second readout now asks first, states which class the
  existing result is in, and offers saving the card unpaired or cancelling.
- Additional entries are numbered correctly: the "(n)" suffix was misparsed beyond a single
  digit and could produce two competitors of exactly the same name, which `getRunnerByName`
  then no longer resolves.
- The class course entry on the runner tab is labelled after the class, not after the course the
  competitor happens to have of their own.
- A list switched to no row limit, a margin of 0 % or a display time of 0 keeps these settings:
  the HTML export dropped the row limit, columns and margin whenever the margin was 0, and a
  reopened competition read every zero back as the default — 60 rows per page, 5 %, 8 seconds.
- A list link of the Information Server (`?html=1&type=…`) keeps working after a restart of
  MeOS or the service; it used to answer "Unknown list" until the list overview had been
  opened once.
