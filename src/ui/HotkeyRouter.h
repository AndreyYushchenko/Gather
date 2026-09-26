#pragma once

#include <QKeyCombination>
#include <QList>
#include <QObject>
#include <functional>

class QKeyEvent;
class QWidget;

// App-wide presentation hotkeys (Настройки → Горячие клавиши).
//
// Installed as a QApplication event filter rather than QShortcuts on the main
// window, so the keys keep working when the projector window, a preview
// window or another app window is the active one, and so they win over a
// panel's own shortcut on the same key (a QShortcut clash makes Qt fire
// neither). The key is claimed at ShortcutOverride time, and the KeyPress
// that follows is swallowed so a focused button doesn't also react (Space).
//
// Left alone: modal dialogs and popups, the hotkey editor itself, typing
// keys (arrows, Space, Esc, …) while a text field has focus, and keys that
// a non-main window (e.g. the photo viewer's ← →) binds for itself.
class HotkeyRouter : public QObject {
    Q_OBJECT
public:
    explicit HotkeyRouter(QObject *parent = nullptr);

    // Replaces the key list of an action; an empty list disables it.
    void setAction(const QString &id, const QList<QKeyCombination> &keys, const std::function<void()> &handler);
    void setKeys(const QString &id, const QList<QKeyCombination> &keys);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Action {
        QString id;
        QList<QKeyCombination> keys;
        std::function<void()> handler;
    };

    const Action *match(const QKeyEvent *event) const;
    bool shouldLeaveAlone(const QKeyEvent *event) const;

    QList<Action> m_actions;
    // The KeyPress that belongs to a ShortcutOverride we already handled.
    QKeyCombination m_swallowKey;
    bool m_swallowPending = false;
};
