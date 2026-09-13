/************************************************************************
    MeOS - Orienteering Software
    Linux port: GDI fonts and text output on Qt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Text follows GDI rather than Qt: font heights are cell heights, metrics come
// from the OS/2 table as GDI computes them, text is laid out without kerning or
// ligatures, and DrawText breaks lines by the rules of DrawText.

#include "win32_gdi.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QRawFont>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <map>

namespace {

using meos_qt::DeviceContext;
using meos_qt::Font;
using meos_qt::FontMetrics;

constexpr int nonAntialiasedQuality = 3; // NONANTIALIASED_QUALITY

std::wstring lowerCase(const std::wstring &text) {
  std::wstring lower(text);
  for (wchar_t &ch : lower)
    ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
  return lower;
}

/* ---------------------------------------------------------------------
   Font selection
   --------------------------------------------------------------------- */

// Families for Windows face names, best first. The first installed family is
// used; if none is installed, fontconfig chooses for the face name itself.
const std::map<std::wstring, std::vector<const char *>> &substitutes() {
  static const std::vector<const char *> sansSerif = {"Microsoft Sans Serif", "Tahoma", "Arial", "Liberation Sans"};
  static const std::vector<const char *> monospace = {"Courier New", "Liberation Mono", "DejaVu Sans Mono"};
  static const std::map<std::wstring, std::vector<const char *>> table = {
      {L"arial", {"Arial", "Liberation Sans"}},
      {L"times new roman", {"Times New Roman", "Liberation Serif"}},
      {L"courier new", {"Courier New", "Liberation Mono"}},
      // Selawik is Microsoft's metric compatible open replacement for Segoe UI.
      {L"segoe ui", {"Segoe UI", "Selawik", "Open Sans", "Noto Sans", "DejaVu Sans"}},
      {L"lucida console", {"Lucida Console", "DejaVu Sans Mono", "Liberation Mono"}},
      // Stock fonts.
      {L"ms shell dlg", sansSerif},
      {L"ms sans serif", sansSerif},
      {L"microsoft sans serif", sansSerif},
      {L"system", sansSerif},
      {L"courier", monospace},
      {L"fixedsys", monospace},
      {L"terminal", monospace},
  };
  return table;
}

// What the substitute of a missing face takes over from the original, derived from
// the Windows measurement (tests/gdi_metrics_windows.csv): the cell height per em
// ((winAscent + winDescent) / unitsPerEm); the cell heights GDI uses for the em
// heights from 6 pixels on, where the face picks them by its VDMX table; and whether
// the face has a bold style or GDI emboldens it, which widens every character by a
// pixel.
struct OriginalFace {
  qreal cellPerEm;
  std::vector<int> cells;
  bool hasBold;

  static constexpr int firstEm = 6;

  // The em height GDI picks for a cell height: the largest one whose cell fits.
  int emForCell(int cell) const {
    int em = 0;
    for (std::size_t i = 0; i < cells.size() && cells[i] <= cell; i++)
      em = firstEm + int(i);
    if (em && cell <= cells.back())
      return em;
    return std::max(qRound(cell / cellPerEm), 1);
  }

  int cellForEm(int em) const {
    if (em >= firstEm && em < firstEm + int(cells.size()))
      return cells[std::size_t(em - firstEm)];
    return qRound(em * cellPerEm);
  }
};

const OriginalFace *originalFace(const std::wstring &lowerFaceName) {
  static const std::map<std::wstring, OriginalFace> faces = {
      {L"segoe ui",
       {2724.0 / 2048.0,
        // Em heights 6 to 60, the same for all styles.
        {8,  10, 11, 12, 12, 13, 15, 17, 19, 20, 21, 23, 25, 25, 28, 30, 30, 31, 32,
         35, 36, 37, 38, 40, 41, 42, 45, 45, 46, 47, 48, 50, 51, 52, 54, 55, 57, 59,
         60, 61, 62, 62, 65, 66, 67, 68, 70, 71, 72, 74, 74, 76, 77, 78, 81},
        true}},
      {L"lucida console", {1.0, {}, false}},
  };
  const auto entry = faces.find(lowerFaceName);
  return entry == faces.end() ? nullptr : &entry->second;
}

bool isInstalled(const QString &family) {
  static std::mutex mutex;
  static std::map<QString, bool> known;
  std::lock_guard<std::mutex> lock(mutex);
  const auto entry = known.find(family);
  if (entry != known.end())
    return entry->second;
  const bool installed = QFontDatabase::hasFamily(family);
  known[family] = installed;
  return installed;
}

// Font metrics in font units, as GDI reads them.
struct DesignMetrics {
  qreal unitsPerEm = 0;
  int winAscent = 0;
  int winDescent = 0;
  int avgCharWidth = 0;
  int lineAscent = 0;
  int lineDescent = 0;
  int lineGap = 0;
  int boundsWidth = 0;
  // The VDMX table, which maps cell heights to pixel sizes.
  QByteArray vdmx;
};

int readU16(const QByteArray &table, int offset) {
  if (offset + 2 > table.size())
    return 0;
  return qFromBigEndian<quint16>(table.constData() + offset);
}

int readS16(const QByteArray &table, int offset) {
  if (offset + 2 > table.size())
    return 0;
  return qFromBigEndian<qint16>(table.constData() + offset);
}

DesignMetrics designMetrics(const QFont &font) {
  static std::mutex mutex;
  static std::map<QString, DesignMetrics> cache;
  const QString key = font.family() + QLatin1Char('|') + QString::number(font.weight()) + QLatin1Char('|') +
                      QString::number(font.italic());
  {
    std::lock_guard<std::mutex> lock(mutex);
    const auto entry = cache.find(key);
    if (entry != cache.end())
      return entry->second;
  }

  DesignMetrics metrics;
  const QRawFont raw = QRawFont::fromFont(font);
  if (raw.isValid() && raw.unitsPerEm() > 0) {
    metrics.unitsPerEm = raw.unitsPerEm();
    const QByteArray os2 = raw.fontTable("OS/2");
    const QByteArray hhea = raw.fontTable("hhea");
    metrics.avgCharWidth = readS16(os2, 2);
    metrics.winAscent = readU16(os2, 74);
    metrics.winDescent = readU16(os2, 76);
    metrics.lineAscent = readS16(hhea, 4);
    metrics.lineDescent = readS16(hhea, 6);
    metrics.lineGap = readS16(hhea, 8);
    const QByteArray head = raw.fontTable("head");
    metrics.boundsWidth = readS16(head, 40) - readS16(head, 36);
    metrics.vdmx = raw.fontTable("VDMX");
    if (metrics.winAscent + metrics.winDescent <= 0) {
      metrics.winAscent = metrics.lineAscent;
      metrics.winDescent = -metrics.lineDescent;
    }
    if (metrics.winAscent + metrics.winDescent <= 0) {
      const qreal toUnits = metrics.unitsPerEm / raw.pixelSize();
      metrics.winAscent = qRound(raw.ascent() * toUnits);
      metrics.winDescent = qRound(raw.descent() * toUnits);
    }
  }
  if (metrics.winAscent + metrics.winDescent <= 0) {
    // No outline font: take Qt's metrics at the probe size as font units.
    const QFontMetrics qtMetrics(font);
    metrics.unitsPerEm = font.pixelSize();
    metrics.winAscent = qtMetrics.ascent();
    metrics.winDescent = qtMetrics.descent();
    metrics.lineAscent = metrics.winAscent;
    metrics.lineDescent = -metrics.winDescent;
    metrics.boundsWidth = qtMetrics.maxWidth();
  }

  std::lock_guard<std::mutex> lock(mutex);
  cache[key] = metrics;
  return metrics;
}

// Looks up the pixel size for a GDI height in the VDMX table, as GDI does for
// the fonts that have one: for a cell height the largest size whose yMax - yMin
// does not exceed it, for an em height the entry of that size. Sets ascent and
// descent from the entry.
bool lookupVdmx(const QByteArray &vdmx, int height, int &em, int &ascent, int &descent) {
  const int ratioCount = readU16(vdmx, 4);
  int group = 0;
  for (int i = 0; i < ratioCount; i++) {
    const int ratio = 6 + 4 * i;
    if (ratio + 4 > vdmx.size())
      return false;
    const int charSet = quint8(vdmx[ratio]);
    const int xRatio = quint8(vdmx[ratio + 1]);
    const int yStart = quint8(vdmx[ratio + 2]);
    const int yEnd = quint8(vdmx[ratio + 3]);
    if (!charSet)
      continue;
    // Any aspect ratio, or the 1:1 of the screen.
    if ((xRatio == 0 && yStart == 0 && yEnd == 0) || (xRatio == 1 && yStart <= 1 && yEnd >= 1)) {
      group = readU16(vdmx, 6 + 4 * ratioCount + 2 * i);
      break;
    }
  }
  if (group <= 0 || group + 4 > vdmx.size())
    return false;

  const int records = readU16(vdmx, group);
  const int firstSize = quint8(vdmx[group + 2]);
  const int lastSize = quint8(vdmx[group + 3]);
  auto entry = [&](int i, int &size, int &yMax, int &yMin) {
    const int offset = group + 4 + 6 * i;
    if (offset + 6 > vdmx.size())
      return false;
    size = readU16(vdmx, offset);
    yMax = readS16(vdmx, offset + 2);
    yMin = readS16(vdmx, offset + 4);
    return true;
  };

  int size = 0;
  int yMax = 0;
  int yMin = 0;
  if (height > 0) {
    int found = -1;
    for (int i = 0; i < records; i++) {
      if (!entry(i, size, yMax, yMin))
        return false;
      if (yMax - yMin == height) {
        found = i;
        break;
      }
      if (yMax - yMin > height) {
        found = i - 1;
        break;
      }
    }
    if (found < 0 || !entry(found, size, yMax, yMin))
      return false;
  }
  else {
    const int wanted = -height;
    if (wanted < firstSize || wanted > lastSize)
      return false;
    int i = 0;
    for (; i < records; i++) {
      if (!entry(i, size, yMax, yMin) || size > wanted)
        return false;
      if (size == wanted)
        break;
    }
    if (i == records)
      return false;
  }
  em = size;
  ascent = yMax;
  descent = -yMin;
  return em > 0;
}

TEXTMETRIC textMetricOf(const Font &font) {
  const FontMetrics &m = font.metrics;
  TEXTMETRIC tm = {};
  tm.tmHeight = m.height;
  tm.tmAscent = m.ascent;
  tm.tmDescent = m.descent;
  tm.tmInternalLeading = m.internalLeading;
  tm.tmExternalLeading = m.externalLeading;
  tm.tmAveCharWidth = m.avgCharWidth;
  tm.tmMaxCharWidth = m.maxCharWidth;
  tm.tmWeight = font.font.weight();
  tm.tmDigitizedAspectX = 96;
  tm.tmDigitizedAspectY = 96;
  tm.tmFirstChar = 0x20;
  tm.tmLastChar = 0xFFFC;
  tm.tmDefaultChar = 0x1F;
  tm.tmBreakChar = 0x20;
  tm.tmItalic = font.logFont.lfItalic;
  tm.tmUnderlined = font.logFont.lfUnderline;
  tm.tmStruckOut = font.logFont.lfStrikeOut;
  // Bit 0 set means variable pitch; 4 is TMPF_TRUETYPE.
  tm.tmPitchAndFamily = BYTE((font.fixedPitch ? 0 : 1) | 4 | (font.logFont.lfPitchAndFamily & 0xF0));
  tm.tmCharSet = 0; // ANSI_CHARSET
  return tm;
}

/* ---------------------------------------------------------------------
   Measuring and drawing
   --------------------------------------------------------------------- */

QString toQString(LPCWSTR text, int length) {
  return QString::fromWCharArray(text, length);
}

int textWidth(const Font &font, const QString &text) {
  return text.isEmpty() ? 0 : QFontMetrics(font.font).horizontalAdvance(text);
}

// Draws one line with its cell's top left corner at (x, y), rotated by the escapement.
void drawTextLine(QPainter &painter, const DeviceContext &dc, const Font &font, int x, int y, const QString &text,
                  int width, int underline) {
  const FontMetrics &m = font.metrics;
  const QColor color = meos_qt::toQColor(dc.textColor);
  painter.save();
  painter.translate(x, y);
  if (font.logFont.lfEscapement)
    painter.rotate(-font.logFont.lfEscapement / 10.0);
  if (dc.bkMode == OPAQUE && width > 0)
    painter.fillRect(QRect(0, 0, width, m.height), meos_qt::toQColor(dc.bkColor));
  painter.setFont(font.font);
  painter.setPen(color);
  painter.drawText(QPoint(0, m.ascent), text);
  if (underline >= 0 && underline < text.size()) {
    const int left = textWidth(font, text.left(underline));
    const int charWidth = textWidth(font, text.mid(underline, 1));
    painter.fillRect(QRect(left, m.ascent + 1, charWidth, 1), color);
  }
  painter.restore();
}

// The area a line may cover, in DC coordinates.
QRect lineArea(const Font &font, int x, int y, int width) {
  if (!font.logFont.lfEscapement)
    return QRect(x, y, width, font.metrics.height);
  const int extent = width + font.metrics.height;
  return QRect(x - extent, y - extent, 2 * extent, 2 * extent);
}

struct TextLine {
  QString text;
  int width = 0;
  int underline = -1;
};

// "&&" stands for "&"; "&x" shows x underlined.
QString removePrefixes(const QString &text, int &underline) {
  QString result;
  result.reserve(text.size());
  underline = -1;
  for (int i = 0; i < text.size(); i++) {
    if (text[i] != QLatin1Char('&')) {
      result += text[i];
    }
    else if (i + 1 < text.size() && text[i + 1] == QLatin1Char('&')) {
      result += QLatin1Char('&');
      i++;
    }
    else if (i + 1 < text.size()) {
      underline = int(result.size());
    }
  }
  return result;
}

// Breaks a paragraph between words, as DrawText does: a line ends before the word
// that would pass maxWidth. The space at the break belongs to the line if it still
// fits (not for centred or right-aligned text); further spaces are dropped. maxWidth
// is at least the width of the widest word (see layoutText).
void breakParagraph(const Font &font, const QString &text, int underline, int maxWidth, bool keepSpace,
                    std::vector<TextLine> &lines) {
  const int length = int(text.size());
  auto isSpace = [&](int i) { return text[i] == QLatin1Char(' '); };
  auto addLine = [&](int start, int end) {
    TextLine line;
    line.text = text.mid(start, end - start);
    line.width = textWidth(font, line.text);
    if (underline >= start && underline < end)
      line.underline = underline - start;
    lines.push_back(std::move(line));
  };

  int start = 0;
  for (;;) {
    int lineEnd = start;
    int pos = start;
    bool broken = false;
    while (pos < length) {
      int wordStart = pos;
      while (wordStart < length && isSpace(wordStart))
        wordStart++;
      if (wordStart == length)
        break;
      int wordEnd = wordStart;
      while (wordEnd < length && !isSpace(wordEnd))
        wordEnd++;
      if (lineEnd > start && textWidth(font, text.mid(start, wordEnd - start)) > maxWidth) {
        broken = true;
        break;
      }
      lineEnd = wordEnd;
      pos = wordEnd;
    }
    if (!broken) {
      // The last line keeps its trailing spaces.
      addLine(start, length);
      return;
    }
    if (keepSpace && lineEnd < length && textWidth(font, text.mid(start, lineEnd + 1 - start)) <= maxWidth)
      lineEnd++;
    addLine(start, lineEnd);
    start = lineEnd;
    while (start < length && isSpace(start))
      start++;
  }
}

// Shortens a line to "..." at its end so that it fits maxWidth. At least the first
// character stays, even if the result is wider.
void addEllipsis(const Font &font, TextLine &line, int maxWidth) {
  if (line.width <= maxWidth || line.text.isEmpty())
    return;
  const QString dots = QStringLiteral("...");
  const int first = line.text[0].isHighSurrogate() && line.text.size() > 1 ? 2 : 1;
  int keep = int(line.text.size()) - 1;
  QString shortened;
  int width = 0;
  for (; keep >= first; keep--) {
    if (line.text[keep - 1].isHighSurrogate())
      continue;
    shortened = line.text.left(keep) + dots;
    width = textWidth(font, shortened);
    if (width <= maxWidth)
      break;
  }
  if (keep < first) {
    keep = first;
    shortened = line.text.left(keep) + dots;
    width = textWidth(font, shortened);
  }
  line.text = shortened;
  line.width = width;
  if (line.underline >= keep)
    line.underline = -1;
}

std::vector<TextLine> layoutText(const Font &font, const QString &text, UINT format, int maxWidth) {
  QStringList paragraphs;
  if (format & DT_SINGLELINE) {
    QString line = text;
    line.remove(QLatin1Char('\r'));
    line.remove(QLatin1Char('\n'));
    paragraphs << line;
  }
  else {
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    paragraphs = normalized.split(QLatin1Char('\n'));
    // A line break at the end does not start another line.
    if (paragraphs.size() > 1 && paragraphs.back().isEmpty())
      paragraphs.removeLast();
  }

  const bool wordBreak = (format & DT_WORDBREAK) && !(format & DT_SINGLELINE);
  std::vector<QString> visibleParagraphs;
  std::vector<int> underlines;
  for (const QString &paragraph : paragraphs) {
    int underline = -1;
    visibleParagraphs.push_back((format & DT_NOPREFIX) ? paragraph : removePrefixes(paragraph, underline));
    underlines.push_back(underline);
  }

  // A word wider than the rectangle widens it before the lines are broken, so
  // that the following lines may use that width as well (measured on Windows).
  int breakWidth = maxWidth;
  if (wordBreak) {
    for (const QString &paragraph : visibleParagraphs) {
      for (const QString &word : paragraph.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        breakWidth = std::max(breakWidth, textWidth(font, word));
    }
  }

  std::vector<TextLine> lines;
  for (std::size_t i = 0; i < visibleParagraphs.size(); i++) {
    const QString &visible = visibleParagraphs[i];
    const int underline = underlines[i];
    if (wordBreak) {
      breakParagraph(font, visible, underline, breakWidth, !(format & (DT_CENTER | DT_RIGHT)), lines);
    }
    else {
      TextLine line;
      line.text = visible;
      line.width = textWidth(font, visible);
      line.underline = underline;
      lines.push_back(std::move(line));
    }
  }
  // Also with DT_CALCRECT, which then measures the shortened text.
  if (format & DT_END_ELLIPSIS) {
    for (TextLine &line : lines)
      addEllipsis(font, line, maxWidth);
  }
  return lines;
}

/* ---------------------------------------------------------------------
   Font files
   --------------------------------------------------------------------- */

void putU16(QByteArray &data, int offset, quint16 value) {
  qToBigEndian(value, data.data() + offset);
}

void putU32(QByteArray &data, int offset, quint32 value) {
  qToBigEndian(value, data.data() + offset);
}

quint32 tableChecksum(const QByteArray &data) {
  quint32 sum = 0;
  for (int offset = 0; offset < data.size(); offset += 4) {
    quint32 word = 0;
    for (int i = 0; i < 4; i++)
      word = (word << 8) | (offset + i < data.size() ? quint8(data[offset + i]) : 0);
    sum += word;
  }
  return sum;
}

// A font file made of the tables of the font (Qt gives no access to the file
// itself), for GetFontData with table 0.
QByteArray assembleFontFile(const QRawFont &raw) {
  static const char *const tags[] = {
      "BASE", "CFF ", "COLR", "CPAL", "EBDT", "EBLC", "EBSC", "GDEF", "GPOS", "GSUB", "HVAR", "JSTF", "LTSH",
      "MATH", "MVAR", "OS/2", "PCLT", "STAT", "VDMX", "VORG", "avar", "cmap", "cvt ", "fpgm", "fvar", "gasp",
      "glyf", "gvar", "hdmx", "head", "hhea", "hmtx", "kern", "loca", "maxp", "meta", "name", "post", "prep",
      "vhea", "vmtx"};
  std::vector<std::pair<QByteArray, QByteArray>> tables;
  for (const char *tag : tags) {
    QByteArray data = raw.fontTable(tag);
    if (!data.isEmpty())
      tables.emplace_back(QByteArray(tag), std::move(data));
  }
  if (tables.empty())
    return QByteArray();
  std::sort(tables.begin(), tables.end());

  const int count = int(tables.size());
  int selector = 0;
  while ((2 << selector) <= count)
    selector++;
  const int searchRange = (1 << selector) * 16;

  QByteArray file(12 + 16 * count, 0);
  const bool cff = std::any_of(tables.begin(), tables.end(), [](const auto &t) { return t.first == "CFF "; });
  putU32(file, 0, cff ? 0x4F54544F : 0x00010000);
  putU16(file, 4, quint16(count));
  putU16(file, 6, quint16(searchRange));
  putU16(file, 8, quint16(selector));
  putU16(file, 10, quint16(count * 16 - searchRange));

  int headOffset = -1;
  for (int i = 0; i < count; i++) {
    QByteArray data = tables[i].second;
    const int length = int(data.size());
    if (tables[i].first == "head" && length >= 12) {
      headOffset = int(file.size());
      putU32(data, 8, 0);
    }
    data.append(QByteArray((4 - length % 4) % 4, 0));
    const int entry = 12 + 16 * i;
    std::memcpy(file.data() + entry, tables[i].first.constData(), 4);
    putU32(file, entry + 4, tableChecksum(data));
    putU32(file, entry + 8, quint32(file.size()));
    putU32(file, entry + 12, quint32(length));
    file.append(data);
  }
  if (headOffset >= 0)
    putU32(file, headOffset + 8, 0xB1B0AFBA - tableChecksum(file));
  return file;
}

} // namespace

/* ---------------------------------------------------------------------
   Internal interface
   --------------------------------------------------------------------- */

QString meos_qt::substituteFamily(const std::wstring &faceName, BYTE pitchAndFamily) {
  std::wstring key = lowerCase(faceName);
  if (key.empty()) {
    const int family = pitchAndFamily & 0xF0;
    key = family == FF_ROMAN ? L"times new roman" : family == FF_MODERN ? L"courier new" : L"arial";
  }
  const auto entry = substitutes().find(key);
  if (entry != substitutes().end()) {
    for (const char *candidate : entry->second) {
      const QString family = QString::fromLatin1(candidate);
      if (isInstalled(family))
        return family;
    }
  }
  return faceName.empty() ? QString::fromStdWString(key) : QString::fromStdWString(faceName);
}

std::shared_ptr<Font> meos_qt::createFont(const LOGFONT &logFont) {
  auto result = std::make_shared<Font>();
  result->logFont = logFont;
  result->logFont.lfFaceName[LF_FACESIZE - 1] = 0;
  const std::wstring face(result->logFont.lfFaceName);

  QFont font(substituteFamily(face, logFont.lfPitchAndFamily));
  const int weight = logFont.lfWeight <= 0 ? FW_NORMAL : std::min<int>(logFont.lfWeight, 1000);
  font.setWeight(QFont::Weight(weight));
  font.setItalic(logFont.lfItalic != 0);
  font.setUnderline(logFont.lfUnderline != 0);
  font.setStrikeOut(logFont.lfStrikeOut != 0);
  // GDI applies neither kerning nor ligatures, and hints advances to whole pixels.
  font.setKerning(false);
  font.setHintingPreference(QFont::PreferFullHinting);
  int strategy = QFont::PreferNoShaping;
  if (logFont.lfQuality == nonAntialiasedQuality)
    strategy |= QFont::NoAntialias;
  font.setStyleStrategy(QFont::StyleStrategy(strategy));
  font.setPixelSize(100);

  // A positive height is the cell height (ascent + descent), a negative one the
  // em height, as for GDI; zero is the default cell height. Fonts with a VDMX
  // table give the size and the cell; otherwise both follow from the OS/2 table.
  // A substitute for a known missing face gets the cell proportions of the
  // original, so that sizes and line heights stay those of Windows.
  const DesignMetrics design = designMetrics(font);
  const int cellUnits = design.winAscent + design.winDescent;
  const int height = logFont.lfHeight ? logFont.lfHeight : 16;
  const std::wstring lowerFace = lowerCase(face);
  const bool substituted = !lowerFace.empty() && lowerCase(font.family().toStdWString()) != lowerFace;
  const OriginalFace *original = substituted ? originalFace(lowerFace) : nullptr;
  int em = 0;
  int ascent = 0;
  int descent = 0;
  if (original) {
    em = height < 0 ? -height : original->emForCell(height);
    const int cell = original->cellForEm(em);
    ascent = qRound(cell * qreal(design.winAscent) / cellUnits);
    descent = cell - ascent;
    if (!original->hasBold && weight >= 600) {
      // GDI emboldens the regular style and widens every character by a pixel.
      font.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    }
  }
  else if (!lookupVdmx(design.vdmx, height, em, ascent, descent)) {
    em = height < 0 ? -height : qRound(height * design.unitsPerEm / cellUnits);
    em = std::max(em, 1);
    ascent = qRound(design.winAscent * em / design.unitsPerEm);
    descent = qRound(design.winDescent * em / design.unitsPerEm);
  }
  font.setPixelSize(em);
  result->font = font;

  const qreal scale = em / design.unitsPerEm;
  FontMetrics &m = result->metrics;
  m.em = em;
  m.ascent = ascent;
  m.descent = descent;
  m.height = m.ascent + m.descent;
  m.internalLeading = m.height - em;
  const int lineUnits = design.lineAscent - design.lineDescent;
  m.externalLeading = std::max(0, qRound((design.lineGap - (cellUnits - lineUnits)) * scale));
  m.avgCharWidth = design.avgCharWidth > 0 ? qRound(design.avgCharWidth * scale)
                                           : QFontMetrics(font).averageCharWidth();
  m.maxCharWidth = qRound(design.boundsWidth * scale);
  result->fixedPitch = QFontInfo(font).fixedPitch();
  return result;
}

std::shared_ptr<Font> meos_qt::createStockFont(int index) {
  LOGFONT logFont = {};
  const wchar_t *face = nullptr;
  switch (index) {
  case 10: // OEM_FIXED_FONT
    logFont.lfHeight = 12;
    face = L"Terminal";
    break;
  case ANSI_FIXED_FONT:
    logFont.lfHeight = 13;
    face = L"Courier";
    break;
  case 12: // ANSI_VAR_FONT
    logFont.lfHeight = 13;
    face = L"MS Sans Serif";
    break;
  case SYSTEM_FONT:
  case 14: // DEVICE_DEFAULT_FONT
    logFont.lfHeight = 16;
    logFont.lfWeight = FW_BOLD;
    face = L"System";
    break;
  case 16: // SYSTEM_FIXED_FONT
    logFont.lfHeight = 15;
    face = L"Fixedsys";
    break;
  case DEFAULT_GUI_FONT:
    logFont.lfHeight = -11;
    face = L"MS Shell Dlg";
    break;
  default:
    return nullptr;
  }
  std::wcsncpy(logFont.lfFaceName, face, LF_FACESIZE - 1);
  return createFont(logFont);
}

/* ---------------------------------------------------------------------
   Fonts and text
   --------------------------------------------------------------------- */

HFONT CreateFont(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                 DWORD underline, DWORD strikeOut, DWORD charSet, DWORD outPrecision, DWORD clipPrecision,
                 DWORD quality, DWORD pitchAndFamily, LPCWSTR faceName) {
  LOGFONT logFont = {};
  logFont.lfHeight = height;
  logFont.lfWidth = width; // Not applied: MeOS always passes 0.
  logFont.lfEscapement = escapement;
  logFont.lfOrientation = orientation;
  logFont.lfWeight = weight;
  logFont.lfItalic = BYTE(italic);
  logFont.lfUnderline = BYTE(underline);
  logFont.lfStrikeOut = BYTE(strikeOut);
  logFont.lfCharSet = BYTE(charSet);
  logFont.lfOutPrecision = BYTE(outPrecision);
  logFont.lfClipPrecision = BYTE(clipPrecision);
  logFont.lfQuality = BYTE(quality);
  logFont.lfPitchAndFamily = BYTE(pitchAndFamily);
  if (faceName)
    std::wcsncpy(logFont.lfFaceName, faceName, LF_FACESIZE - 1);
  return static_cast<HFONT>(meos_qt::registerGdiObject(meos_qt::createFont(logFont)));
}

int EnumFontFamiliesEx(HDC /*dc*/, LPLOGFONT logFont, FONTENUMPROC enumProc, LPARAM data, DWORD /*flags*/) {
  if (!logFont || !enumProc)
    return 0;
  QStringList families;
  if (logFont->lfFaceName[0]) {
    const QString family = QString::fromWCharArray(logFont->lfFaceName);
    if (isInstalled(family))
      families << family;
  }
  else {
    families = QFontDatabase::families();
  }

  int result = 1;
  for (const QString &family : families) {
    if (QFontDatabase::isPrivateFamily(family))
      continue;
    // Windows reports the metrics of an em height of 32 pixels.
    LOGFONT entry = {};
    entry.lfHeight = -32;
    entry.lfWeight = FW_NORMAL;
    const std::wstring name = family.left(LF_FACESIZE - 1).toStdWString();
    std::wcsncpy(entry.lfFaceName, name.c_str(), LF_FACESIZE - 1);
    const std::shared_ptr<Font> font = meos_qt::createFont(entry);
    const TEXTMETRIC metric = textMetricOf(*font);
    entry.lfHeight = metric.tmHeight;
    entry.lfWidth = metric.tmAveCharWidth;
    entry.lfPitchAndFamily = BYTE(font->fixedPitch ? 1 : 2); // FIXED_PITCH, VARIABLE_PITCH
    entry.lfOutPrecision = 3;                                 // OUT_STROKE_PRECIS
    entry.lfClipPrecision = 2;                                // CLIP_STROKE_PRECIS
    entry.lfQuality = 1;                                      // DRAFT_QUALITY
    result = enumProc(&entry, &metric, TRUETYPE_FONTTYPE, data);
    if (!result)
      break;
  }
  return result;
}

BOOL TextOut(HDC dc, int x, int y, LPCWSTR text, int length) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context || length < 0 || (!text && length > 0))
    return FALSE;
  const std::shared_ptr<Font> font = context->font;
  const QString line = toQString(text, length);
  const int width = textWidth(*font, line);
  meos_qt::paint(*context, lineArea(*font, x, y, width),
                 [&](QPainter &painter) { drawTextLine(painter, *context, *font, x, y, line, width, -1); });
  return TRUE;
}

int DrawText(HDC dc, LPCWSTR text, int length, LPRECT rect, UINT format) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context || !text || !rect)
    return 0;
  if (length < 0)
    length = int(std::wcslen(text));
  const std::shared_ptr<Font> font = context->font;
  const int lineHeight = font->metrics.height;

  if (length == 0) {
    if (format & DT_CALCRECT) {
      rect->right = rect->left;
      rect->bottom = rect->top + ((format & DT_SINGLELINE) ? lineHeight : 0);
    }
    return 0;
  }

  const int maxWidth = rect->right - rect->left;
  const std::vector<TextLine> lines = layoutText(*font, toQString(text, length), format, maxWidth);
  const int height = int(lines.size()) * lineHeight;
  if (format & DT_CALCRECT) {
    int widest = 0;
    for (const TextLine &line : lines)
      widest = std::max(widest, line.width);
    // The left and top edges stay.
    rect->right = rect->left + widest;
    rect->bottom = rect->top + height;
    return height;
  }

  std::vector<QPoint> origins;
  QRect area;
  int y = rect->top;
  for (const TextLine &line : lines) {
    int x = rect->left;
    if (format & DT_CENTER)
      x += (maxWidth - line.width) / 2;
    else if (format & DT_RIGHT)
      x = rect->right - line.width;
    origins.emplace_back(x, y);
    area |= lineArea(*font, x, y, line.width);
    y += lineHeight;
  }
  const QRect bounds = meos_qt::toQRect(*rect);
  if (!(format & DT_NOCLIP))
    area &= bounds;

  meos_qt::paint(*context, area, [&](QPainter &painter) {
    if (!(format & DT_NOCLIP))
      painter.setClipRegion(context->clipped ? context->clip & bounds : QRegion(bounds));
    for (std::size_t i = 0; i < lines.size(); i++)
      drawTextLine(painter, *context, *font, origins[i].x(), origins[i].y(), lines[i].text, lines[i].width,
                   lines[i].underline);
  });
  return height;
}

BOOL GetTextExtentPoint32(HDC dc, LPCWSTR text, int length, LPSIZE size) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context || !size || length < 0 || (!text && length > 0))
    return FALSE;
  size->cx = textWidth(*context->font, toQString(text, length));
  size->cy = context->font->metrics.height;
  return TRUE;
}

BOOL GetTextExtentPoint32A(HDC dc, LPCSTR text, int length, LPSIZE size) {
  if (length < 0 || (!text && length > 0))
    return FALSE;
  std::wstring wide(std::size_t(length), L'\0');
  const int converted = length ? MultiByteToWideChar(CP_ACP, 0, text, length, &wide[0], length) : 0;
  wide.resize(std::size_t(std::max(converted, 0)));
  return GetTextExtentPoint32(dc, wide.c_str(), int(wide.size()), size);
}

DWORD GetFontData(HDC dc, DWORD table, DWORD offset, LPVOID buffer, DWORD size) {
  const std::shared_ptr<DeviceContext> context = meos_qt::findDc(dc);
  if (!context)
    return GDI_ERROR;
  const QRawFont raw = QRawFont::fromFont(context->font->font);
  if (!raw.isValid())
    return GDI_ERROR;

  QByteArray data;
  if (table == 0) {
    data = assembleFontFile(raw);
  }
  else {
    // The tag's first character is the lowest byte.
    const char tag[5] = {char(table & 0xFF), char((table >> 8) & 0xFF), char((table >> 16) & 0xFF),
                         char((table >> 24) & 0xFF), 0};
    data = raw.fontTable(tag);
  }
  if (data.isEmpty() || offset > DWORD(data.size()))
    return GDI_ERROR;
  const DWORD available = DWORD(data.size()) - offset;
  if (!buffer || size == 0)
    return available;
  const DWORD count = std::min(size, available);
  std::memcpy(buffer, data.constData() + offset, count);
  return count;
}
