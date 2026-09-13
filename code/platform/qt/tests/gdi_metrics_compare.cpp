/************************************************************************
    MeOS - Orienteering Software
    Linux port: compares the text metrics of the Qt backend with Windows GDI.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// gdi_metrics_compare <linux.csv> <windows.csv> [face...]
//
// Compares two outputs of gdi_metrics. A value is an outlier if a height differs
// by more than 1 pixel or a width by more than 3 % (at least 1 pixel). Prints a
// summary per face and every outlier. Fails if more than 1 % of the values of a
// face named on the command line are outliers or missing: hinting differences of
// a pixel at the smallest sizes, and line breaks that tip over by a pixel, remain.
// The other faces are only reported (their substitutes are not settled yet).
// Exits with 77 (skipped) if there is no Windows output.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Value {
  long width = 0;
  long height = 0;
};

using Key = std::string; // face,height,style,measure,text

bool readCsv(const char *path, std::map<Key, Value> &values) {
  std::ifstream in(path);
  if (!in)
    return false;
  std::string line;
  std::getline(in, line); // header
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ','))
      fields.push_back(field);
    if (fields.size() != 7)
      continue;
    const Key key = fields[0] + ',' + fields[1] + ',' + fields[2] + ',' + fields[3] + ',' + fields[4];
    values[key] = Value{std::stol(fields[5]), std::stol(fields[6])};
  }
  return true;
}

struct Summary {
  int count = 0;
  int outliers = 0;
  int missing = 0;
  double widthDeviation = 0; // sum of relative deviations
  int widthSamples = 0;
};

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <linux.csv> <windows.csv> [face...]\n", argv[0]);
    return 2;
  }
  std::map<Key, Value> linux;
  std::map<Key, Value> windows;
  if (!readCsv(argv[1], linux)) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  if (!readCsv(argv[2], windows)) {
    std::printf("no Windows metrics in %s; run gdi_metrics on Windows to create them\n", argv[2]);
    return 77;
  }
  const std::set<std::string> strictFaces(argv + 3, argv + argc);

  std::map<std::string, Summary> summaries;
  for (const auto &[key, reference] : windows) {
    // GetTextExtentPoint32 of the text with a line break (text 14) gave 56 and 62
    // pixels for Arial 11 in two runs on the same Windows machine; MeOS measures
    // line breaks only with DrawText.
    if (key.size() > 10 && key.compare(key.size() - 10, 10, ",extent,14") == 0)
      continue;
    const std::string face = key.substr(0, key.find(','));
    Summary &summary = summaries[face];
    summary.count++;
    const auto entry = linux.find(key);
    if (entry == linux.end()) {
      summary.missing++;
      continue;
    }
    const Value &value = entry->second;
    const long widthDiff = std::labs(value.width - reference.width);
    const long widthLimit = std::max(1L, std::lround(reference.width * 0.03));
    if (reference.width > 0) {
      summary.widthDeviation += double(widthDiff) / double(reference.width);
      summary.widthSamples++;
    }
    if (widthDiff > widthLimit || std::labs(value.height - reference.height) > 1) {
      summary.outliers++;
      std::printf("outlier %s: windows %ld x %ld, linux %ld x %ld\n", key.c_str(), reference.width,
                  reference.height, value.width, value.height);
    }
  }

  bool failed = false;
  for (const auto &[face, summary] : summaries) {
    const bool strict = strictFaces.count(face) > 0;
    std::printf("%-20s %5d values, %4d outliers, %3d missing, mean width deviation %.1f %%%s\n", face.c_str(),
                summary.count, summary.outliers, summary.missing,
                summary.widthSamples ? 100.0 * summary.widthDeviation / summary.widthSamples : 0.0,
                strict ? " (strict)" : "");
    if (strict && 100 * (summary.outliers + summary.missing) > summary.count)
      failed = true;
  }
  return failed ? 1 : 0;
}
