/************************************************************************
    MeOS - Orienteering Software
    Linux port: replay of recorded SI master station traffic.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Replays traces of a real SI master station (MEOS_SERIAL_TRACE, stage 1.3.6) and
// reads the cards with the real SportIdent class. A pseudo terminal stands in for the
// station's serial port: its master side checks every request MeOS sends against the
// trace and answers with the recorded bytes. The station data and the cards that
// SportIdent reports must match the "# station" and "# card" lines of the trace, in
// order. Keeps the hardware findings of 1.3.6 testable without a station.
//
//   meos_si_replay_test <folder>     replays every *.txt file in the folder
//
// What a trace line means for the replay (one port at a time):
//
//   open ...            MeOS opens the port: openCom, then startMonitorThread
//   > bytes             MeOS writes; the replay checks the bytes
//   < bytes             the station sends: right after a request, or after a pause
//                       when the trace shows one (a card inserted or removed)
//   ! ... (error 5)     the station is unplugged; MeOS must close the port and tell
//                       its window (WM_USER+1), as on Windows
//   close               MeOS closes the port (closeCom)
//
// The other lines (timeouts, line state, the remaining "!" lines) follow from these
// and are not replayed. Cards are described as
//
//   card <number> [first <name> last <name>] check <time> start <time>
//        finish <time> [voltage <mV>] punches <code>/<time> ...
//
// with "-" for a missing punch. Traces of real cards carry the owner's name: replace
// it before adding a trace here (and recompute the CRC of the block).

#include "StdAfx.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "app_frame.h"
#include "SportIdent.h"
#include "timeconstants.hpp"

namespace {

int failures = 0;

void fail(const std::string &where, const std::string &message) {
  std::fprintf(stderr, "%s: %s\n", where.c_str(), message.c_str());
  failures++;
}

std::string hex(const std::vector<BYTE> &bytes) {
  std::string text;
  char digits[4];
  for (BYTE b : bytes) {
    std::snprintf(digits, sizeof(digits), "%s%02X", text.empty() ? "" : " ", b);
    text += digits;
  }
  return text.empty() ? "nothing" : text;
}

/* ---------------------------------------------------------------------
   Trace files
   --------------------------------------------------------------------- */

struct Exchange {
  bool fromStation = false; // "<", otherwise ">"
  double time = 0;
  int line = 0;
  std::vector<BYTE> bytes;
};

struct Session {
  std::string port;
  int line = 0;
  std::vector<Exchange> exchanges;
  bool unplugged = false;
  bool ended = false;
};

struct Trace {
  std::string name;
  std::vector<Session> sessions;
  std::vector<std::string> stations;
  std::vector<std::string> cards;
};

bool parseTrace(const std::filesystem::path &file, Trace &trace) {
  trace.name = file.filename().string();
  std::ifstream in(file);
  if (!in) {
    fail(trace.name, "cannot be read");
    return false;
  }

  std::string text;
  int number = 0;
  while (meosGetline(in, text)) {
    number++;
    const std::string where = trace.name + ":" + std::to_string(number);
    if (text.empty())
      continue;
    if (text[0] == '#') {
      if (text.compare(0, 10, "# station ") == 0)
        trace.stations.push_back(text.substr(2));
      else if (text.compare(0, 7, "# card ") == 0)
        trace.cards.push_back(text.substr(2));
      continue;
    }

    std::istringstream line(text);
    double time = 0;
    std::string port, what;
    if (!(line >> time >> port >> what)) {
      fail(where, "not a trace line");
      return false;
    }
    Session *session = trace.sessions.empty() ? nullptr : &trace.sessions.back();
    if (what == "open") {
      if (session && !session->ended) {
        fail(where, "the replay handles one port at a time, " + session->port + " is still open");
        return false;
      }
      trace.sessions.emplace_back();
      trace.sessions.back().port = port;
      trace.sessions.back().line = number;
      continue;
    }
    if (!session || port != session->port) {
      fail(where, port + " is not open");
      return false;
    }

    if (what == "<" || what == ">") {
      if (session->ended) {
        fail(where, "data after the end of the session");
        return false;
      }
      Exchange exchange;
      exchange.fromStation = what == "<";
      exchange.time = time;
      exchange.line = number;
      std::string digits;
      while (line >> digits)
        exchange.bytes.push_back(BYTE(std::strtoul(digits.c_str(), nullptr, 16)));
      session->exchanges.push_back(exchange);
    }
    else if (what == "!" && text.find("(error 5)") != std::string::npos && !session->ended) {
      session->unplugged = true;
      session->ended = true;
    }
    else if (what == "close") {
      session->ended = true;
    }
  }

  if (!trace.sessions.empty() && !trace.sessions.back().ended) {
    fail(trace.name, "the last session does not end");
    return false;
  }
  if (trace.stations.size() != trace.sessions.size()) {
    fail(trace.name, "needs one \"# station\" line per session");
    return false;
  }
  return true;
}

/* ---------------------------------------------------------------------
   The station: master side of a pseudo terminal
   --------------------------------------------------------------------- */

class Station {
public:
  Station() {
    master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    char name[64] = {0};
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0 ||
        ::ptsname_r(master, name, sizeof(name)) != 0)
      return;
    path = name;
    // A second descriptor of MeOS' side, only to see how many bytes MeOS has not read yet.
    probe = ::open(name, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  }

  ~Station() {
    unplug();
    if (probe >= 0)
      ::close(probe);
  }

  bool valid() const {
    return master >= 0 && probe >= 0;
  }

  // Closing the master side hangs up the terminal, as unplugging the adapter does.
  void unplug() {
    if (master >= 0)
      ::close(master);
    master = -1;
  }

  // MeOS reads a reply until the line has been quiet for up to 460 ms (readBytes_delay
  // with the timeouts of openCom), at most about 710 ms after the station sent it (the
  // station data: Sleep 100, Sleep 100, 50 ms interval, 460 ms). Before the station
  // sends something new, or is unplugged, it waits until MeOS has read everything and
  // both times have passed, so that MeOS does not take the new message for the rest of
  // the previous reply and waits for a card as it did in the trace. Bytes written to the
  // master side reach MeOS' input queue asynchronously (the kernel's flip buffer), so an
  // empty queue right after a write proves nothing; hence the bound from the write.
  static constexpr DWORD quietTime = 600;
  static constexpr DWORD replyTime = 1200;

  // Returns false if MeOS did not read what the station sent.
  bool waitUntilQuiet() const {
    const std::uint64_t deadline = GetTickCount64() + 5000;
    int waiting = 0;
    while (::ioctl(probe, FIONREAD, &waiting) == 0 && waiting > 0) {
      if (GetTickCount64() > deadline)
        return false;
      Sleep(10);
    }
    const unsigned long long until =
        std::max<unsigned long long>(GetTickCount64() + quietTime, lastSend + replyTime);
    while (GetTickCount64() < until)
      Sleep(10);
    return true;
  }

  void send(const std::vector<BYTE> &bytes) {
    lastSend = GetTickCount64();
    std::size_t done = 0;
    while (done < bytes.size()) {
      const ssize_t written = ::write(master, bytes.data() + done, bytes.size() - done);
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        return;
      done += std::size_t(written);
    }
  }

  // Up to count bytes that MeOS writes within the timeout; with timeout 0, the bytes
  // that are already there.
  std::vector<BYTE> receive(std::size_t count, DWORD timeout) const {
    std::vector<BYTE> bytes;
    const std::uint64_t deadline = GetTickCount64() + timeout;
    while (bytes.size() < count) {
      const std::uint64_t now = GetTickCount64();
      pollfd ready = {master, POLLIN, 0};
      const int result = ::poll(&ready, 1, now >= deadline ? 0 : int(deadline - now));
      if (result < 0 && errno == EINTR)
        continue;
      if (result <= 0)
        break;
      BYTE buffer[512];
      const ssize_t got = ::read(master, buffer, std::min(sizeof(buffer), count - bytes.size()));
      if (got > 0)
        bytes.insert(bytes.end(), buffer, buffer + got);
      else if (got == 0 || (errno != EINTR && errno != EAGAIN))
        break;
    }
    return bytes;
  }

  std::string path;

private:
  int master = -1;
  int probe = -1;
  unsigned long long lastSend = 0; // GetTickCount64
};

// Plays one session of a trace on the station; runs in its own thread while MeOS
// talks to the port. An empty error means that MeOS sent what the trace says.
struct Replay {
  Replay(Station &station, const Session &session, const std::string &traceName)
      : station(station), session(session), traceName(traceName) {}

  void run() {
    const Exchange *previous = nullptr;
    for (const Exchange &exchange : session.exchanges) {
      const std::string where = traceName + ":" + std::to_string(exchange.line);
      if (exchange.fromStation) {
        // A pause in the station's data means that something happened at the station.
        if (previous && previous->fromStation && exchange.time - previous->time > 0.25)
          station.waitUntilQuiet();
        station.send(exchange.bytes);
      }
      else {
        const std::vector<BYTE> got = station.receive(exchange.bytes.size(), 5000);
        if (got != exchange.bytes) {
          errorAt = where;
          error = "MeOS sent " + hex(got) + ", the trace has " + hex(exchange.bytes);
          break;
        }
      }
      previous = &exchange;
    }

    if (error.empty()) {
      errorAt = traceName;
      if (!station.waitUntilQuiet())
        error = "MeOS did not read the last message of the station";
      const std::vector<BYTE> extra = station.receive(64, 0);
      if (error.empty() && !extra.empty())
        error = "MeOS sent more than the trace has: " + hex(extra);
    }
    if (session.unplugged)
      station.unplug();
    done = true;
  }

  Station &station;
  const Session &session;
  const std::string traceName;
  std::string errorAt;
  std::string error;
  std::atomic<bool> done{false};
};

/* ---------------------------------------------------------------------
   MeOS side
   --------------------------------------------------------------------- */

int cardMessages = 0; // WM_USER: a card is waiting
int lostMessages = 0; // WM_USER+1: the connection to a unit is lost

LRESULT CALLBACK notifyProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_USER)
    cardMessages++;
  else if (message == WM_USER + 1)
    lostMessages++;
  return DefWindowProc(window, message, wParam, lParam);
}

// Processes messages until the condition holds; the window's timer makes GetMessage
// return regularly.
template <class Condition>
bool pumpUntil(Condition condition, DWORD timeout) {
  const std::uint64_t deadline = GetTickCount64() + timeout;
  MSG msg;
  while (!condition()) {
    if (GetTickCount64() > deadline)
      return false;
    if (GetMessage(&msg, nullptr, 0, 0) <= 0)
      return false;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return true;
}

void pumpFor(DWORD time) {
  const std::uint64_t end = GetTickCount64() + time;
  pumpUntil([end] { return GetTickCount64() >= end; }, time + 1000);
}

std::string describeTime(const SIPunch &punch) {
  if (punch.Code == DWORD(-1))
    return "-";
  const int time = int(punch.Time);
  char text[32];
  std::snprintf(text, sizeof(text), "%d:%02d:%02d", time / timeConstHour, (time / timeConstMinute) % 60,
                (time / timeConstSecond) % 60);
  std::string result = text;
  if (time % timeConstSecond)
    result += "." + std::to_string(time % timeConstSecond);
  return result;
}

std::string describe(const SICard &card) {
  std::string text = "card " + std::to_string(card.CardNumber);
  if (card.firstName[0] || card.lastName[0])
    text += " first " + meos_compat::wideToUtf8(card.firstName) + " last " + meos_compat::wideToUtf8(card.lastName);
  text += " check " + describeTime(card.CheckPunch);
  text += " start " + describeTime(card.StartPunch);
  text += " finish " + describeTime(card.FinishPunch);
  if (card.miliVolt)
    text += " voltage " + std::to_string(card.miliVolt);
  text += " punches";
  for (DWORD k = 0; k < card.nPunch; k++)
    text += " " + std::to_string(card.Punch[k].Code) + "/" + describeTime(card.Punch[k]);
  return text;
}

std::string describe(const SI_StationInfo *info) {
  if (!info || info->data.empty())
    return "station unknown";
  std::string text;
  for (const SI_StationData &data : info->data) {
    if (!text.empty())
      text += ", ";
    text += "station " + std::to_string(data.stationNumber) + " mode " + std::to_string(data.stationMode);
    if (data.extended)
      text += " extended";
    if (data.handShake)
      text += " handshake";
    if (data.autoSend)
      text += " autosend";
  }
  return text;
}

void replayTrace(HWND window, const Trace &trace) {
  // Battery voltage on, as MeOS read it when the traces were recorded.
  SportIdent si(window, 0, true);
  std::vector<SICard> cards;
  const int cardsBefore = cardMessages;
  const int lostBefore = lostMessages;
  int unplugged = 0;

  for (size_t s = 0; s < trace.sessions.size(); s++) {
    const Session &session = trace.sessions[s];
    const std::string where = trace.name + ":" + std::to_string(session.line);
    Station station;
    if (!station.valid()) {
      fail(where, "no pseudo terminal");
      return;
    }
    const std::wstring com = meos_compat::utf8ToWide(station.path.c_str());

    Replay replay(station, session, trace.name);
    std::thread player([&replay] { replay.run(); });

    if (!si.openCom(com.c_str()))
      fail(where, "openCom failed");
    const std::string found = describe(si.findStation(com));
    if (found != trace.stations[s])
      fail(where, "MeOS found \"" + found + "\", the trace has \"" + trace.stations[s] + "\"");
    si.startMonitorThread(com.c_str());

    if (!pumpUntil([&replay] { return replay.done.load(); }, 120000))
      fail(where, "the replay did not end");
    player.join();
    if (!replay.error.empty())
      fail(replay.errorAt, replay.error);

    if (session.unplugged) {
      unplugged++;
      // MeOS closes the port itself and posts WM_USER+1 after that.
      if (!pumpUntil([&] { return lostMessages - lostBefore >= unplugged; }, 5000))
        fail(where, "MeOS did not report the lost connection");
      const SI_StationInfo *info = si.findStation(com);
      if (info && info->hComm)
        fail(where, "MeOS did not close the port of the unplugged station");
    }
    else {
      pumpFor(100);
      si.closeCom(com.c_str());
    }

    std::vector<SICard> batch;
    SICard card(ConvertedTimeStatus::Unknown);
    while (si.getCard(card))
      batch.push_back(card);
    // getCard returns the newest card first.
    cards.insert(cards.end(), batch.rbegin(), batch.rend());
  }

  pumpFor(100);
  // TabSI fetches the cards when the window gets WM_USER, one message per card.
  if (cardMessages - cardsBefore != int(cards.size()))
    fail(trace.name, std::to_string(cardMessages - cardsBefore) + " card messages for " +
                         std::to_string(cards.size()) + " card(s)");
  if (lostMessages - lostBefore != unplugged)
    fail(trace.name, std::to_string(lostMessages - lostBefore) + " lost connection messages, expected " +
                         std::to_string(unplugged));

  for (size_t k = 0; k < std::max(cards.size(), trace.cards.size()); k++) {
    const std::string got = k < cards.size() ? describe(cards[k]) : "no card";
    const std::string expected = k < trace.cards.size() ? trace.cards[k] : "no card";
    if (got != expected)
      fail(trace.name, "card " + std::to_string(k + 1) + ":\n  read     " + got + "\n  expected " + expected);
  }
  std::printf("%s: %zu session(s), %zu card(s)\n", trace.name.c_str(), trace.sessions.size(), cards.size());
}

std::string argument(const char *commandLine) {
  std::string text = commandLine ? commandLine : "";
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
    text = text.substr(1, text.size() - 2);
  // The command line is in the ANSI code page, as on Windows.
  const int size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), int(text.size()), nullptr, 0);
  std::wstring wide(std::size_t(std::max(size, 0)), L'\0');
  if (size > 0)
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), int(text.size()), &wide[0], size);
  return meos_compat::wideToUtf8(wide.c_str());
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
  const std::filesystem::path folder = argument(lpCmdLine);
  std::vector<std::filesystem::path> files;
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(folder, error)) {
    if (entry.path().extension() == ".txt")
      files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    std::fprintf(stderr, "usage: meos_si_replay_test <folder with traces>; no *.txt in \"%s\"\n",
                 folder.string().c_str());
    return 1;
  }

  if (!app_frame::initialize(hInstance, L"MeOS SI Replay Test")) {
    std::fprintf(stderr, "initialization failed\n");
    return 1;
  }

  WNDCLASSEX windowClass = {};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = notifyProc;
  windowClass.hInstance = hInstance;
  windowClass.lpszClassName = L"MeOSSIReplay";
  RegisterClassEx(&windowClass);
  const HWND window = CreateWindowEx(0, L"MeOSSIReplay", L"SI replay", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100,
                                     NULL, NULL, hInstance, NULL);
  if (!window) {
    std::fprintf(stderr, "no window\n");
    return 1;
  }
  SetTimer(window, 1, 20, nullptr);

  for (const std::filesystem::path &file : files) {
    Trace trace;
    if (parseTrace(file, trace))
      replayTrace(window, trace);
  }

  KillTimer(window, 1);
  DestroyWindow(window);
  app_frame::shutdown();

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("SI replay test passed\n");
  return 0;
}
