#pragma once

#include "display/TimerScreen.h"

#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <functional>

class QHBoxLayout;

// Small building blocks for the "Таймеры и время" screen. Plain QFrames
// that paint/handle their own clicks rather than QPushButtons with nested
// labels — the same ClearType-garbling workaround as Sidebar's
// SidebarNavRow and ChevronButton. Callbacks (std::function) instead of
// signals keep them free of moc.
namespace TimerUi {

QLabel *text(const QString &content, double size, int weight, const QString &color);
QLabel *icon(const QString &name, const QString &color, int size);
// Styles a frame by its own object name so the rule can't cascade onto
// child labels.
void styleFrame(QFrame *frame, const QString &namePrefix, const QString &body);
// Shared QSS for line edits, time/date edits and sliders in the editor.
QString fieldStyleSheet();

} // namespace TimerUi

class ClickFrame : public QFrame {
public:
    explicit ClickFrame(QWidget *parent = nullptr);

    std::function<void()> onClick;
    std::function<void()> onDoubleClick;
    std::function<void(const QPoint &)> onContextMenu;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
};

// A QLabel that elides instead of forcing its parent wider.
class ElideLabel : public QLabel {
public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

// Icon + label button, filled or outlined. When squeezed narrower than icon
// + label need, the icon hides so the label is never clipped.
ClickFrame *makeTimerButton(const QString &icon, const QString &label, const QString &fill, const QString &border,
                            const QString &color, int padV, int padH, bool center);
// 38×38 outlined icon-only button.
ClickFrame *makeTimerIconButton(const QString &icon, const QString &tooltip);

// A selectable chip: blue when checked ("Тип экрана").
class ChoiceButton : public ClickFrame {
public:
    ChoiceButton(const QString &icon, const QString &label);
    void setChecked(bool checked);

private:
    QString m_icon;
    QString m_name;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_label = nullptr;
};

// design.pen "Target Seg" / "Format Seg": a light track with one white
// selected segment. Wraps onto a second line when it doesn't fit.
class SegmentedControl : public QFrame {
public:
    explicit SegmentedControl(const QStringList &labels);
    void setCurrent(int index);
    int current() const { return m_current; }
    std::function<void(int)> onChanged;

private:
    QList<ClickFrame *> m_items;
    QList<QLabel *> m_labels;
    int m_current = -1;
};

// Value box that opens a menu of options (design.pen "Style Field").
class DropdownField : public ClickFrame {
public:
    explicit DropdownField(bool chevronAtEnd = false);
    void setOptions(const QStringList &options);
    void setCurrent(int index);
    int current() const { return m_current; }
    void setSwatch(const QColor &color);
    void setLeadingIcon(const QString &name);
    std::function<void(int)> onSelected;

private:
    void openMenu();

    QFrame *m_swatch;
    QLabel *m_icon;
    ElideLabel *m_value;
    QStringList m_options;
    int m_current = -1;
};

// A tiny rounded checkbox + label ("Показывать названия").
class CheckRow : public ClickFrame {
public:
    explicit CheckRow(const QString &label);
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    std::function<void(bool)> onToggled;

private:
    QFrame *m_box;
    QLabel *m_check;
    bool m_checked = false;
};

// One "Оформление" background thumbnail.
class BackgroundThumb : public ClickFrame {
public:
    explicit BackgroundThumb(TimerBackground background);
    TimerBackground background() const { return m_background; }
    void setSelected(bool selected);
    void setCustomImage(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void paintBox();

    TimerBackground m_background;
    QWidget *m_box;
    QPixmap m_image;
    QString m_customPath;
    bool m_selected = false;
};
