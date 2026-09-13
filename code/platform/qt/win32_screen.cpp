/************************************************************************
    MeOS - Orienteering Software
    Linux port: system colours (monitors, metrics and cursors follow in
    step 1.2.5).

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

#include "win32_ui.h"

#include <array>

// The default colours of Windows 10, not those of the desktop theme: MeOS mixes
// system colours with fixed colours of its own, which a dark theme would make
// unreadable.
DWORD GetSysColor(int index) {
  static const std::array<COLORREF, 31> colors = {
      RGB(200, 200, 200), // COLOR_SCROLLBAR
      RGB(0, 0, 0),       // COLOR_BACKGROUND
      RGB(153, 180, 209), // COLOR_ACTIVECAPTION
      RGB(191, 205, 219), // COLOR_INACTIVECAPTION
      RGB(240, 240, 240), // COLOR_MENU
      RGB(255, 255, 255), // COLOR_WINDOW
      RGB(100, 100, 100), // COLOR_WINDOWFRAME
      RGB(0, 0, 0),       // COLOR_MENUTEXT
      RGB(0, 0, 0),       // COLOR_WINDOWTEXT
      RGB(0, 0, 0),       // COLOR_CAPTIONTEXT
      RGB(180, 180, 180), // COLOR_ACTIVEBORDER
      RGB(244, 247, 252), // COLOR_INACTIVEBORDER
      RGB(171, 171, 171), // COLOR_APPWORKSPACE
      RGB(0, 120, 215),   // COLOR_HIGHLIGHT
      RGB(255, 255, 255), // COLOR_HIGHLIGHTTEXT
      RGB(240, 240, 240), // COLOR_3DFACE
      RGB(160, 160, 160), // COLOR_3DSHADOW
      RGB(109, 109, 109), // COLOR_GRAYTEXT
      RGB(0, 0, 0),       // COLOR_BTNTEXT
      RGB(0, 0, 0),       // COLOR_INACTIVECAPTIONTEXT
      RGB(255, 255, 255), // COLOR_3DHIGHLIGHT
      RGB(105, 105, 105), // COLOR_3DDKSHADOW
      RGB(227, 227, 227), // COLOR_3DLIGHT
      RGB(0, 0, 0),       // COLOR_INFOTEXT
      RGB(255, 255, 225), // COLOR_INFOBK
      RGB(0, 0, 0),       // (unused)
      RGB(0, 102, 204),   // COLOR_HOTLIGHT
      RGB(185, 209, 234), // COLOR_GRADIENTACTIVECAPTION
      RGB(215, 228, 242), // COLOR_GRADIENTINACTIVECAPTION
      RGB(51, 153, 255),  // COLOR_MENUHILIGHT
      RGB(240, 240, 240), // COLOR_MENUBAR
  };
  if (index < 0 || index >= int(colors.size()))
    return 0;
  return colors[std::size_t(index)];
}
