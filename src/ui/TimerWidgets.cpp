#include "TimerWidgets.h"
#include "FlowLayout.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace {


QString uniqueName(const QString &prefix)
{
    static int counter = 0;
    return QStringLiteral("%1_%2").arg(prefix).arg(++counter);
}

// Hides a button's icon when the button is narrower than icon + label need.
class IconFitter : public QObject {
public:
    IconFitter(QWidget *button, QWidget *icon)
        : QObject(button)
        , m_button(button)
        , m_icon(icon)
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Resize) {
            if (m_fullWidth == 0)
                m_fullWidth = m_button->layout()->sizeHint().width();
            m_icon->setVisible(m_button->width() >= m_fullWidth);
        }
        return false;
    }

private:
    QWidget *m_button;
    QWidget *m_icon;
    int m_fullWidth = 0;
};

QString menuStyleSheet()
{
    return QStringLiteral(
        "QMenu { background: #ffffff; border: 1px solid %1; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 7px 14px; border-radius: 6px; color: %2; font-size: 12.5px; }"
        "QMenu::item:selected { background: %3; }"
        "QMenu::separator { height: 1px; background: %1; margin: 4px 6px; }")
        .arg(Theme::BorderLight, Theme::TextDarkPrimary, Theme::AccentBlueBg);
}

} // namespace

namespace TimerUi {

QLabel *text(const QString &content, double size, int weight, const QString &color)
{
    auto *label = new QLabel(content);
    label->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: %2px; font-weight: %3;")
                             .arg(color).arg(size).arg(weight));
    return label;
}

QLabel *icon(const QString &name, const QString &color, int size)
{
    auto *label = new QLabel;
    label->setFixedSize(size, size);
    label->setStyleSheet(QStringLiteral("background: transparent;"));
    label->setPixmap(IconProvider::pixmap(name, QColor(color), size));
    return label;
}

void styleFrame(QFrame *frame, const QString &namePrefix, const QString &body)
{
    const QString name = frame->objectName().isEmpty() ? uniqueName(namePrefix) : frame->objectName();
    frame->setObjectName(name);
    frame->setStyleSheet(QStringLiteral("QFrame#%1 { %2 }").arg(name, body));
}

QString fieldStyleSheet()
{
    return QStringLiteral(R"(
        QLineEdit, QPlainTextEdit, QTimeEdit, QDateTimeEdit {
            background: #ffffff; border: 1px solid %1; border-radius: 9px; padding: 7px 11px;
            font-size: 13px; color: %2; selection-background-color: %3;
        }
        QPlainTextEdit { padding: 5px 8px; }
        QLineEdit:focus, QPlainTextEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus { border: 1px solid %3; }
        QDateTimeEdit::drop-down { border: none; width: 24px; subcontrol-position: center right; }
        QDateTimeEdit::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
        QSlider::groove:horizontal { height: 6px; background: %1; border-radius: 3px; }
        QSlider::sub-page:horizontal { background: %3; border-radius: 3px; }
        QSlider::handle:horizontal { background: #ffffff; border: 2px solid %3; width: 12px; height: 12px;
                                     margin: -5px 0; border-radius: 8px; }
    )").arg(Theme::BorderLight, Theme::TextDarkPrimary, Theme::AccentBlue);
}

} // namespace TimerUi

// ---- ClickFrame ------------------------------------------------------------

ClickFrame::ClickFrame(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::PointingHandCursor);
}

void ClickFrame::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick)
        onClick();
}

void ClickFrame::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && onDoubleClick)
        onDoubleClick();
}

void ClickFrame::contextMenuEvent(QContextMenuEvent *event)
{
    if (onContextMenu)
        onContextMenu(event->globalPos());
}

// ---- ElideLabel ------------------------------------------------------------

QSize ElideLabel::minimumSizeHint() const
{
    return QSize(0, QLabel::minimumSizeHint().height());
}

void ElideLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
}

// ---- Buttons ---------------------------------------------------------------

ClickFrame *makeTimerButton(const QString &icon, const QString &label, const QString &fill, const QString &border,
                            const QString &color, int padV, int padH, bool center)
{
    auto *button = new ClickFrame;
    TimerUi::styleFrame(button, QStringLiteral("TimerButton"),
                        QStringLiteral("background: %1; border: 1px solid %2; border-radius: 9px;").arg(fill, border));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(padH, padV, padH, padV);
    layout->setSpacing(7);
    if (center)
        layout->addStretch();
    QLabel *iconLabel = icon.isEmpty() ? nullptr : TimerUi::icon(icon, color, 14);
    if (iconLabel)
        layout->addWidget(iconLabel);
    if (!label.isEmpty())
        layout->addWidget(TimerUi::text(label, 13, 600, color));
    if (center)
        layout->addStretch();
    if (iconLabel && !label.isEmpty())
        button->installEventFilter(new IconFitter(button, iconLabel));
    return button;
}

ClickFrame *makeTimerIconButton(const QString &icon, const QString &tooltip)
{
    auto *button = new ClickFrame;
    button->setFixedSize(36, 36);
    button->setToolTip(tooltip);
    TimerUi::styleFrame(button, QStringLiteral("TimerIconButton"),
                        QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 9px;").arg(Theme::BorderLight));
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(TimerUi::icon(icon, Theme::TextDarkPrimary, 14), 0, Qt::AlignCenter);
    return button;
}

// ---- ChoiceButton ----------------------------------------------------------

ChoiceButton::ChoiceButton(const QString &icon, const QString &label)
    : m_icon(icon)
{
    m_name = uniqueName(QStringLiteral("Choice"));
    setObjectName(m_name);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(13, 8, 13, 8);
    layout->setSpacing(7);
    if (!icon.isEmpty()) {
        m_iconLabel = new QLabel;
        m_iconLabel->setFixedSize(15, 15);
        m_iconLabel->setStyleSheet(QStringLiteral("background: transparent;"));
        layout->addWidget(m_iconLabel);
    }
    m_label = new QLabel(label);
    layout->addWidget(m_label);
    setChecked(false);
}

void ChoiceButton::setChecked(bool checked)
{
    const QString fg = checked ? Theme::TextLightPrimary : Theme::TextDarkPrimary;
    setStyleSheet(QStringLiteral("QFrame#%1 { background: %2; border: 1px solid %3; border-radius: 9px; }")
                      .arg(m_name, checked ? Theme::AccentBlue : QStringLiteral("#ffffff"), checked ? Theme::AccentBlue : Theme::BorderLight));
    m_label->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: 12.5px; font-weight: 600;").arg(fg));
    if (m_iconLabel)
        m_iconLabel->setPixmap(IconProvider::pixmap(m_icon, QColor(fg), 15));
}

// ---- SegmentedControl ------------------------------------------------------

SegmentedControl::SegmentedControl(const QStringList &labels)
{
    TimerUi::styleFrame(this, QStringLiteral("Segmented"),
                        QStringLiteral("background: #fbfbfc; border: 1px solid %1; border-radius: 10px;").arg(Theme::BorderLight));
    auto *flow = new FlowLayout(FlowLayout::Mode::Natural, 2, 0, this);
    flow->setContentsMargins(3, 3, 3, 3);
    for (int i = 0; i < labels.size(); ++i) {
        auto *item = new ClickFrame;
        item->setObjectName(uniqueName(QStringLiteral("Segment")));
        auto *layout = new QHBoxLayout(item);
        layout->setContentsMargins(12, 7, 12, 7);
        auto *label = new QLabel(labels.at(i));
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        item->onClick = [this, i]() {
            if (i == m_current)
                return;
            setCurrent(i);
            if (onChanged)
                onChanged(i);
        };
        m_items << item;
        m_labels << label;
        flow->addWidget(item);
    }
    setCurrent(0);
}

void SegmentedControl::setCurrent(int index)
{
    m_current = index;
    for (int i = 0; i < m_items.size(); ++i) {
        const bool on = i == index;
        m_items.at(i)->setStyleSheet(on
            ? QStringLiteral("QFrame#%1 { background: #ffffff; border: 1px solid %2; border-radius: 7px; }").arg(m_items.at(i)->objectName(), Theme::BorderLight)
            : QStringLiteral("QFrame#%1 { background: transparent; border: 1px solid transparent; border-radius: 7px; }").arg(m_items.at(i)->objectName()));
        m_labels.at(i)->setStyleSheet(QStringLiteral("background: transparent; border: none; color: %1; font-size: 12.5px; font-weight: %2;")
                                          .arg(on ? Theme::TextDarkPrimary : Theme::TextDarkSecondary)
                                          .arg(on ? 600 : 500));
    }
}

// ---- DropdownField ---------------------------------------------------------

DropdownField::DropdownField(bool chevronAtEnd)
{
    TimerUi::styleFrame(this, QStringLiteral("Dropdown"),
                        QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 9px;").arg(Theme::BorderLight));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);

    m_swatch = new QFrame;
    m_swatch->setFixedSize(16, 16);
    m_swatch->hide();
    layout->addWidget(m_swatch);

    m_icon = new QLabel;
    m_icon->setFixedSize(15, 15);
    m_icon->setStyleSheet(QStringLiteral("background: transparent;"));
    m_icon->hide();
    layout->addWidget(m_icon);

    m_value = new ElideLabel;
    m_value->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: 13px; font-weight: 500;").arg(Theme::TextDarkPrimary));
    layout->addWidget(m_value, chevronAtEnd ? 1 : 0);
    layout->addWidget(TimerUi::icon(QStringLiteral("chevron-down"), Theme::TextDarkSecondary, 13));
    if (!chevronAtEnd)
        layout->addStretch();

    onClick = [this]() { openMenu(); };
}

void DropdownField::setOptions(const QStringList &options)
{
    m_options = options;
    if (m_current >= 0)
        m_value->setText(m_options.value(m_current));
}

void DropdownField::setCurrent(int index)
{
    m_current = index;
    m_value->setText(m_options.value(index));
    m_value->updateGeometry();
}

void DropdownField::setSwatch(const QColor &color)
{
    m_swatch->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px;").arg(color.name(), Theme::BorderLight));
    m_swatch->show();
}

void DropdownField::setLeadingIcon(const QString &name)
{
    m_icon->setPixmap(IconProvider::pixmap(name, QColor(Theme::TextDarkPrimary), 15));
    m_icon->show();
}

void DropdownField::openMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(menuStyleSheet());
    for (int i = 0; i < m_options.size(); ++i) {
        QAction *action = menu.addAction(m_options.at(i));
        action->setCheckable(true);
        action->setChecked(i == m_current);
        action->setData(i);
    }
    menu.setMinimumWidth(width());
    QAction *chosen = menu.exec(mapToGlobal(QPoint(0, height() + 4)));
    if (!chosen)
        return;
    setCurrent(chosen->data().toInt());
    if (onSelected)
        onSelected(m_current);
}

// ---- CheckRow --------------------------------------------------------------

CheckRow::CheckRow(const QString &label)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_box = new QFrame;
    m_box->setFixedSize(18, 18);
    auto *boxLayout = new QHBoxLayout(m_box);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    m_check = TimerUi::icon(QStringLiteral("check"), Theme::TextLightPrimary, 13);
    boxLayout->addWidget(m_check, 0, Qt::AlignCenter);
    layout->addWidget(m_box);
    layout->addWidget(TimerUi::text(label, 13, 500, Theme::TextDarkPrimary));
    onClick = [this]() {
        setChecked(!m_checked);
        if (onToggled)
            onToggled(m_checked);
    };
    setChecked(false);
}

void CheckRow::setChecked(bool checked)
{
    m_checked = checked;
    TimerUi::styleFrame(m_box, QStringLiteral("CheckBox"),
                        checked ? QStringLiteral("background: %1; border: 1px solid %1; border-radius: 4px;").arg(Theme::AccentBlue)
                                : QStringLiteral("background: #ffffff; border: 1px solid #c9ced8; border-radius: 4px;"));
    m_check->setVisible(checked);
}

// ---- BackgroundThumb -------------------------------------------------------

BackgroundThumb::BackgroundThumb(TimerBackground background)
    : m_background(background)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    m_box = new QWidget;
    m_box->setFixedHeight(62);
    m_box->installEventFilter(this);
    layout->addWidget(m_box);
    auto *caption = TimerUi::text(TimerFormat::backgroundName(background), 11.5, 500, Theme::TextDarkSecondary);
    caption->setAlignment(Qt::AlignCenter);
    layout->addWidget(caption);

    if (background != TimerBackground::Custom) {
        TimerScreen screen;
        screen.background = background;
        const QString path = TimerFormat::backgroundImagePath(screen);
        if (!path.isEmpty())
            m_image = QPixmap(path).scaled(320, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    } else {
        setToolTip(QObject::tr("Выбрать своё изображение для фона (правый щелчок — заменить)"));
    }
}

void BackgroundThumb::setSelected(bool selected)
{
    m_selected = selected;
    m_box->update();
}

void BackgroundThumb::setCustomImage(const QString &path)
{
    if (path == m_customPath)
        return;
    m_customPath = path;
    m_image = path.isEmpty() ? QPixmap() : QPixmap(path).scaled(320, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_box->update();
}

bool BackgroundThumb::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_box && event->type() == QEvent::Paint) {
        paintBox();
        return true;
    }
    return ClickFrame::eventFilter(watched, event);
}

void BackgroundThumb::paintBox()
{
    QPainter painter(m_box);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF bounds = QRectF(m_box->rect()).adjusted(1, 1, -1, -1);
    QPainterPath clip;
    clip.addRoundedRect(bounds, Theme::radius(10), Theme::radius(10));

    painter.save();
    painter.setClipPath(clip);
    if (m_background == TimerBackground::Gradient) {
        QLinearGradient gradient(bounds.topLeft(), bounds.bottomLeft());
        gradient.setColorAt(0, QColor(0x7c, 0x6f, 0xd6));
        gradient.setColorAt(1, QColor(0x2b, 0x23, 0x50));
        painter.fillRect(bounds, gradient);
    } else if (!m_image.isNull()) {
        const QSizeF source = m_image.size();
        const qreal scale = qMax(bounds.width() / source.width(), bounds.height() / source.height());
        const QSizeF cropped(bounds.width() / scale, bounds.height() / scale);
        painter.drawPixmap(bounds, m_image,
                           QRectF(QPointF((source.width() - cropped.width()) / 2, (source.height() - cropped.height()) / 2), cropped));
    } else if (m_background == TimerBackground::Custom) {
        painter.fillRect(bounds, QColor(Theme::BgPanel));
    } else {
        painter.fillRect(bounds, QColor(0x12, 0x15, 0x1c));
    }
    painter.restore();

    if (m_background == TimerBackground::Custom && m_image.isNull()) {
        painter.setPen(QPen(QColor(Theme::BorderLight), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds, Theme::radius(10), Theme::radius(10));
        painter.drawPixmap(QPointF(bounds.center().x() - 9, bounds.center().y() - 9),
                           IconProvider::pixmap(QStringLiteral("image-plus"), QColor(Theme::TextDarkSecondary), 18));
    }

    if (TimerFormat::isVideoBackground(m_background)) {
        // design.pen "Video Badge": dark pill with ▶ видео.
        QFont font(Theme::fontFamily());
        font.setPixelSize(9);
        font.setBold(true);
        const QRectF badge(bounds.left() + 6, bounds.top() + 6, 44, 15);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 128));
        painter.drawRoundedRect(badge, Theme::radius(5), Theme::radius(5));
        painter.drawPixmap(QPointF(badge.left() + 4, badge.top() + 3), IconProvider::pixmap(QStringLiteral("play"), Qt::white, 9));
        painter.setPen(Qt::white);
        painter.setFont(font);
        painter.drawText(badge.adjusted(15, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, QObject::tr("видео"));
    }

    if (m_selected) {
        painter.setPen(QPen(QColor(Theme::AccentBlue), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds, Theme::radius(10), Theme::radius(10));
        const QRectF badge(bounds.right() - 6 - 18, bounds.bottom() - 6 - 18, 18, 18);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Theme::AccentBlue));
        painter.drawEllipse(badge);
        painter.drawPixmap(QPointF(badge.center().x() - 5.5, badge.center().y() - 5.5),
                           IconProvider::pixmap(QStringLiteral("check"), Qt::white, 11));
    }
}
