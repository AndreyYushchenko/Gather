#include "HotkeyRouter.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextEdit>

namespace {

QKeyCombination combinationOf(const QKeyEvent *event)
{
    // Numpad keys carry KeypadModifier; a binding doesn't care where the key is.
    return QKeyCombination(event->modifiers() & ~Qt::KeypadModifier, Qt::Key(event->key()));
}

bool isTextInput(const QWidget *widget)
{
    if (!widget)
        return false;
    if (const auto *line = qobject_cast<const QLineEdit *>(widget))
        return !line->isReadOnly();
    if (const auto *text = qobject_cast<const QTextEdit *>(widget))
        return !text->isReadOnly();
    if (const auto *plain = qobject_cast<const QPlainTextEdit *>(widget))
        return !plain->isReadOnly();
    return qobject_cast<const QAbstractSpinBox *>(widget) != nullptr;
}

// Keys a text field uses for itself: plain keys other than F-keys and
// PageUp/PageDown (clickers send those, and a line edit has no use for them).
bool isTypingKey(const QKeyEvent *event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const int key = event->key();
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return false;
    return key != Qt::Key_PageUp && key != Qt::Key_PageDown;
}

} // namespace

HotkeyRouter::HotkeyRouter(QObject *parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
}

void HotkeyRouter::setAction(const QString &id, const QList<QKeyCombination> &keys, const std::function<void()> &handler)
{
    for (Action &action : m_actions) {
        if (action.id == id) {
            action.keys = keys;
            action.handler = handler;
            return;
        }
    }
    m_actions.append({id, keys, handler});
}

void HotkeyRouter::setKeys(const QString &id, const QList<QKeyCombination> &keys)
{
    for (Action &action : m_actions) {
        if (action.id == id)
            action.keys = keys;
    }
}

const HotkeyRouter::Action *HotkeyRouter::match(const QKeyEvent *event) const
{
    const QKeyCombination combination = combinationOf(event);
    for (const Action &action : m_actions) {
        if (action.keys.contains(combination))
            return &action;
    }
    return nullptr;
}

bool HotkeyRouter::shouldLeaveAlone(const QKeyEvent *event) const
{
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return true;
    QWidget *active = QApplication::activeWindow();
    if (!active)
        return true; // another program has the focus
    QWidget *focus = QApplication::focusWidget();
    if (qobject_cast<QKeySequenceEdit *>(focus) || (focus && qobject_cast<QKeySequenceEdit *>(focus->parentWidget())))
        return true; // assigning a hotkey right now
    if (isTextInput(focus) && isTypingKey(event))
        return true;
    // A secondary window with its own binding for the key (the photo viewer's
    // ← →) keeps it; the main window's panels don't — presentation keys win.
    if (qobject_cast<QDialog *>(active)) {
        const QKeySequence sequence(combinationOf(event));
        for (const QShortcut *shortcut : active->findChildren<QShortcut *>()) {
            if (shortcut->isEnabled() && shortcut->keys().contains(sequence))
                return true;
        }
    }
    return false;
}

bool HotkeyRouter::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);
    if (!watched->isWidgetType())
        return false; // the QWindow copy of the event; the widget one follows

    auto *key = static_cast<QKeyEvent *>(event);
    if (type == QEvent::KeyPress && m_swallowPending) {
        m_swallowPending = false;
        if (combinationOf(key) == m_swallowKey)
            return true;
    }

    const Action *action = match(key);
    if (!action || shouldLeaveAlone(key))
        return false;

    if (type == QEvent::ShortcutOverride) {
        // Claim the key before any QShortcut can, and drop its KeyPress.
        key->accept();
        m_swallowKey = combinationOf(key);
        m_swallowPending = true;
    }
    // Holding a key down must not race through the slides. Queued, so an
    // action that opens a dialog doesn't run its event loop in here.
    if (!key->isAutoRepeat() && action->handler)
        QMetaObject::invokeMethod(this, action->handler, Qt::QueuedConnection);
    return true;
}
