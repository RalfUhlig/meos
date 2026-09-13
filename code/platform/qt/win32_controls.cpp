/************************************************************************
    MeOS - Orienteering Software
    Linux port: the standard controls BUTTON, EDIT, COMBOBOX, LISTBOX and
    STATIC on Qt widgets.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// Each control class has a built-in window procedure that handles its messages,
// so that subclassing with GWLP_WNDPROC and CallWindowProc works as on Windows.
// The Qt widget is the client of the window, inside a frame that draws the
// border. The semantics follow Windows (checked against the Wine sources, which
// are tested against Windows), not Qt:
//
// - Changes made with messages send no notifications (BM_SETCHECK, CB_SETCURSEL,
//   LB_SETCURSEL, ...). WM_SETTEXT on a single-line edit is the exception: it
//   sends EN_CHANGE.
// - Notifications go to the parent as WM_COMMAND(MAKEWPARAM(id, code), control).
// - Key, mouse wheel and scroll bar input passes the procedure chain as
//   WM_KEYDOWN/WM_KEYUP, WM_MOUSEWHEEL and WM_VSCROLL; the built-in procedure
//   performs the Qt processing when the message reaches it. WM_CHAR and mouse
//   button messages are not sent to control procedures.

#include "win32_gdi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProxyStyle>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QTextCursor>
#include <QWheelEvent>

#include "commctrl.h"

namespace {

using meos_qt::Control;
using meos_qt::Window;

constexpr LRESULT cbOkay = 0;
// The largest text length of an edit control (EM_LIMITTEXT with 0).
constexpr int maxEditLength = 0x7FFFFFFE;

std::shared_ptr<Window> windowOf(const QWidget *widget) {
  return meos_qt::findWindow(meos_qt::windowFromWidget(widget));
}

QString toQString(LPCWSTR text) {
  return text ? QString::fromWCharArray(text) : QString();
}

// Copies text for WM_GETTEXT: at most size - 1 characters and a terminating zero.
LRESULT copyText(const QString &text, WPARAM size, LPARAM buffer) {
  const auto target = reinterpret_cast<LPWSTR>(buffer);
  if (!target || size == 0)
    return 0;
  const std::wstring wide = text.toStdWString();
  const std::size_t count = std::min<std::size_t>(wide.size(), size - 1);
  std::wmemcpy(target, wide.c_str(), count);
  target[count] = 0;
  return LRESULT(count);
}

// Copies the whole text of an item (CB_GETLBTEXT, LB_GETTEXT) and returns its length.
LRESULT copyItemText(const QString &text, LPARAM buffer) {
  const std::wstring wide = text.toStdWString();
  if (const auto target = reinterpret_cast<LPWSTR>(buffer)) {
    std::wmemcpy(target, wide.c_str(), wide.size());
    target[wide.size()] = 0;
  }
  return LRESULT(wide.size());
}

LRESULT textLength(const QString &text) {
  return LRESULT(text.toStdWString().size());
}

std::shared_ptr<meos_qt::Font> fontOf(HFONT font) {
  return meos_qt::findGdiObject<meos_qt::Font>(font, meos_qt::GdiType::Font);
}

// MulDiv: rounded, halves away from zero.
int mulDiv(int number, int numerator, int denominator) {
  const long long product = static_cast<long long>(number) * numerator;
  const long long half = denominator / 2;
  return int(product >= 0 ? (product + half) / denominator : (product - half) / denominator);
}

HFONT systemFont() {
  return static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
}

// The first item at or after start + 1 (wrapping around) whose text begins with
// text, ignoring case (CB_FINDSTRING, LB_FINDSTRING).
int findPrefix(int count, int start, const QString &text, const std::function<QString(int)> &itemText) {
  if (count <= 0)
    return -1;
  const int first = (start < 0 || start >= count) ? 0 : start + 1;
  for (int k = 0; k < count; k++) {
    const int index = (first + k) % count;
    if (itemText(index).startsWith(text, Qt::CaseInsensitive))
      return index;
  }
  return -1;
}

/* ---------------------------------------------------------------------
   Input that passes the window procedure chain
   --------------------------------------------------------------------- */

struct PendingInput {
  HWND window;
  UINT message;
  std::function<void()> perform;
  bool done;
};
std::vector<PendingInput *> pendingInputs;

// Sends the message that stands for a Qt input event to the current window
// procedure. The built-in procedure calls performInput, which runs the Qt
// processing; a subclass procedure that swallows the message suppresses it.
void sendInput(QWidget *source, UINT message, WPARAM wParam, LPARAM lParam, std::function<void()> perform) {
  const std::shared_ptr<Window> window = windowOf(source);
  if (!window) {
    if (perform)
      perform();
    return;
  }
  PendingInput pending{window->handle, message, std::move(perform), false};
  pendingInputs.push_back(&pending);
  meos_qt::callWindowProc(window, message, wParam, lParam);
  pendingInputs.erase(std::find(pendingInputs.begin(), pendingInputs.end(), &pending));
}

// Returns false for a message that did not come from a Qt input event (sent by
// the application).
bool performInput(HWND window, UINT message) {
  for (auto it = pendingInputs.rbegin(); it != pendingInputs.rend(); ++it) {
    PendingInput &pending = **it;
    if (pending.window == window && pending.message == message && !pending.done) {
      pending.done = true;
      if (pending.perform)
        pending.perform();
      return true;
    }
  }
  return false;
}

// Base of the control widgets: keys and the mouse wheel pass the window
// procedure, keys stay with the control (Qt would pass unused keys to the parent
// window), and Qt does not move the focus with Tab.
template <typename Base>
class ControlWidget : public Base {
public:
  explicit ControlWidget(QWidget *parent) : Base(parent) {
    this->setMouseTracking(true);
  }

protected:
  bool event(QEvent *event) override {
    const QEvent::Type type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
      return Base::event(event);

    const auto *key = static_cast<QKeyEvent *>(event);
    const int virtualKey = meos_qt::virtualKey(*key);
    if (virtualKey && !(type == QEvent::KeyRelease && key->isAutoRepeat())) {
      sendInput(this, type == QEvent::KeyPress ? WM_KEYDOWN : WM_KEYUP, WPARAM(virtualKey),
                meos_qt::keyMessageParam(*key), [this, event] { this->Base::event(event); });
    }
    else {
      Base::event(event);
    }
    event->accept();
    return true;
  }

  void wheelEvent(QWheelEvent *event) override {
    const QPoint global = event->globalPosition().toPoint();
    sendInput(this, WM_MOUSEWHEEL,
              MAKEWPARAM(meos_qt::mouseKeyFlags(event->buttons(), event->modifiers()), event->angleDelta().y()),
              MAKELPARAM(global.x(), global.y()), [this, event] { this->Base::wheelEvent(event); });
    event->accept();
  }

  bool focusNextPrevChild(bool) override { return false; }
};

// Messages common to all control procedures. Returns true if handled.
bool commonMessage(Window &window, UINT message, WPARAM wParam, LRESULT &result) {
  switch (message) {
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_MOUSEWHEEL:
    performInput(window.handle, message);
    result = 0;
    return true;
  case WM_SETREDRAW:
    if (window.client)
      window.client->setUpdatesEnabled(wParam != 0);
    result = 0;
    return true;
  }
  return false;
}

/* ---------------------------------------------------------------------
   BUTTON: push buttons and check boxes
   --------------------------------------------------------------------- */

class ButtonControl : public Control {
public:
  HFONT font = nullptr;
  HBITMAP image = nullptr;
};

template <typename Base>
class ButtonWidget : public ControlWidget<Base> {
public:
  using ControlWidget<Base>::ControlWidget;
  bool autoCheck = false;

protected:
  // Only BS_AUTOCHECKBOX changes its state when clicked; BS_CHECKBOX leaves that
  // to the application.
  void nextCheckState() override {
    if (autoCheck)
      Base::nextCheckState();
  }
};

int buttonType(const Window &window) {
  return int(window.style & BS_TYPEMASK);
}

bool isCheckBox(const Window &window) {
  return buttonType(window) == BS_CHECKBOX || buttonType(window) == BS_AUTOCHECKBOX;
}

void showButtonText(const Window &window, const ButtonControl &control) {
  auto *button = qobject_cast<QAbstractButton *>(window.client.data());
  if (!button)
    return;
  // A BS_BITMAP button shows its image only.
  const bool imageOnly = (window.style & BS_BITMAP) != 0;
  button->setText(imageOnly ? QString() : QString::fromStdWString(window.text));
  (void)control;
}

void setWidgetFont(QWidget *widget, HFONT font) {
  if (const std::shared_ptr<meos_qt::Font> object = fontOf(font))
    widget->setFont(object->font);
}

LRESULT CALLBACK buttonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> window = meos_qt::findWindow(hwnd);
  const auto control = window ? std::dynamic_pointer_cast<ButtonControl>(window->control) : nullptr;
  auto *button = window ? qobject_cast<QAbstractButton *>(window->client.data()) : nullptr;
  if (!control || !button)
    return DefWindowProc(hwnd, message, wParam, lParam);

  LRESULT result = 0;
  if (commonMessage(*window, message, wParam, result))
    return result;

  switch (message) {
  case BM_GETCHECK:
    return isCheckBox(*window) && button->isChecked() ? BST_CHECKED : BST_UNCHECKED;
  case BM_SETCHECK:
    if (isCheckBox(*window)) {
      const QSignalBlocker blocker(button);
      button->setChecked(wParam == BST_CHECKED);
    }
    return 0;
  case BM_GETIMAGE:
    return wParam == IMAGE_BITMAP ? reinterpret_cast<LRESULT>(control->image) : 0;
  case BM_SETIMAGE: {
    if (wParam != IMAGE_BITMAP)
      return 0;
    const HBITMAP previous = control->image;
    control->image = reinterpret_cast<HBITMAP>(lParam);
    const QPixmap pixmap = meos_qt::bitmapPixmap(control->image);
    button->setIcon(pixmap.isNull() ? QIcon() : QIcon(pixmap));
    if (!pixmap.isNull())
      button->setIconSize(pixmap.deviceIndependentSize().toSize());
    return reinterpret_cast<LRESULT>(previous);
  }
  case BCM_GETIDEALSIZE:
    if (const auto size = reinterpret_cast<LPSIZE>(lParam)) {
      const QSize hint = button->sizeHint();
      size->cx = hint.width();
      size->cy = hint.height();
      return TRUE;
    }
    return FALSE;
  case WM_SETTEXT:
    DefWindowProc(hwnd, message, wParam, lParam);
    showButtonText(*window, *control);
    return TRUE;
  case WM_SETFONT:
    control->font = reinterpret_cast<HFONT>(wParam);
    setWidgetFont(button, control->font);
    return 0;
  case WM_GETFONT:
    return reinterpret_cast<LRESULT>(control->font);
  case WM_SETFOCUS:
    if (window->style & BS_NOTIFY)
      meos_qt::notifyParent(*window, BN_SETFOCUS);
    return 0;
  case WM_KILLFOCUS:
    if (window->style & BS_NOTIFY)
      meos_qt::notifyParent(*window, BN_KILLFOCUS);
    return 0;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

void createButton(Window &window, QWidget *parentWidget) {
  QWidget *frame = meos_qt::createFrame(window, parentWidget);
  auto control = std::make_shared<ButtonControl>();
  const bool checkBox = isCheckBox(window);
  const bool autoCheck = buttonType(window) == BS_AUTOCHECKBOX;

  QAbstractButton *button;
  if (checkBox && !(window.style & BS_PUSHLIKE)) {
    auto *widget = new ButtonWidget<QCheckBox>(frame);
    widget->autoCheck = autoCheck;
    button = widget;
  }
  else {
    auto *widget = new ButtonWidget<QPushButton>(frame);
    widget->autoCheck = autoCheck;
    widget->setCheckable(checkBox);
    button = widget;
  }
  button->setFocusPolicy(Qt::StrongFocus);
  window.client = button;
  window.control = control;
  control->font = systemFont();
  setWidgetFont(button, control->font);
  showButtonText(window, *control);

  // Clicks with the mouse or the space bar.
  QObject::connect(button, &QAbstractButton::clicked, button, [button] {
    if (const std::shared_ptr<Window> target = windowOf(button))
      meos_qt::notifyParent(*target, BN_CLICKED);
  });
}

/* ---------------------------------------------------------------------
   EDIT: single-line (QLineEdit) and multi-line (QPlainTextEdit)
   --------------------------------------------------------------------- */

class EditControl : public Control {
public:
  QPointer<QLineEdit> line;
  QPointer<QPlainTextEdit> multi;
  HFONT font = nullptr;
  // Changes made by messages that send no EN_CHANGE (or send it themselves).
  int silent = 0;
};

// EM_SETPASSWORDCHAR: Qt takes the password character from the style.
class PasswordStyle : public QProxyStyle {
public:
  PasswordStyle(wchar_t character, QObject *owner)
      : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))), character(character) {
    setParent(owner);
  }

  int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                QStyleHintReturn *returnData) const override {
    if (hint == SH_LineEdit_PasswordCharacter)
      return int(character);
    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }

private:
  const wchar_t character;
};

// WM_CTLCOLOREDIT: Windows asks the parent for the colours whenever the edit
// control paints; the answer becomes the palette of the widget.
void queryEditColors(QWidget *widget) {
  const std::shared_ptr<Window> window = windowOf(widget);
  if (!window || (window->style & WS_DISABLED))
    return;
  const std::shared_ptr<Window> parent = meos_qt::findWindow(window->parent);
  if (!parent)
    return;

  QColor base = meos_qt::toQColor(GetSysColor(COLOR_WINDOW));
  QColor text = meos_qt::toQColor(GetSysColor(COLOR_WINDOWTEXT));
  const HDC dc = CreateCompatibleDC(nullptr);
  const LRESULT brush = meos_qt::callWindowProc(parent, WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                                                reinterpret_cast<LPARAM>(window->handle));
  // Without a brush, DefWindowProc's colours apply.
  if (brush) {
    if (const std::shared_ptr<meos_qt::DeviceContext> context = meos_qt::findDc(dc))
      text = meos_qt::toQColor(context->textColor);
    const auto handle = reinterpret_cast<HGDIOBJ>(brush);
    if (const auto object = meos_qt::findGdiObject<meos_qt::Brush>(handle, meos_qt::GdiType::Brush)) {
      if (!object->null) {
        const std::shared_ptr<meos_qt::DeviceContext> context = meos_qt::findDc(dc);
        base = meos_qt::toQColor(object->dcBrush && context ? context->dcBrushColor : object->color);
      }
    }
    else if (std::uintptr_t(brush) > 0 && std::uintptr_t(brush) <= 31) {
      base = meos_qt::toQColor(GetSysColor(int(brush) - 1)); // system colour index + 1
    }
  }
  DeleteDC(dc);

  QPalette palette = widget->palette();
  if (palette.color(QPalette::Active, QPalette::Base) == base && palette.color(QPalette::Active, QPalette::Text) == text)
    return;
  for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
    palette.setColor(group, QPalette::Base, base);
    palette.setColor(group, QPalette::Text, text);
  }
  widget->setPalette(palette);
}

template <typename Base>
class EditWidget : public ControlWidget<Base> {
public:
  using ControlWidget<Base>::ControlWidget;

protected:
  void paintEvent(QPaintEvent *event) override {
    queryEditColors(this);
    Base::paintEvent(event);
  }
};

QString editText(const EditControl &control) {
  if (control.line)
    return control.line->text();
  if (control.multi) {
    QString text = control.multi->toPlainText();
    text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    return text;
  }
  return QString();
}

LRESULT CALLBACK editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> window = meos_qt::findWindow(hwnd);
  const auto control = window ? std::dynamic_pointer_cast<EditControl>(window->control) : nullptr;
  if (!control || (!control->line && !control->multi))
    return DefWindowProc(hwnd, message, wParam, lParam);
  QLineEdit *line = control->line;
  QPlainTextEdit *multi = control->multi;

  LRESULT result = 0;
  if (commonMessage(*window, message, wParam, result))
    return result;

  switch (message) {
  case WM_SETTEXT: {
    DefWindowProc(hwnd, message, wParam, lParam);
    const QString text = toQString(reinterpret_cast<LPCWSTR>(lParam));
    control->silent++;
    if (line) {
      line->setText(text);
      line->setCursorPosition(0);
    }
    else {
      QString plain = text;
      plain.remove(QLatin1Char('\r'));
      multi->setPlainText(plain);
    }
    control->silent--;
    // Only single-line edit controls notify.
    if (line)
      meos_qt::notifyParent(*window, EN_CHANGE);
    return TRUE;
  }
  case WM_GETTEXT:
    return copyText(editText(*control), wParam, lParam);
  case WM_GETTEXTLENGTH:
    return textLength(editText(*control));

  case EM_SETSEL: {
    // Positions are unsigned; -1 as start removes the selection, larger values
    // are clamped to the text length. The caret goes to the end position.
    const int length = line ? int(line->text().size()) : int(multi->document()->characterCount() - 1);
    if (UINT(wParam) == UINT(-1)) {
      if (line)
        line->deselect();
      else {
        QTextCursor cursor = multi->textCursor();
        cursor.clearSelection();
        multi->setTextCursor(cursor);
      }
      return 1;
    }
    const int start = int(std::min<UINT>(UINT(wParam), UINT(length)));
    const int end = int(std::min<UINT>(UINT(lParam), UINT(length)));
    if (line) {
      if (start == end)
        line->setCursorPosition(end);
      else
        line->setSelection(start, end - start);
    }
    else {
      QTextCursor cursor = multi->textCursor();
      cursor.setPosition(start);
      cursor.setPosition(end, QTextCursor::KeepAnchor);
      multi->setTextCursor(cursor);
    }
    return 1;
  }
  case EM_GETSEL: {
    int start, end;
    if (line) {
      if (line->hasSelectedText()) {
        start = line->selectionStart();
        end = line->selectionEnd();
      }
      else {
        start = end = line->cursorPosition();
      }
    }
    else {
      const QTextCursor cursor = multi->textCursor();
      start = cursor.selectionStart();
      end = cursor.selectionEnd();
    }
    if (const auto p = reinterpret_cast<LPDWORD>(wParam))
      *p = DWORD(start);
    if (const auto p = reinterpret_cast<LPDWORD>(lParam))
      *p = DWORD(end);
    return MAKELRESULT(std::min(start, 0xFFFF), std::min(end, 0xFFFF));
  }
  case EM_REPLACESEL: {
    const QString text = toQString(reinterpret_cast<LPCWSTR>(lParam));
    if (line)
      line->insert(text);
    else
      multi->insertPlainText(QString(text).remove(QLatin1Char('\r')));
    return 0;
  }
  case EM_LIMITTEXT:
    if (line)
      line->setMaxLength(wParam == 0 ? maxEditLength : int(std::min<WPARAM>(wParam, maxEditLength)));
    return 0;
  case EM_SETPASSWORDCHAR:
    if (line) {
      const auto character = wchar_t(wParam);
      if (character) {
        window->style |= ES_PASSWORD;
        line->setStyle(new PasswordStyle(character, line));
        line->setEchoMode(QLineEdit::Password);
      }
      else {
        window->style &= ~ES_PASSWORD;
        line->setEchoMode(QLineEdit::Normal);
      }
      line->update();
    }
    return 0;
  case WM_PASTE:
    if (line)
      line->paste();
    else
      multi->paste();
    return 0;
  case WM_SETFONT:
    control->font = reinterpret_cast<HFONT>(wParam);
    control->silent++;
    setWidgetFont(window->client, control->font);
    control->silent--;
    return 0;
  case WM_GETFONT:
    return reinterpret_cast<LRESULT>(control->font);
  case WM_SETFOCUS:
    meos_qt::notifyParent(*window, EN_SETFOCUS);
    return 0;
  case WM_KILLFOCUS:
    meos_qt::notifyParent(*window, EN_KILLFOCUS);
    return 0;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

void createEdit(Window &window, QWidget *parentWidget) {
  QWidget *frame = meos_qt::createFrame(window, parentWidget);
  auto control = std::make_shared<EditControl>();
  window.control = control;
  control->font = systemFont();
  const QString text = QString::fromStdWString(window.text);

  control->silent++;
  if (window.style & ES_MULTILINE) {
    auto *edit = new EditWidget<QPlainTextEdit>(frame);
    edit->viewport()->setMouseTracking(true);
    edit->setFrameShape(QFrame::NoFrame);
    edit->setTabChangesFocus(false);
    // Without ES_AUTOHSCROLL and WS_HSCROLL, lines wrap at the window edge.
    edit->setLineWrapMode((window.style & (ES_AUTOHSCROLL | WS_HSCROLL)) ? QPlainTextEdit::NoWrap
                                                                          : QPlainTextEdit::WidgetWidth);
    edit->setVerticalScrollBarPolicy((window.style & WS_VSCROLL) ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
    edit->setHorizontalScrollBarPolicy((window.style & WS_HSCROLL) ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
    edit->setPlainText(QString(text).remove(QLatin1Char('\r')));
    control->multi = edit;
    window.client = edit;
    QObject::connect(edit, &QPlainTextEdit::textChanged, edit, [edit] {
      const std::shared_ptr<Window> target = windowOf(edit);
      const auto state = target ? std::dynamic_pointer_cast<EditControl>(target->control) : nullptr;
      if (state && !state->silent)
        meos_qt::notifyParent(*target, EN_CHANGE);
    });
  }
  else {
    auto *edit = new EditWidget<QLineEdit>(frame);
    edit->setFrame(false);
    edit->setMaxLength(maxEditLength);
    edit->setText(text);
    edit->setCursorPosition(0);
    if (window.style & ES_PASSWORD) {
      edit->setStyle(new PasswordStyle(L'\x25CF', edit));
      edit->setEchoMode(QLineEdit::Password);
    }
    control->line = edit;
    window.client = edit;
    QObject::connect(edit, &QLineEdit::textChanged, edit, [edit] {
      const std::shared_ptr<Window> target = windowOf(edit);
      const auto state = target ? std::dynamic_pointer_cast<EditControl>(target->control) : nullptr;
      if (state && !state->silent)
        meos_qt::notifyParent(*target, EN_CHANGE);
    });
  }
  window.client->setFocusPolicy(Qt::StrongFocus);
  setWidgetFont(window.client, control->font);
  control->silent--;
}

/* ---------------------------------------------------------------------
   COMBOBOX: CBS_DROPDOWNLIST (QComboBox) and CBS_DROPDOWN (editable)
   --------------------------------------------------------------------- */

class ComboControl : public Control {
public:
  QPointer<QComboBox> combo;
  HFONT font = nullptr;
  int border = 0;
  // Height of the selection field (text height + 8, as Windows computes it) and
  // of the whole control including the list.
  int fieldHeight = 0;
  int itemHeight = 16;
  mutable int droppedHeight = 0;

  // The window keeps the height of the selection field; the requested height
  // sets the size of the list.
  int windowHeight(int requested) const override {
    droppedHeight = requested;
    return fieldHeight + 2 * border;
  }
};

class ComboWidget : public ControlWidget<QComboBox> {
public:
  using ControlWidget<QComboBox>::ControlWidget;

protected:
  // Enter selects nothing in a closed combo box (Qt would pick a matching item).
  void keyPressEvent(QKeyEvent *event) override {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !view()->isVisible()) {
      event->accept();
      return;
    }
    QComboBox::keyPressEvent(event);
  }
};

// Selects an item without notifications. The edit field of an editable combo box
// shows the item only if showItem is set, and otherwise keeps its text.
void selectComboItem(QComboBox *combo, int index, bool showItem) {
  QLineEdit *edit = combo->lineEdit();
  const QString text = edit ? edit->text() : QString();
  const int cursor = edit ? edit->cursorPosition() : 0;
  {
    const QSignalBlocker blocker(combo);
    combo->setCurrentIndex(index);
  }
  if (edit && !showItem && edit->text() != text) {
    const QSignalBlocker blocker(edit);
    edit->setText(text);
    edit->setCursorPosition(cursor);
  }
}

// Runs a change of the item list and restores what Windows keeps: the selected
// item (moved to expected) and the text of the edit field. Qt selects the first
// item inserted into an empty combo box, and a neighbour of a removed one.
template <typename Change>
void changeComboItems(QComboBox *combo, int expected, Change &&change) {
  QLineEdit *edit = combo->lineEdit();
  const QString text = edit ? edit->text() : QString();
  {
    const QSignalBlocker blocker(combo);
    change();
  }
  if (combo->currentIndex() != expected)
    selectComboItem(combo, expected, false);
  if (edit && edit->text() != text) {
    const QSignalBlocker blocker(edit);
    edit->setText(text);
  }
}

void applyComboFont(const Window &window, ComboControl &control) {
  QComboBox *combo = control.combo;
  const std::shared_ptr<meos_qt::Font> font = fontOf(control.font);
  if (!combo || !font)
    return;
  combo->setFont(font->font);
  combo->view()->setFont(font->font);
  control.fieldHeight = font->metrics.height + 8;
  control.itemHeight = std::max(font->metrics.height, 1);
  (void)window;
}

void updateDroppedHeight(ComboControl &control) {
  if (control.combo) {
    const int listHeight = control.droppedHeight - control.fieldHeight - 2 * control.border;
    control.combo->setMaxVisibleItems(std::max(1, (listHeight - 2) / control.itemHeight));
  }
}

LRESULT CALLBACK comboProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> window = meos_qt::findWindow(hwnd);
  const auto control = window ? std::dynamic_pointer_cast<ComboControl>(window->control) : nullptr;
  QComboBox *combo = control ? control->combo.data() : nullptr;
  if (!combo)
    return DefWindowProc(hwnd, message, wParam, lParam);
  QLineEdit *edit = combo->lineEdit();

  LRESULT result = 0;
  if (commonMessage(*window, message, wParam, result))
    return result;

  const int count = combo->count();
  const int index = int(wParam);
  const bool valid = index >= 0 && index < count;

  switch (message) {
  case CB_ADDSTRING:
  case CB_INSERTSTRING: {
    const int position = (message == CB_ADDSTRING || index == -1) ? count : index;
    if (position < 0 || position > count)
      return CB_ERR;
    const int before = combo->currentIndex();
    const int expected = before < 0 ? -1 : (position <= before ? before + 1 : before);
    const QString text = toQString(reinterpret_cast<LPCWSTR>(lParam));
    changeComboItems(combo, expected, [&] { combo->insertItem(position, text, QVariant(qlonglong(0))); });
    return position;
  }
  case CB_DELETESTRING: {
    if (!valid)
      return CB_ERR;
    const int before = combo->currentIndex();
    const int expected = before == index ? -1 : (index < before ? before - 1 : before);
    changeComboItems(combo, expected, [&] { combo->removeItem(index); });
    return combo->count();
  }
  case CB_RESETCONTENT: {
    {
      const QSignalBlocker blocker(combo);
      combo->clear();
    }
    if (edit) {
      const QSignalBlocker blocker(edit);
      edit->clear();
    }
    return cbOkay;
  }
  case CB_INITSTORAGE:
    return count + LRESULT(wParam);
  case CB_GETCOUNT:
    return count;
  case CB_GETCURSEL:
    return combo->currentIndex() < 0 ? CB_ERR : combo->currentIndex();
  case CB_SETCURSEL:
    if (!valid) {
      selectComboItem(combo, -1, true);
      return CB_ERR;
    }
    selectComboItem(combo, index, true);
    if (edit && combo->hasFocus())
      edit->selectAll();
    return index;
  case CB_GETLBTEXT:
    return valid ? copyItemText(combo->itemText(index), lParam) : CB_ERR;
  case CB_GETLBTEXTLEN:
    return valid ? textLength(combo->itemText(index)) : CB_ERR;
  case CB_GETITEMDATA:
    return valid ? LRESULT(combo->itemData(index).toLongLong()) : CB_ERR;
  case CB_SETITEMDATA:
    if (!valid)
      return CB_ERR;
    combo->setItemData(index, QVariant(qlonglong(lParam)));
    return TRUE;
  case CB_FINDSTRING: {
    const int found = findPrefix(count, index, toQString(reinterpret_cast<LPCWSTR>(lParam)),
                                 [combo](int i) { return combo->itemText(i); });
    return found < 0 ? CB_ERR : found;
  }

  // A drop-down list has no text of its own; its window text is the selected item.
  case WM_SETTEXT:
    if (!edit)
      return CB_ERR;
    DefWindowProc(hwnd, message, wParam, lParam);
    {
      const QSignalBlocker blocker(edit);
      edit->setText(toQString(reinterpret_cast<LPCWSTR>(lParam)));
    }
    return TRUE;
  case WM_GETTEXT:
  case WM_GETTEXTLENGTH: {
    QString text;
    if (edit)
      text = edit->text();
    else if (combo->currentIndex() >= 0)
      text = combo->itemText(combo->currentIndex());
    return message == WM_GETTEXT ? copyText(text, wParam, lParam) : textLength(text);
  }
  case WM_PASTE:
    if (edit)
      edit->paste();
    return 0;
  case WM_SETFONT: {
    control->font = reinterpret_cast<HFONT>(wParam);
    applyComboFont(*window, *control);
    const QRect rect = meos_qt::windowRect(*window);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.width(), control->droppedHeight, SWP_NOMOVE | SWP_NOZORDER);
    updateDroppedHeight(*control);
    return 0;
  }
  case WM_GETFONT:
    return reinterpret_cast<LRESULT>(control->font);
  case WM_SETFOCUS:
    meos_qt::notifyParent(*window, CBN_SETFOCUS);
    return 0;
  case WM_KILLFOCUS:
    combo->hidePopup();
    meos_qt::notifyParent(*window, CBN_KILLFOCUS);
    return 0;
  case WM_WINDOWPOSCHANGED:
    updateDroppedHeight(*control);
    break;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

void createCombo(Window &window, QWidget *parentWidget) {
  QWidget *frame = meos_qt::createFrame(window, parentWidget);
  auto control = std::make_shared<ComboControl>();
  auto *combo = new ComboWidget(frame);
  control->combo = combo;
  control->border = window.isChild() ? meos_qt::borderWidth(window.style, window.exStyle) : 0;
  window.control = control;
  window.client = combo;
  combo->setFocusPolicy(Qt::StrongFocus);

  const bool editable = (window.style & CBS_DROPDOWNLIST) == CBS_DROPDOWN;
  if (editable) {
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setCompleter(nullptr);
    QLineEdit *edit = combo->lineEdit();
    edit->setMaxLength(maxEditLength);
    // Typing removes the selection of the list (Windows selects the matching
    // item only while the list is open).
    QObject::connect(edit, &QLineEdit::textEdited, combo, [combo] {
      if (combo->currentIndex() >= 0)
        selectComboItem(combo, -1, false);
      if (const std::shared_ptr<Window> target = windowOf(combo))
        meos_qt::notifyParent(*target, CBN_EDITCHANGE);
    });
  }
  // Selection with the mouse, the arrow keys or the wheel, also of the same item.
  QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [combo](int) {
    if (const std::shared_ptr<Window> target = windowOf(combo))
      meos_qt::notifyParent(*target, CBN_SELCHANGE);
  });

  control->font = systemFont();
  applyComboFont(window, *control);
}

/* ---------------------------------------------------------------------
   LISTBOX: QListWidget with tab stops
   --------------------------------------------------------------------- */

class ListWidget;

class ListControl : public Control {
public:
  QPointer<ListWidget> list;
  HFONT font = nullptr;
};

class ListWidget : public ControlWidget<QListWidget> {
public:
  explicit ListWidget(QWidget *parent) : ControlWidget<QListWidget>(parent) {
    viewport()->setMouseTracking(true);
    connect(verticalScrollBar(), &QAbstractSlider::actionTriggered, this, [this](int action) { onScroll(action); });
    connect(verticalScrollBar(), &QAbstractSlider::sliderReleased, this, [this] {
      const int position = verticalScrollBar()->value();
      sendInput(this, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, position), 0, nullptr);
      sendInput(this, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0, nullptr);
    });
  }

  bool notify = false;
  bool useTabStops = false;
  int itemHeight = 16;
  int ascent = 13;
  // tmAveCharWidth, and the average width that converts dialog units.
  int aveCharWidth = 7;
  int dialogCharWidth = 6;
  std::vector<int> tabStops; // pixels

  // Lays out pending item changes now, so that the scroll range is current.
  void layoutNow() {
    doItemsLayout();
    updateGeometries();
  }

  // TabbedTextOut as the list box calls it: stops at the given positions, then
  // at multiples of the default width.
  void drawItemText(QPainter &painter, int x, int y, const QString &text) const {
    const int baseline = y + ascent;
    if (!useTabStops) {
      painter.drawText(QPoint(x, baseline), text);
      return;
    }
    const QFontMetrics metrics(font());
    const bool single = tabStops.size() == 1;
    const int defaultWidth = single ? tabStops[0] : 8 * aveCharWidth;
    std::size_t nextStop = 0;
    const std::size_t stopCount = single ? 0 : tabStops.size();
    int position = x;
    int i = 0;
    const int length = int(text.size());
    while (i < length) {
      int tabs = 0;
      while (i < length && text[i] == QLatin1Char('\t')) {
        tabs++;
        i++;
      }
      int end = int(text.indexOf(QLatin1Char('\t'), i));
      if (end < 0)
        end = length;
      const QString part = text.mid(i, end - i);
      if (tabs > 0) {
        bool found = false;
        for (; stopCount >= nextStop + std::size_t(tabs); nextStop++) {
          if (tabStops[nextStop] > position) {
            position = tabStops[nextStop + std::size_t(tabs) - 1];
            found = true;
            break;
          }
        }
        if (!found && defaultWidth > 0)
          position = (position / defaultWidth + tabs) * defaultWidth;
      }
      painter.drawText(QPoint(position, baseline), part);
      position += metrics.horizontalAdvance(part);
      i = end;
    }
  }

protected:
  // Windows notifies when the button is released after a press in the list,
  // also if the selection has not changed.
  void mousePressEvent(QMouseEvent *event) override {
    QListWidget::mousePressEvent(event);
    if (event->button() == Qt::LeftButton)
      captured = true;
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    QListWidget::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && captured) {
      captured = false;
      notifyChange(LBN_SELCHANGE);
    }
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    QListWidget::mouseDoubleClickEvent(event);
    if (event->button() == Qt::LeftButton)
      notifyChange(LBN_DBLCLK);
  }

  void keyPressEvent(QKeyEvent *event) override {
    const int before = currentRow();
    QListWidget::keyPressEvent(event);
    bool changed = false;
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Home:
    case Qt::Key_End:
      changed = true;
      break;
    case Qt::Key_Space:
      changed = selectionMode() == QAbstractItemView::MultiSelection;
      break;
    default:
      changed = !event->text().isEmpty() && currentRow() != before;
      break;
    }
    if (changed)
      notifyChange(LBN_SELCHANGE);
  }

private:
  void notifyChange(int code) {
    if (!notify || count() == 0)
      return;
    if (const std::shared_ptr<Window> window = windowOf(this))
      meos_qt::notifyParent(*window, code);
  }

  // A user action on the scroll bar scrolls first, then passes the procedure
  // chain as WM_VSCROLL (where a subclass may synchronize another list).
  void onScroll(int action) {
    int code;
    switch (action) {
    case QAbstractSlider::SliderSingleStepAdd: code = SB_LINEDOWN; break;
    case QAbstractSlider::SliderSingleStepSub: code = SB_LINEUP; break;
    case QAbstractSlider::SliderPageStepAdd: code = SB_PAGEDOWN; break;
    case QAbstractSlider::SliderPageStepSub: code = SB_PAGEUP; break;
    case QAbstractSlider::SliderToMinimum: code = SB_TOP; break;
    case QAbstractSlider::SliderToMaximum: code = SB_BOTTOM; break;
    case QAbstractSlider::SliderMove: code = SB_THUMBTRACK; break;
    default: return;
    }
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(bar->sliderPosition());
    sendInput(this, WM_VSCROLL, MAKEWPARAM(code, code == SB_THUMBTRACK ? bar->value() : 0), 0, nullptr);
  }

  bool captured = false;
};

class ListDelegate : public QStyledItemDelegate {
public:
  explicit ListDelegate(ListWidget *list) : QStyledItemDelegate(list), list(list) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    const QPalette::ColorGroup group = (option.state & QStyle::State_Enabled) ? QPalette::Active : QPalette::Disabled;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    painter->save();
    painter->fillRect(option.rect, option.palette.color(group, selected ? QPalette::Highlight : QPalette::Base));
    painter->setFont(list->font());
    painter->setPen(option.palette.color(group, selected ? QPalette::HighlightedText : QPalette::Text));
    painter->setClipRect(option.rect);
    list->drawItemText(*painter, option.rect.left() + 1, option.rect.top(), index.data(Qt::DisplayRole).toString());
    painter->restore();
    if ((option.state & QStyle::State_HasFocus) && list->hasFocus()) {
      QStyleOptionFocusRect focus;
      focus.QStyleOption::operator=(option);
      focus.backgroundColor = option.palette.color(group, selected ? QPalette::Highlight : QPalette::Base);
      list->style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, list);
    }
  }

  QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
    return QSize(1, list->itemHeight);
  }

private:
  ListWidget *list;
};

void applyListFont(ListWidget &list, HFONT font) {
  const std::shared_ptr<meos_qt::Font> object = fontOf(font);
  if (!object)
    return;
  list.setFont(object->font);
  list.ascent = object->metrics.ascent;
  list.aveCharWidth = object->metrics.avgCharWidth;

  // As the list box: height and average width from the extent of the alphabet.
  const HDC dc = GetDC(nullptr);
  const HGDIOBJ previous = SelectObject(dc, font);
  SIZE size = {0, 0};
  const wchar_t alphabet[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  GetTextExtentPoint32(dc, alphabet, 52, &size);
  SelectObject(dc, previous);
  ReleaseDC(nullptr, dc);
  list.itemHeight = std::max<int>(size.cy, 1);
  list.dialogCharWidth = (size.cx / 26 + 1) / 2;
  list.doItemsLayout();
}

LRESULT CALLBACK listProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> window = meos_qt::findWindow(hwnd);
  const auto control = window ? std::dynamic_pointer_cast<ListControl>(window->control) : nullptr;
  ListWidget *list = control ? control->list.data() : nullptr;
  if (!list)
    return DefWindowProc(hwnd, message, wParam, lParam);

  LRESULT result = 0;
  if (commonMessage(*window, message, wParam, result))
    return result;

  const bool multiple = list->selectionMode() == QAbstractItemView::MultiSelection;
  const int count = list->count();
  const int index = int(wParam);
  const bool valid = index >= 0 && index < count;

  switch (message) {
  case LB_ADDSTRING:
  case LB_INSERTSTRING: {
    const int position = (message == LB_ADDSTRING || index == -1) ? count : index;
    if (position < 0 || position > count)
      return LB_ERR;
    auto *item = new QListWidgetItem(toQString(reinterpret_cast<LPCWSTR>(lParam)));
    item->setData(Qt::UserRole, QVariant(qlonglong(0)));
    list->insertItem(position, item);
    return position;
  }
  case LB_DELETESTRING: {
    if (!valid)
      return LB_ERR;
    // The other items keep their selection; Qt would select a neighbour of a
    // removed selected item.
    const QList<QListWidgetItem *> selected = list->selectedItems();
    delete list->takeItem(index);
    const QSignalBlocker blocker(list);
    for (int i = 0; i < list->count(); i++) {
      QListWidgetItem *item = list->item(i);
      item->setSelected(selected.contains(item));
    }
    return list->count();
  }
  case LB_RESETCONTENT:
    list->clear();
    return 0;
  case LB_INITSTORAGE:
    return count + LRESULT(wParam);
  case LB_GETCOUNT:
    return count;

  case LB_SETCURSEL:
    if (multiple)
      return LB_ERR;
    if (!valid) {
      list->clearSelection();
      return LB_ERR;
    }
    list->setCurrentRow(index, QItemSelectionModel::ClearAndSelect);
    list->scrollToItem(list->item(index));
    return index;
  case LB_GETCURSEL: {
    if (count == 0)
      return LB_ERR;
    // A multiple-selection list box returns the item with the focus rectangle.
    if (multiple)
      return std::max(list->currentRow(), 0);
    const QList<QListWidgetItem *> selected = list->selectedItems();
    return selected.isEmpty() ? LB_ERR : list->row(selected.first());
  }
  case LB_SETSEL: {
    if (!multiple)
      return LB_ERR;
    const int item = int(lParam);
    const bool select = wParam != 0;
    if (item == -1) {
      for (int i = 0; i < count; i++)
        list->item(i)->setSelected(select);
      return 0;
    }
    if (item < 0 || item >= count)
      return LB_ERR;
    list->item(item)->setSelected(select);
    return 0;
  }
  case LB_GETSEL:
    return valid ? (list->item(index)->isSelected() ? 1 : 0) : LB_ERR;

  case LB_GETTEXT:
    return valid ? copyItemText(list->item(index)->text(), lParam) : LB_ERR;
  case LB_GETTEXTLEN:
    return valid ? textLength(list->item(index)->text()) : LB_ERR;
  case LB_GETITEMDATA:
    return valid ? LRESULT(list->item(index)->data(Qt::UserRole).toLongLong()) : LB_ERR;
  case LB_SETITEMDATA:
    if (!valid)
      return LB_ERR;
    list->item(index)->setData(Qt::UserRole, QVariant(qlonglong(lParam)));
    return TRUE;
  case LB_FINDSTRING: {
    const int found = findPrefix(count, index, toQString(reinterpret_cast<LPCWSTR>(lParam)),
                                 [list](int i) { return list->item(i)->text(); });
    return found < 0 ? LB_ERR : found;
  }

  case LB_GETTOPINDEX:
    list->layoutNow();
    return list->verticalScrollBar()->value();
  case LB_SETTOPINDEX:
    if (!valid && !(index == 0 && count == 0))
      return LB_ERR;
    list->layoutNow();
    list->verticalScrollBar()->setValue(index);
    return 0;
  case LB_SETTABSTOPS: {
    if (!(window->style & LBS_USETABSTOPS))
      return FALSE;
    // Dialog template units: a quarter of the average character width.
    list->tabStops.clear();
    const auto *stops = reinterpret_cast<const int *>(lParam);
    for (WPARAM i = 0; stops && i < wParam; i++)
      list->tabStops.push_back(mulDiv(stops[i], list->dialogCharWidth, 4));
    list->viewport()->update();
    return TRUE;
  }

  case WM_VSCROLL: {
    // Scroll bar input from the user has already scrolled.
    if (performInput(hwnd, message))
      return 0;
    list->layoutNow();
    QScrollBar *bar = list->verticalScrollBar();
    switch (LOWORD(wParam)) {
    case SB_LINEUP: bar->setValue(bar->value() - 1); break;
    case SB_LINEDOWN: bar->setValue(bar->value() + 1); break;
    case SB_PAGEUP: bar->setValue(bar->value() - bar->pageStep()); break;
    case SB_PAGEDOWN: bar->setValue(bar->value() + bar->pageStep()); break;
    case SB_TOP: bar->setValue(bar->minimum()); break;
    case SB_BOTTOM: bar->setValue(bar->maximum()); break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: bar->setValue(HIWORD(wParam)); break;
    }
    return 0;
  }

  case WM_SETFONT:
    control->font = reinterpret_cast<HFONT>(wParam);
    applyListFont(*list, control->font);
    return 0;
  case WM_GETFONT:
    return reinterpret_cast<LRESULT>(control->font);
  case WM_SETFOCUS:
    meos_qt::notifyParent(*window, LBN_SETFOCUS);
    return 0;
  case WM_KILLFOCUS:
    meos_qt::notifyParent(*window, LBN_KILLFOCUS);
    return 0;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

void createList(Window &window, QWidget *parentWidget) {
  QWidget *frame = meos_qt::createFrame(window, parentWidget);
  auto control = std::make_shared<ListControl>();
  auto *list = new ListWidget(frame);
  control->list = list;
  window.control = control;
  window.client = list;

  list->notify = (window.style & LBS_NOTIFY) != 0;
  list->useTabStops = (window.style & LBS_USETABSTOPS) != 0;
  list->setFocusPolicy(Qt::StrongFocus);
  list->setFrameShape(QFrame::NoFrame);
  list->setSelectionMode((window.style & LBS_MULTIPLESEL) ? QAbstractItemView::MultiSelection
                                                          : QAbstractItemView::SingleSelection);
  list->setEditTriggers(QAbstractItemView::NoEditTriggers);
  list->setUniformItemSizes(true);
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
  list->setVerticalScrollBarPolicy((window.style & WS_VSCROLL) ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
  list->setHorizontalScrollBarPolicy((window.style & WS_HSCROLL) ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
  list->setItemDelegate(new ListDelegate(list));

  control->font = systemFont();
  applyListFont(*list, control->font);
}

/* ---------------------------------------------------------------------
   STATIC: a canvas that shows its text (MeOS draws on it with GetDC)
   --------------------------------------------------------------------- */

class StaticControl : public Control {
public:
  HFONT font = nullptr;
};

LRESULT CALLBACK staticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const std::shared_ptr<Window> window = meos_qt::findWindow(hwnd);
  const auto control = window ? std::dynamic_pointer_cast<StaticControl>(window->control) : nullptr;
  if (!control)
    return DefWindowProc(hwnd, message, wParam, lParam);

  switch (message) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT paint;
    const HDC dc = BeginPaint(hwnd, &paint);
    RECT rect;
    GetClientRect(hwnd, &rect);
    SetDCBrushColor(dc, GetSysColor(COLOR_3DFACE));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    Rectangle(dc, rect.left, rect.top, rect.right + 1, rect.bottom + 1);
    SelectObject(dc, control->font ? static_cast<HGDIOBJ>(control->font) : GetStockObject(SYSTEM_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    DrawText(dc, window->text.c_str(), int(window->text.size()), &rect, DT_LEFT | DT_WORDBREAK);
    EndPaint(hwnd, &paint);
    return 0;
  }
  case WM_SETTEXT:
    DefWindowProc(hwnd, message, wParam, lParam);
    InvalidateRect(hwnd, nullptr, TRUE);
    return TRUE;
  case WM_SETFONT:
    control->font = reinterpret_cast<HFONT>(wParam);
    if (lParam)
      InvalidateRect(hwnd, nullptr, TRUE);
    return 0;
  case WM_GETFONT:
    return reinterpret_cast<LRESULT>(control->font);
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

void createStatic(Window &window, QWidget *parentWidget) {
  meos_qt::createCanvas(window, parentWidget);
  window.control = std::make_shared<StaticControl>();
}

meos_qt::WindowClass systemClass(const wchar_t *name, WNDPROC proc, UINT style, LPCWSTR cursor,
                                 void (*create)(Window &, QWidget *)) {
  meos_qt::WindowClass windowClass;
  windowClass.name = name;
  windowClass.style = style;
  windowClass.proc = proc;
  windowClass.cursor = LoadCursor(nullptr, cursor);
  windowClass.createWidgets = create;
  return windowClass;
}

} // namespace

void meos_qt::registerControlClasses(const std::function<void(const WindowClass &)> &add) {
  add(systemClass(L"Button", buttonProc, CS_DBLCLKS, IDC_ARROW, createButton));
  add(systemClass(L"Edit", editProc, CS_DBLCLKS, IDC_IBEAM, createEdit));
  add(systemClass(L"ComboBox", comboProc, CS_DBLCLKS, IDC_ARROW, createCombo));
  add(systemClass(L"ListBox", listProc, CS_DBLCLKS, IDC_ARROW, createList));
  add(systemClass(L"Static", staticProc, 0, IDC_ARROW, createStatic));
}
