#include "TimersPanel.h"
#include "FlowLayout.h"
#include "IconProvider.h"
#include "Theme.h"
#include "TimerSound.h"
#include "TimerWidgets.h"
#include "ToggleSwitch.h"
#include "core/Database.h"
#include "display/PresentationController.h"
#include "display/SlideRenderWidget.h"
#include "display/TimerEngine.h"
#include "display/TimerRenderWidget.h"

#include <QCursor>
#include <QDateTimeEdit>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTimeEdit>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

using namespace TimerUi;

namespace {

constexpr qint64 Minute = 60 * 1000;

// Keeps the preview 16:9 at whatever width its column gets. Positions the
// child by hand: with a layout inside, Qt asks that layout (not this
// widget) for height-for-width and the box collapses to zero height.
class AspectBox : public QWidget {
public:
    explicit AspectBox(QWidget *child)
        : m_child(child)
    {
        child->setParent(this);
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return width * 9 / 16; }
    QSize sizeHint() const override { return QSize(440, 248); }
    QSize minimumSizeHint() const override { return QSize(160, 90); }

protected:
    void resizeEvent(QResizeEvent *) override { m_child->setGeometry(rect()); }

private:
    QWidget *m_child;
};

QWidget *flowRow(const QList<QWidget *> &widgets, int spacing = 10)
{
    auto *row = new QWidget;
    auto *flow = new FlowLayout(FlowLayout::Mode::Natural, spacing, 0, row);
    flow->setCenterItems(true);
    for (QWidget *widget : widgets)
        flow->addWidget(widget);
    return row;
}

QWidget *labelledColumn(const QString &label, QWidget *field)
{
    auto *column = new QWidget;
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(text(label, 12, 500, Theme::TextDarkSecondary));
    layout->addWidget(field);
    return column;
}

QLabel *hint(const QString &content)
{
    auto *label = text(content, 12, 500, Theme::TextDarkSecondary);
    label->setWordWrap(true);
    return label;
}

ToggleSwitch *makeToggle()
{
    auto *toggle = new ToggleSwitch;
    toggle->setFixedSize(36, 20); // design.pen "Toggle": 36×20
    return toggle;
}

QTimeEdit *makeTimeEdit(const QString &format, int width)
{
    auto *edit = new QTimeEdit;
    edit->setDisplayFormat(format);
    edit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    edit->setAlignment(Qt::AlignCenter);
    edit->setFixedWidth(width);
    edit->setFixedHeight(34);
    return edit;
}

QString minutesLabel(int minutes)
{
    return QObject::tr("%1 мин").arg(minutes);
}

} // namespace

// ---------------------------------------------------------------------------

TimersPanel::TimersPanel(QWidget *parent)
    : QWidget(parent)
{
    m_engine = new TimerEngine(this);
    QSettings settings;
    m_selectedId = settings.value(QStringLiteral("timers/v2/selected")).toString();
    m_showNames = settings.value(QStringLiteral("timers/v2/showNames"), true).toBool();
    if (m_engine->indexOf(m_selectedId) < 0)
        m_selectedId = m_engine->screens().first().id;

    setObjectName(QStringLiteral("TimersPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#TimersPanel { background: #ffffff; }"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }") + Theme::scrollBarCss());
    outer->addWidget(scroll);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("TimersContent"));
    content->setStyleSheet(QStringLiteral("QWidget#TimersContent { background: #ffffff; }") + fieldStyleSheet());
    scroll->setWidget(content);

    // design.pen "Timers Content": vertical, gap 16, padding [22, 28].
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 22, 28, 22);
    layout->setSpacing(16);
    layout->addWidget(buildHeader());
    layout->addWidget(buildScreensHeader());
    m_cardsRow = new QWidget;
    new FlowLayout(FlowLayout::Mode::EqualColumns, 14, 118, m_cardsRow);
    layout->addWidget(m_cardsRow);
    layout->addWidget(buildEditorCard());
    layout->addWidget(buildContentSection());
    layout->addWidget(buildTextSection());
    layout->addWidget(buildStyleSection());
    layout->addWidget(buildBehaviourSection());
    layout->addStretch();

    for (int i = 0; i < 9; ++i) {
        auto *shortcut = new QShortcut(QKeySequence(Qt::Key_1 + i), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, i]() {
            if (i < m_engine->screens().size())
                m_engine->goLive(m_engine->screens().at(i).id);
        });
    }

    connect(m_engine, &TimerEngine::screensChanged, this, [this]() {
        if (m_engine->indexOf(m_selectedId) < 0)
            m_selectedId = m_engine->screens().first().id;
        rebuildCards();
        refreshEditor();
    });
    connect(m_engine, &TimerEngine::screenChanged, this, [this](const QString &id) {
        refreshCards();
        if (id == m_selectedId)
            refreshEditor();
    });
    connect(m_engine, &TimerEngine::runStateChanged, this, [this](const QString &id) {
        refreshCards();
        if (id == m_selectedId) {
            m_preview->setSlide(m_engine->slide(id));
            refreshState();
        }
    });
    connect(m_engine, &TimerEngine::liveChanged, this, [this]() {
        refreshCards();
        refreshState();
    });

    auto *stateTick = new QTimer(this);
    stateTick->setInterval(500);
    connect(stateTick, &QTimer::timeout, this, &TimersPanel::refreshState);
    stateTick->start();

    rebuildCards();
    refreshEditor();
}

void TimersPanel::setController(PresentationController *controller)
{
    m_controller = controller;
    m_engine->setController(controller);
}

void TimersPanel::goLiveSelected()
{
    if (m_engine->indexOf(m_selectedId) >= 0)
        m_engine->goLive(m_selectedId);
}

// ---- Building --------------------------------------------------------------

QFrame *TimersPanel::makeSection(const QString &title, const QString &iconName, QVBoxLayout **layoutOut)
{
    auto *card = new QFrame;
    styleFrame(card, QStringLiteral("TimerSection"),
               QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 14px;").arg(Theme::BorderLight));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    header->addWidget(icon(iconName, Theme::AccentBlue, 18));
    header->addWidget(text(title, 14.5, 700, Theme::TextDarkPrimary));
    header->addStretch();
    layout->addLayout(header);
    *layoutOut = layout;
    return card;
}

QWidget *TimersPanel::makeRow(const QString &label, QWidget *body)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *caption = text(label, 13, 500, Theme::TextDarkSecondary);
    caption->setFixedWidth(120);
    caption->setMinimumHeight(34);
    caption->setWordWrap(true);
    caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(caption, 0, Qt::AlignTop);
    layout->addWidget(body, 1);
    return row;
}

QWidget *TimersPanel::buildHeader()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *column = new QVBoxLayout;
    column->setSpacing(4);
    column->addWidget(text(tr("Таймеры и время"), 24, 700, Theme::TextDarkPrimary));
    auto *subtitle = text(tr("Экраны для служения: часы, обратный отсчёт, секундомер и сообщения"), 14, 400,
                          Theme::TextDarkSecondary);
    subtitle->setWordWrap(true);
    column->addWidget(subtitle);
    layout->addLayout(column, 1);

    auto *newButton = makeTimerButton(QStringLiteral("plus"), tr("Новый экран"), Theme::AccentBlue, Theme::AccentBlue,
                                      Theme::TextLightPrimary, 9, 16, false);
    newButton->setToolTip(tr("Создать экран — выберите, что он будет показывать"));
    newButton->onClick = [this, newButton]() {
        QMenu menu(this);
        for (TimerScreenType type : {TimerScreenType::Countdown, TimerScreenType::Time, TimerScreenType::TimeDate,
                                     TimerScreenType::Stopwatch, TimerScreenType::Message}) {
            menu.addAction(IconProvider::icon(TimerFormat::typeIcon(type), QColor(Theme::TextDarkPrimary), 16),
                           TimerFormat::typeName(type), this, [this, type]() { addScreen(type); });
        }
        menu.exec(newButton->mapToGlobal(QPoint(0, newButton->height() + 4)));
    };
    layout->addWidget(newButton, 0, Qt::AlignTop);
    return row;
}

QWidget *TimersPanel::buildScreensHeader()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    m_screensHeading = text(QString(), 16, 700, Theme::TextDarkPrimary);
    layout->addWidget(m_screensHeading);
    layout->addStretch();
    auto *showNames = new CheckRow(tr("Показывать названия"));
    showNames->setChecked(m_showNames);
    showNames->onToggled = [this](bool on) {
        m_showNames = on;
        QSettings().setValue(QStringLiteral("timers/v2/showNames"), on);
        refreshCards();
    };
    layout->addWidget(showNames);
    return row;
}

QWidget *TimersPanel::buildEditorCard()
{
    auto *card = new QFrame;
    styleFrame(card, QStringLiteral("TimerEditor"),
               QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 14px;").arg(Theme::BorderLight));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(16);

    // Header: type badge, editable name + summary | actions
    auto *titleGroup = new QWidget;
    auto *titleLayout = new QHBoxLayout(titleGroup);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);
    auto *badge = new QFrame;
    badge->setFixedSize(34, 34);
    styleFrame(badge, QStringLiteral("TypeBadge"), QStringLiteral("background: %1; border-radius: 9px;").arg(Theme::AccentBlueBg));
    auto *badgeLayout = new QHBoxLayout(badge);
    badgeLayout->setContentsMargins(0, 0, 0, 0);
    m_typeBadgeIcon = new QLabel;
    m_typeBadgeIcon->setStyleSheet(QStringLiteral("background: transparent;"));
    badgeLayout->addWidget(m_typeBadgeIcon, 0, Qt::AlignCenter);
    titleLayout->addWidget(badge, 0, Qt::AlignTop);

    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(2);
    auto *nameRow = new QHBoxLayout;
    nameRow->setSpacing(6);
    m_nameEdit = new QLineEdit;
    m_nameEdit->setMinimumWidth(230);
    m_nameEdit->setToolTip(tr("Название экрана — нажмите, чтобы изменить"));
    m_nameEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { border: none; border-bottom: 1px solid transparent; border-radius: 0; background: transparent;"
        " padding: 0; font-size: 17px; font-weight: 700; color: %1; }"
        "QLineEdit:focus { border-bottom: 1px solid %2; }").arg(Theme::TextDarkPrimary, Theme::AccentBlue));
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty()) {
            m_nameEdit->setText(current().name);
            return;
        }
        if (name != current().name)
            modify([name](TimerScreen &s) { s.name = name; });
    });
    nameRow->addWidget(m_nameEdit, 1);
    nameRow->addWidget(icon(QStringLiteral("pencil"), Theme::TextDarkSecondary, 14));
    titleColumn->addLayout(nameRow);
    m_summary = text(QString(), 12.5, 500, Theme::TextDarkSecondary);
    m_summary->setWordWrap(true);
    titleColumn->addWidget(m_summary);
    titleLayout->addLayout(titleColumn, 1);

    auto *actions = new QWidget;
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    auto *preview = makeTimerButton(QStringLiteral("eye"), tr("Предпросмотр"), QStringLiteral("#ffffff"), Theme::BorderLight,
                                    Theme::TextDarkPrimary, 8, 13, false);
    preview->onClick = [this]() { showPreview(); };
    auto *copy = makeTimerIconButton(QStringLiteral("copy"), tr("Дублировать экран"));
    copy->onClick = [this]() { duplicateScreen(m_selectedId); };
    m_deleteButton = makeTimerIconButton(QStringLiteral("trash-2"), tr("Удалить экран"));
    m_deleteButton->onClick = [this]() { deleteScreen(m_selectedId); };

    auto *onScreen = new ClickFrame;
    styleFrame(onScreen, QStringLiteral("OnScreen"),
               QStringLiteral("background: %1; border: 1px solid %1; border-radius: 9px;").arg(Theme::AccentBlue));
    auto *onScreenLayout = new QHBoxLayout(onScreen);
    onScreenLayout->setContentsMargins(13, 8, 13, 8);
    onScreenLayout->setSpacing(7);
    onScreenLayout->addWidget(icon(QStringLiteral("monitor"), Theme::TextLightPrimary, 14));
    onScreenLayout->addWidget(text(tr("На экран"), 13, 600, Theme::TextLightPrimary));
    auto *chevron = icon(QStringLiteral("chevron-down"), Theme::TextLightPrimary, 14);
    onScreenLayout->addWidget(chevron);
    onScreen->setToolTip(tr("Вывести этот экран на проектор и OBS (стрелка — ещё варианты)"));
    onScreen->onClick = [this, onScreen, chevron]() {
        if (onScreen->mapFromGlobal(QCursor::pos()).x() >= chevron->geometry().left() - 4)
            showOnScreenMenu(onScreen);
        else
            m_engine->goLive(m_selectedId);
    };
    actionsLayout->addWidget(preview);
    actionsLayout->addWidget(copy);
    actionsLayout->addWidget(m_deleteButton);
    actionsLayout->addWidget(onScreen);

    auto *header = new QWidget;
    auto *headerFlow = new FlowLayout(FlowLayout::Mode::Natural, 12, 0, header);
    headerFlow->setSpaceBetween(true);
    headerFlow->setCenterItems(true);
    headerFlow->addWidget(titleGroup);
    headerFlow->addWidget(actions);
    layout->addWidget(header);

    // Body: preview | state + controls
    auto *body = new QWidget;
    auto *bodyFlow = new FlowLayout(FlowLayout::Mode::EqualColumns, 18, 250, body);
    m_preview = new TimerRenderWidget;
    m_preview->setCornerRadius(12);
    bodyFlow->addWidget(new AspectBox(m_preview));

    auto *controls = new QWidget;
    auto *controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(12);

    m_stateBox = new QFrame;
    auto *stateLayout = new QVBoxLayout(m_stateBox);
    stateLayout->setContentsMargins(12, 10, 12, 10);
    stateLayout->setSpacing(3);
    auto *stateRow = new QHBoxLayout;
    stateRow->setSpacing(6);
    m_stateDot = new QLabel;
    m_stateDot->setFixedSize(8, 8);
    stateRow->addWidget(m_stateDot);
    m_stateTitle = new QLabel;
    stateRow->addWidget(m_stateTitle, 1);
    stateLayout->addLayout(stateRow);
    m_stateDetail = new QLabel;
    m_stateDetail->setWordWrap(true);
    stateLayout->addWidget(m_stateDetail);
    controlsLayout->addWidget(m_stateBox);

    m_mainControls = new QWidget;
    auto *mainLayout = new QHBoxLayout(m_mainControls);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(8);
    auto *start = makeTimerButton(QStringLiteral("play"), tr("Старт"), Theme::AccentGreen, Theme::AccentGreen,
                                  Theme::TextLightPrimary, 8, 10, true);
    auto *pause = makeTimerButton(QStringLiteral("pause"), tr("Пауза"), QStringLiteral("#ffffff"), Theme::BorderLight,
                                  Theme::TextDarkPrimary, 8, 10, true);
    auto *reset = makeTimerButton(QStringLiteral("rotate-ccw"), tr("Сброс"), QStringLiteral("#ffffff"), Theme::BorderLight,
                                  Theme::TextDarkPrimary, 8, 10, true);
    start->onClick = [this]() { m_engine->start(m_selectedId); };
    pause->onClick = [this]() { m_engine->pause(m_selectedId); };
    reset->onClick = [this]() { m_engine->reset(m_selectedId); };
    mainLayout->addWidget(start, 1);
    mainLayout->addWidget(pause, 1);
    mainLayout->addWidget(reset, 1);
    controlsLayout->addWidget(m_mainControls);

    m_adjustBlock = new QWidget;
    auto *adjustLayout = new QVBoxLayout(m_adjustBlock);
    adjustLayout->setContentsMargins(0, 0, 0, 0);
    adjustLayout->setSpacing(8);
    adjustLayout->addWidget(text(tr("Быстро подправить"), 12, 500, Theme::TextDarkSecondary));
    auto *adjustRow = new QHBoxLayout;
    adjustRow->setSpacing(8);
    const QList<QPair<QString, qint64>> adjustments = {
        {tr("−1 мин"), -Minute}, {tr("+1 мин"), Minute}, {tr("+5 мин"), 5 * Minute}, {tr("+10 мин"), 10 * Minute}};
    for (const auto &adjustment : adjustments) {
        auto *button = makeTimerButton(QString(), adjustment.first, QStringLiteral("#ffffff"), Theme::BorderLight,
                                       Theme::TextDarkPrimary, 7, 4, true);
        button->onClick = [this, delta = adjustment.second]() { m_engine->adjust(m_selectedId, delta); };
        adjustRow->addWidget(button, 1);
    }
    adjustLayout->addLayout(adjustRow);
    controlsLayout->addWidget(m_adjustBlock);

    auto *hintRow = new QHBoxLayout;
    hintRow->setSpacing(8);
    hintRow->addWidget(icon(QStringLiteral("keyboard"), Theme::TextDarkSecondary, 14), 0, Qt::AlignTop);
    hintRow->addWidget(hint(tr("Клавиши 1–9 или двойной щелчок по карточке — вывести экран на проектор")), 1);
    controlsLayout->addLayout(hintRow);
    controlsLayout->addStretch();
    bodyFlow->addWidget(controls);
    layout->addWidget(body);
    return card;
}

QWidget *TimersPanel::buildContentSection()
{
    QVBoxLayout *layout = nullptr;
    auto *section = makeSection(tr("Что показывать"), QStringLiteral("layout-template"), &layout);

    // Тип экрана
    QList<QWidget *> chips;
    for (TimerScreenType type : {TimerScreenType::Time, TimerScreenType::TimeDate, TimerScreenType::Countdown,
                                 TimerScreenType::Stopwatch, TimerScreenType::Message}) {
        auto *chip = new ChoiceButton(TimerFormat::typeIcon(type), TimerFormat::typeName(type));
        chip->onClick = [this, type]() {
            modify([type](TimerScreen &s) {
                s.type = type;
                if (type == TimerScreenType::Message && s.message.trimmed().isEmpty())
                    s.message = s.name;
            });
        };
        m_typeButtons << chip;
        chips << chip;
    }
    layout->addWidget(makeRow(tr("Тип экрана"), flowRow(chips, 8)));

    // Считать до
    auto *targetBody = new QWidget;
    auto *targetLayout = new QVBoxLayout(targetBody);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setSpacing(8);
    m_targetSeg = new SegmentedControl({TimerFormat::targetName(CountdownTarget::Duration),
                                        TimerFormat::targetName(CountdownTarget::TimeOfDay),
                                        TimerFormat::targetName(CountdownTarget::DateTime)});
    m_targetSeg->onChanged = [this](int index) {
        modify([index](TimerScreen &s) { s.target = static_cast<CountdownTarget>(index); });
    };
    targetLayout->addWidget(m_targetSeg, 0, Qt::AlignLeft);

    m_durationEdit = makeTimeEdit(QStringLiteral("HH:mm:ss"), 110);
    m_durationEdit->setToolTip(tr("Длительность: часы:минуты:секунды"));
    connect(m_durationEdit, &QTimeEdit::timeChanged, this, [this](const QTime &time) {
        const qint64 ms = time.msecsSinceStartOfDay();
        if (ms > 0)
            modify([ms](TimerScreen &s) { s.durationMs = ms; });
    });
    m_targetTimeEdit = makeTimeEdit(QStringLiteral("HH:mm"), 90);
    connect(m_targetTimeEdit, &QTimeEdit::timeChanged, this, [this](const QTime &time) {
        modify([time](TimerScreen &s) { s.targetTime = time; });
    });
    m_repeatField = new DropdownField;
    m_repeatField->setLeadingIcon(QStringLiteral("repeat"));
    m_repeatField->setOptions(TimerFormat::weekdayChoices());
    m_repeatField->onSelected = [this](int index) { modify([index](TimerScreen &s) { s.repeatWeekday = index; }); };
    m_targetDateEdit = new QDateTimeEdit;
    m_targetDateEdit->setDisplayFormat(QStringLiteral("dd.MM.yyyy  HH:mm"));
    m_targetDateEdit->setCalendarPopup(true);
    m_targetDateEdit->setFixedHeight(34);
    m_targetDateEdit->setMinimumWidth(190);
    connect(m_targetDateEdit, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &dateTime) {
        modify([dateTime](TimerScreen &s) { s.targetDateTime = dateTime; });
    });
    targetLayout->addWidget(flowRow({m_durationEdit, m_targetTimeEdit, m_repeatField, m_targetDateEdit}, 8));
    m_targetHint = hint(QString());
    targetLayout->addWidget(m_targetHint);
    m_targetRow = makeRow(tr("Считать до"), targetBody);
    layout->addWidget(m_targetRow);

    // Формат
    auto *formatBody = new QWidget;
    auto *formatLayout = new QVBoxLayout(formatBody);
    formatLayout->setContentsMargins(0, 0, 0, 0);
    formatLayout->setSpacing(8);
    QStringList formats;
    for (int i = 0; i < 5; ++i)
        formats << TimerFormat::formatName(static_cast<CountdownFormat>(i));
    m_formatSeg = new SegmentedControl(formats);
    m_formatSeg->onChanged = [this](int index) {
        modify([index](TimerScreen &s) { s.format = static_cast<CountdownFormat>(index); });
    };
    formatLayout->addWidget(m_formatSeg, 0, Qt::AlignLeft);
    m_formatHint = hint(tr("Авто: «12 дн 04:32» → «04:32:10» → «12:48» по мере приближения."));
    formatLayout->addWidget(m_formatHint);
    m_formatRow = makeRow(tr("Формат"), formatBody);
    layout->addWidget(m_formatRow);

    // Часы
    m_faceSeg = new SegmentedControl({TimerFormat::faceName(ClockFace::Digital), TimerFormat::faceName(ClockFace::Analog),
                                      TimerFormat::faceName(ClockFace::Both)});
    m_faceSeg->onChanged = [this](int index) {
        static const ClockFace faces[] = {ClockFace::Digital, ClockFace::Analog, ClockFace::Both};
        const ClockFace face = faces[index];
        modify([face](TimerScreen &s) { s.face = face; });
    };
    auto *faceBody = new QWidget;
    auto *faceLayout = new QHBoxLayout(faceBody);
    faceLayout->setContentsMargins(0, 0, 0, 0);
    faceLayout->addWidget(m_faceSeg);
    faceLayout->addStretch();
    m_faceRow = makeRow(tr("Вид часов"), faceBody);
    layout->addWidget(m_faceRow);

    m_use24hToggle = makeToggle();
    m_secondsToggle = makeToggle();
    connect(m_use24hToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.use24h = on; }); });
    connect(m_secondsToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.showSeconds = on; }); });
    m_clockOptionsRow = makeRow(tr("Формат времени"),
                                flowRow({m_use24hToggle, text(tr("24 часа"), 13, 500, Theme::TextDarkPrimary),
                                         m_secondsToggle, text(tr("Секунды"), 13, 500, Theme::TextDarkPrimary)}));
    layout->addWidget(m_clockOptionsRow);

    m_timeZoneField = new DropdownField(true);
    m_timeZoneField->setLeadingIcon(QStringLiteral("globe"));
    m_timeZoneField->setMinimumWidth(200);
    m_timeZoneField->setMaximumWidth(280);
    const QList<QByteArray> zones = TimerFormat::timeZoneChoices();
    QStringList zoneNames;
    for (const QByteArray &zone : zones)
        zoneNames << TimerFormat::timeZoneLabel(zone);
    m_timeZoneField->setOptions(zoneNames);
    m_timeZoneField->onSelected = [this, zones](int index) {
        const QByteArray zone = zones.value(index);
        modify([zone](TimerScreen &s) { s.timeZoneId = zone; });
    };
    auto *zoneBody = new QWidget;
    auto *zoneLayout = new QHBoxLayout(zoneBody);
    zoneLayout->setContentsMargins(0, 0, 0, 0);
    zoneLayout->addWidget(m_timeZoneField, 1);
    zoneLayout->addStretch();
    m_timeZoneRow = makeRow(tr("Часовой пояс"), zoneBody);
    layout->addWidget(m_timeZoneRow);
    return section;
}

QWidget *TimersPanel::buildTextSection()
{
    QVBoxLayout *layout = nullptr;
    auto *section = makeSection(tr("Текст на экране"), QStringLiteral("type"), &layout);

    m_messageEdit = new QPlainTextEdit;
    m_messageEdit->setFixedHeight(76);
    m_messageEdit->setPlaceholderText(tr("Например: Время молитвы"));
    connect(m_messageEdit, &QPlainTextEdit::textChanged, this, [this]() {
        const QString message = m_messageEdit->toPlainText();
        modify([message](TimerScreen &s) { s.message = message; });
    });
    m_messageBlock = labelledColumn(tr("Сообщение"), m_messageEdit);
    layout->addWidget(m_messageBlock);

    const auto makeLine = [this](const QString &placeholder, std::function<void(TimerScreen &, const QString &)> apply) {
        auto *edit = new QLineEdit;
        edit->setPlaceholderText(placeholder);
        edit->setFixedHeight(34);
        connect(edit, &QLineEdit::textEdited, this, [this, apply](const QString &value) {
            modify([&apply, &value](TimerScreen &s) { apply(s, value); });
        });
        return edit;
    };
    m_titleEdit = makeLine(tr("Например: До начала"), [](TimerScreen &s, const QString &v) { s.title = v; });
    m_subtitleEdit = makeLine(tr("Например: Молодёжное служение"), [](TimerScreen &s, const QString &v) { s.subtitle = v; });
    m_endMessageEdit = makeLine(tr("Например: Начинаем! 🔥"), [](TimerScreen &s, const QString &v) { s.endMessage = v; });

    auto *fields = new QWidget;
    auto *flow = new FlowLayout(FlowLayout::Mode::EqualColumns, 14, 170, fields);
    flow->addWidget(labelledColumn(tr("Заголовок сверху"), m_titleEdit));
    flow->addWidget(labelledColumn(tr("Подпись снизу"), m_subtitleEdit));
    m_endMessageColumn = labelledColumn(tr("Когда время вышло"), m_endMessageEdit);
    flow->addWidget(m_endMessageColumn);
    layout->addWidget(fields);
    layout->addWidget(hint(tr("Можно оставить пустым. Эмодзи тоже работают 🙂")));
    return section;
}

QWidget *TimersPanel::buildStyleSection()
{
    QVBoxLayout *layout = nullptr;
    auto *section = makeSection(tr("Оформление"), QStringLiteral("palette"), &layout);

    auto *thumbs = new QWidget;
    auto *thumbsFlow = new FlowLayout(FlowLayout::Mode::EqualColumns, 12, 84, thumbs);
    for (int i = 0; i < 7; ++i) {
        auto *thumb = new BackgroundThumb(static_cast<TimerBackground>(i));
        thumb->onClick = [this, thumb]() {
            if (thumb->background() == TimerBackground::Custom && current().customBackgroundPath.isEmpty()) {
                chooseCustomBackground();
                return;
            }
            const TimerBackground background = thumb->background();
            modify([background](TimerScreen &s) { s.background = background; });
        };
        if (thumb->background() == TimerBackground::Custom) {
            thumb->onContextMenu = [this](const QPoint &pos) {
                QMenu menu(this);
                menu.addAction(tr("Выбрать другое изображение…"), this, &TimersPanel::chooseCustomBackground);
                menu.exec(pos);
            };
        }
        m_backgroundThumbs << thumb;
        thumbsFlow->addWidget(thumb);
    }
    layout->addWidget(thumbs);

    auto *fields = new QWidget;
    auto *fieldsFlow = new FlowLayout(FlowLayout::Mode::EqualColumns, 12, 140, fields);
    m_fontField = new DropdownField;
    m_fontField->setLeadingIcon(QStringLiteral("type"));
    const QList<QPair<QString, QString>> fonts = TimerFormat::fontChoices();
    QStringList fontNames;
    for (const auto &font : fonts)
        fontNames << font.second;
    m_fontField->setOptions(fontNames);
    m_fontField->onSelected = [this, fonts](int index) {
        const QString family = fonts.value(index).first;
        modify([family](TimerScreen &s) { s.fontFamily = family; });
    };
    fieldsFlow->addWidget(labelledColumn(tr("Шрифт"), m_fontField));

    m_colorField = new DropdownField;
    QStringList colors;
    for (int i = 0; i < 4; ++i)
        colors << TimerFormat::textColorName(static_cast<TimerTextColor>(i));
    m_colorField->setOptions(colors);
    m_colorField->onSelected = [this](int index) {
        modify([index](TimerScreen &s) { s.textColor = static_cast<TimerTextColor>(index); });
    };
    fieldsFlow->addWidget(labelledColumn(tr("Цвет текста"), m_colorField));

    m_sizeField = new DropdownField;
    m_sizeField->setLeadingIcon(QStringLiteral("case-sensitive"));
    QStringList sizes;
    for (int i = 0; i < 4; ++i)
        sizes << TimerFormat::fontSizeName(static_cast<TimerFontSize>(i));
    m_sizeField->setOptions(sizes);
    m_sizeField->onSelected = [this](int index) {
        modify([index](TimerScreen &s) { s.fontSize = static_cast<TimerFontSize>(index); });
    };
    fieldsFlow->addWidget(labelledColumn(tr("Размер"), m_sizeField));

    m_alignField = new DropdownField;
    QStringList aligns;
    for (int i = 0; i < 3; ++i)
        aligns << TimerFormat::alignName(static_cast<TimerAlign>(i));
    m_alignField->setOptions(aligns);
    m_alignField->onSelected = [this](int index) {
        modify([index](TimerScreen &s) { s.align = static_cast<TimerAlign>(index); });
    };
    fieldsFlow->addWidget(labelledColumn(tr("Выравнивание"), m_alignField));
    layout->addWidget(fields);

    m_dimSlider = new QSlider(Qt::Horizontal);
    m_dimSlider->setRange(0, 90);
    m_dimSlider->setCursor(Qt::PointingHandCursor);
    m_dimLabel = text(QString(), 13, 600, Theme::TextDarkPrimary);
    m_dimLabel->setFixedWidth(40);
    m_dimLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_dimSlider, &QSlider::valueChanged, this, [this](int value) {
        m_dimLabel->setText(QStringLiteral("%1%").arg(value));
        modify([value](TimerScreen &s) { s.dimPercent = value; });
    });
    auto *dimBody = new QWidget;
    auto *dimLayout = new QHBoxLayout(dimBody);
    dimLayout->setContentsMargins(0, 7, 0, 7);
    dimLayout->setSpacing(12);
    dimLayout->addWidget(m_dimSlider, 1);
    dimLayout->addWidget(m_dimLabel);
    auto *dimRow = makeRow(tr("Затемнение фона"), dimBody);
    dimRow->setToolTip(tr("Насколько затемнить фото или видео, чтобы цифры читались лучше"));
    layout->addWidget(dimRow);

    m_shadowToggle = makeToggle();
    connect(m_shadowToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.textShadow = on; }); });
    layout->addWidget(makeRow(tr("Тень под текстом"), flowRow({m_shadowToggle})));

    m_progressToggle = makeToggle();
    connect(m_progressToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.showProgress = on; }); });
    m_progressRow = makeRow(tr("Полоска прогресса"), flowRow({m_progressToggle}));
    layout->addWidget(m_progressRow);
    return section;
}

QWidget *TimersPanel::buildBehaviourSection()
{
    QVBoxLayout *layout = nullptr;
    auto *section = makeSection(tr("Поведение"), QStringLiteral("sliders-horizontal"), &layout);

    // Когда время вышло
    auto *endBody = new QWidget;
    auto *endLayout = new QVBoxLayout(endBody);
    endLayout->setContentsMargins(0, 0, 0, 0);
    endLayout->setSpacing(8);
    QStringList actions;
    for (int i = 0; i < 4; ++i)
        actions << TimerFormat::endActionName(static_cast<TimerEndAction>(i));
    m_endActionSeg = new SegmentedControl(actions);
    m_endActionSeg->onChanged = [this](int index) {
        modify([index](TimerScreen &s) { s.endAction = static_cast<TimerEndAction>(index); });
    };
    endLayout->addWidget(m_endActionSeg, 0, Qt::AlignLeft);
    m_switchField = new DropdownField(true);
    m_switchField->setLeadingIcon(QStringLiteral("monitor"));
    m_switchField->setMinimumWidth(220);
    m_switchField->setMaximumWidth(320);
    m_switchField->setToolTip(tr("Экран, на который переключиться, когда время выйдет"));
    m_switchField->onSelected = [this](int index) {
        const QString target = m_switchTargets.value(index);
        modify([target](TimerScreen &s) { s.switchToScreenId = target; });
    };
    endLayout->addWidget(m_switchField, 0, Qt::AlignLeft);
    m_endActionRow = makeRow(tr("Когда время вышло"), endBody);
    layout->addWidget(m_endActionRow);

    // Сигнал
    m_soundToggle = makeToggle();
    connect(m_soundToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.soundEnabled = on; }); });
    m_soundField = new DropdownField(true);
    m_soundField->setLeadingIcon(QStringLiteral("bell"));
    m_soundField->setFixedWidth(160);
    m_soundField->setOptions(TimerSound::names());
    m_soundField->onSelected = [this](int index) {
        TimerSound::play(index); // let the operator hear what they picked
        modify([index](TimerScreen &s) { s.soundIndex = index; });
    };
    m_soundRow = makeRow(tr("Сигнал"), flowRow({m_soundToggle, m_soundField}));
    layout->addWidget(m_soundRow);

    // Предупреждение цветом
    m_warnToggle = makeToggle();
    connect(m_warnToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.warnEnabled = on; }); });
    const auto makeWarnChip = [this](const QString &color, bool yellow, QLabel **labelOut) {
        auto *chip = new ClickFrame;
        chip->setToolTip(tr("Нажмите, чтобы выбрать, за сколько минут"));
        styleFrame(chip, QStringLiteral("WarnChip"),
                   QStringLiteral("background: #ffffff; border: 1px solid %1; border-radius: 9px;").arg(Theme::BorderLight));
        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(11, 7, 11, 7);
        chipLayout->setSpacing(8);
        auto *dot = new QLabel;
        dot->setFixedSize(12, 12);
        dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 6px;").arg(color));
        chipLayout->addWidget(dot);
        *labelOut = text(QString(), 12.5, 500, Theme::TextDarkPrimary);
        chipLayout->addWidget(*labelOut);
        chipLayout->addWidget(icon(QStringLiteral("chevron-down"), Theme::TextDarkSecondary, 12));
        chip->onClick = [this, chip, yellow]() { chooseWarnMinutes(yellow, chip); };
        return chip;
    };
    auto *yellowChip = makeWarnChip(QStringLiteral("#facc15"), true, &m_warnYellowLabel);
    auto *redChip = makeWarnChip(QStringLiteral("#ef4444"), false, &m_warnRedLabel);
    m_warnRow = makeRow(tr("Предупреждение"), flowRow({m_warnToggle, yellowChip, redChip}));
    m_warnRow->setToolTip(tr("Цифры меняют цвет, когда времени остаётся мало"));
    layout->addWidget(m_warnRow);

    // Автозапуск
    m_autoStartToggle = makeToggle();
    connect(m_autoStartToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.autoStart = on; }); });
    m_autoStartRow = makeRow(tr("Автозапуск"),
                             flowRow({m_autoStartToggle, hint(tr("запускать отсчёт, когда экран выводится"))}));
    layout->addWidget(m_autoStartRow);

    // По расписанию
    m_scheduleToggle = makeToggle();
    connect(m_scheduleToggle, &ToggleSwitch::toggled, this, [this](bool on) { modify([on](TimerScreen &s) { s.scheduleEnabled = on; }); });
    m_scheduleTimeEdit = makeTimeEdit(QStringLiteral("HH:mm"), 80);
    connect(m_scheduleTimeEdit, &QTimeEdit::timeChanged, this, [this](const QTime &time) {
        modify([time](TimerScreen &s) { s.scheduleTime = time; });
    });
    m_scheduleDayField = new DropdownField;
    m_scheduleDayField->setLeadingIcon(QStringLiteral("repeat"));
    m_scheduleDayField->setOptions(TimerFormat::weekdayChoices());
    m_scheduleDayField->onSelected = [this](int index) { modify([index](TimerScreen &s) { s.scheduleWeekday = index; }); };
    auto *scheduleRow = makeRow(tr("По расписанию"),
                                flowRow({m_scheduleToggle, text(tr("в"), 13, 500, Theme::TextDarkSecondary),
                                         m_scheduleTimeEdit, m_scheduleDayField}));
    scheduleRow->setToolTip(tr("В это время экран сам выведется на проектор и OBS (приложение должно быть открыто)"));
    layout->addWidget(scheduleRow);
    return section;
}

// ---- Cards -----------------------------------------------------------------

void TimersPanel::rebuildCards()
{
    auto *flow = static_cast<FlowLayout *>(m_cardsRow->layout());
    while (QLayoutItem *item = flow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_cards.clear();

    const QList<TimerScreen> &screens = m_engine->screens();
    for (int i = 0; i < screens.size(); ++i) {
        const QString id = screens.at(i).id;
        Card card;
        card.id = id;
        card.frame = new ClickFrame;
        auto *layout = new QVBoxLayout(card.frame);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(7);
        card.render = new TimerRenderWidget;
        card.render->setFixedHeight(88);
        card.render->setCornerRadius(12);
        card.render->setPlayVideo(false); // thumbnails use the still frame
        layout->addWidget(card.render);
        card.caption = text(QString(), 11.5, 500, Theme::TextDarkPrimary);
        card.caption->setAlignment(Qt::AlignCenter);
        card.caption->setWordWrap(true);
        layout->addWidget(card.caption);
        card.number = text(QString::number(i + 1), 11.5, 500, Theme::TextDarkSecondary);
        card.number->setAlignment(Qt::AlignCenter);
        layout->addWidget(card.number);
        card.frame->setToolTip(i < 9 ? tr("Щелчок — настроить · двойной щелчок или клавиша %1 — на экран").arg(i + 1)
                                     : tr("Щелчок — настроить · двойной щелчок — на экран"));
        card.frame->onClick = [this, id]() { select(id); };
        card.frame->onDoubleClick = [this, id]() { m_engine->goLive(id); };
        card.frame->onContextMenu = [this, id](const QPoint &pos) { showCardMenu(id, pos); };
        m_cards << card;
        flow->addWidget(card.frame);
    }
    m_deleteButton->setEnabled(screens.size() > 1);
    refreshCards();
    m_cardsRow->updateGeometry();
}

void TimersPanel::refreshCards()
{
    const QString live = m_engine->liveScreenId();
    for (const Card &card : std::as_const(m_cards)) {
        const TimerSlide slide = m_engine->slide(card.id);
        card.render->setSlide(slide);
        card.render->setSelected(card.id == m_selectedId);
        card.render->setLiveBadge(card.id == live);
        card.caption->setText(slide.screen.name);
        card.caption->setVisible(m_showNames);
        card.number->setVisible(m_showNames);
    }
    m_screensHeading->setText(tr("Мои экраны (%1)").arg(m_engine->screens().size()));
}

// ---- Editor ----------------------------------------------------------------

TimerScreen TimersPanel::current() const
{
    return m_engine->screen(m_selectedId);
}

void TimersPanel::modify(const std::function<void(TimerScreen &)> &change)
{
    if (m_updating)
        return;
    TimerScreen screen = current();
    if (screen.id.isEmpty())
        return;
    change(screen);
    m_engine->updateScreen(screen);
}

void TimersPanel::select(const QString &id)
{
    if (m_engine->indexOf(id) < 0)
        return;
    m_selectedId = id;
    QSettings().setValue(QStringLiteral("timers/v2/selected"), id);
    refreshCards();
    refreshEditor();
}

void TimersPanel::refreshEditor()
{
    const TimerScreen s = current();
    if (s.id.isEmpty())
        return;
    m_updating = true;

    // Header + preview
    m_typeBadgeIcon->setPixmap(IconProvider::pixmap(TimerFormat::typeIcon(s.type), QColor(Theme::AccentBlue), 17));
    if (!m_nameEdit->hasFocus())
        m_nameEdit->setText(s.name);
    m_summary->setText(TimerFormat::summary(s));
    m_preview->setSlide(m_engine->slide(s.id));

    // Что показывать
    static const QList<TimerScreenType> typeOrder = {TimerScreenType::Time, TimerScreenType::TimeDate,
                                                     TimerScreenType::Countdown, TimerScreenType::Stopwatch,
                                                     TimerScreenType::Message};
    for (int i = 0; i < m_typeButtons.size(); ++i)
        m_typeButtons.at(i)->setChecked(typeOrder.value(i) == s.type);

    m_targetSeg->setCurrent(static_cast<int>(s.target));
    if (!m_durationEdit->hasFocus())
        m_durationEdit->setTime(QTime(0, 0).addMSecs(int(qBound(qint64(1000), s.durationMs, qint64(86399000)))));
    if (!m_targetTimeEdit->hasFocus())
        m_targetTimeEdit->setTime(s.targetTime);
    m_repeatField->setCurrent(s.repeatWeekday);
    if (!m_targetDateEdit->hasFocus())
        m_targetDateEdit->setDateTime(s.targetDateTime);
    m_durationEdit->setVisible(s.target == CountdownTarget::Duration);
    m_targetTimeEdit->setVisible(s.target == CountdownTarget::TimeOfDay);
    m_repeatField->setVisible(s.target == CountdownTarget::TimeOfDay);
    m_targetDateEdit->setVisible(s.target == CountdownTarget::DateTime);
    switch (s.target) {
    case CountdownTarget::Duration:
        m_targetHint->setText(tr("Запускается кнопкой «Старт» (или сам — см. «Автозапуск» ниже)."));
        break;
    case CountdownTarget::TimeOfDay:
        m_targetHint->setText(tr("Отсчёт всегда до ближайших %1 — не нужно перезапускать каждую неделю.")
                                  .arg(s.targetTime.toString(QStringLiteral("HH:mm"))));
        break;
    case CountdownTarget::DateTime:
        m_targetHint->setText(tr("Для больших событий: лагерь, конференция, Пасха — «12 дн 04:32»."));
        break;
    }
    m_formatSeg->setCurrent(static_cast<int>(s.format));
    m_faceSeg->setCurrent(s.face == ClockFace::Digital ? 0 : s.face == ClockFace::Analog ? 1 : 2);
    m_use24hToggle->setChecked(s.use24h);
    m_secondsToggle->setChecked(s.showSeconds);
    m_timeZoneField->setCurrent(qMax(0, int(TimerFormat::timeZoneChoices().indexOf(s.timeZoneId))));

    // Текст
    if (m_messageEdit->toPlainText() != s.message)
        m_messageEdit->setPlainText(s.message);
    if (m_titleEdit->text() != s.title)
        m_titleEdit->setText(s.title);
    if (m_subtitleEdit->text() != s.subtitle)
        m_subtitleEdit->setText(s.subtitle);
    if (m_endMessageEdit->text() != s.endMessage)
        m_endMessageEdit->setText(s.endMessage);

    // Оформление
    for (BackgroundThumb *thumb : std::as_const(m_backgroundThumbs)) {
        thumb->setSelected(thumb->background() == s.background);
        if (thumb->background() == TimerBackground::Custom)
            thumb->setCustomImage(s.customBackgroundPath);
    }
    const QList<QPair<QString, QString>> fonts = TimerFormat::fontChoices();
    int fontIndex = 0;
    for (int i = 0; i < fonts.size(); ++i) {
        if (fonts.at(i).first == s.fontFamily)
            fontIndex = i;
    }
    m_fontField->setCurrent(fontIndex);
    m_colorField->setCurrent(static_cast<int>(s.textColor));
    m_colorField->setSwatch(TimerFormat::textColor(s.textColor));
    m_sizeField->setCurrent(static_cast<int>(s.fontSize));
    m_alignField->setCurrent(static_cast<int>(s.align));
    m_alignField->setLeadingIcon(TimerFormat::alignIcon(s.align));
    m_dimSlider->setValue(s.dimPercent);
    m_dimLabel->setText(QStringLiteral("%1%").arg(s.dimPercent));
    m_shadowToggle->setChecked(s.textShadow);
    m_progressToggle->setChecked(s.showProgress);

    // Поведение
    m_endActionSeg->setCurrent(static_cast<int>(s.endAction));
    m_switchTargets.clear();
    QStringList switchNames;
    int switchIndex = -1;
    for (const TimerScreen &other : m_engine->screens()) {
        if (other.id == s.id)
            continue;
        if (other.id == s.switchToScreenId)
            switchIndex = m_switchTargets.size();
        m_switchTargets << other.id;
        switchNames << other.name;
    }
    m_switchField->setOptions(switchNames);
    m_switchField->setCurrent(switchIndex);
    m_switchField->setVisible(s.endAction == TimerEndAction::SwitchScreen);
    m_soundToggle->setChecked(s.soundEnabled);
    m_soundField->setCurrent(qBound(0, s.soundIndex, int(TimerSound::names().size()) - 1));
    m_warnToggle->setChecked(s.warnEnabled);
    m_warnYellowLabel->setText(tr("за %1 — жёлтый").arg(minutesLabel(s.warnYellowMinutes)));
    m_warnRedLabel->setText(tr("за %1 — красный").arg(minutesLabel(s.warnRedMinutes)));
    m_autoStartToggle->setChecked(s.autoStart);
    m_scheduleToggle->setChecked(s.scheduleEnabled);
    if (!m_scheduleTimeEdit->hasFocus())
        m_scheduleTimeEdit->setTime(s.scheduleTime);
    m_scheduleDayField->setCurrent(s.scheduleWeekday);

    // Only the rows that make sense for this kind of screen.
    const bool countdown = s.isCountdown();
    m_targetRow->setVisible(countdown);
    m_formatRow->setVisible(countdown || s.type == TimerScreenType::Stopwatch);
    m_formatHint->setVisible(countdown);
    m_faceRow->setVisible(s.isClock());
    m_clockOptionsRow->setVisible(s.isClock());
    m_timeZoneRow->setVisible(s.isClock());
    m_messageBlock->setVisible(s.type == TimerScreenType::Message);
    m_endMessageColumn->setVisible(countdown);
    m_progressRow->setVisible(countdown && s.target != CountdownTarget::DateTime);
    m_endActionRow->setVisible(countdown);
    m_soundRow->setVisible(countdown);
    m_warnRow->setVisible(countdown);
    m_autoStartRow->setVisible(s.hasStartStop());
    m_mainControls->setVisible(s.hasStartStop());
    m_adjustBlock->setVisible(countdown || s.type == TimerScreenType::Stopwatch);
    m_deleteButton->setEnabled(m_engine->screens().size() > 1);

    m_updating = false;
    refreshState();
}

void TimersPanel::refreshState()
{
    const TimerSlide slide = m_engine->slide(m_selectedId);
    const TimerScreen &s = slide.screen;
    if (s.id.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto plain = [](QString text) { return text.remove(QLatin1Char('+')); };

    enum Tone { Green, Gray, Amber, Red };
    Tone tone = Gray;
    QString title;
    QString detail;

    switch (s.type) {
    case TimerScreenType::Time:
    case TimerScreenType::TimeDate:
        tone = Green;
        title = tr("Часы идут");
        detail = tr("Сейчас %1 · %2").arg(TimerFormat::time(TimerFormat::clockNow(s), s.use24h, true),
                                          TimerFormat::timeZoneLabel(s.timeZoneId));
        break;
    case TimerScreenType::Message:
        title = tr("Сообщение");
        detail = tr("Показывает текст без таймера");
        break;
    case TimerScreenType::Stopwatch:
        tone = slide.run.running ? Green : Gray;
        title = slide.run.running ? tr("Идёт") : (slide.run.valueMs > 0 ? tr("На паузе") : tr("Готов к запуску"));
        detail = tr("Прошло %1").arg(plain(TimerFormat::durationText(-TimerFormat::stopwatchElapsed(slide.run, now),
                                                                     CountdownFormat::Auto)));
        break;
    case TimerScreenType::Countdown: {
        const qint64 remaining = TimerFormat::countdownRemaining(slide, now);
        const QString left = TimerFormat::durationText(qMax<qint64>(0, remaining), CountdownFormat::Auto);
        if (s.target == CountdownTarget::Duration) {
            if (slide.run.running && remaining > 0) {
                tone = Green;
                title = tr("Идёт");
                detail = tr("Закончится в %1 · осталось %2")
                             .arg(QDateTime::fromMSecsSinceEpoch(slide.run.anchorMs).toString(QStringLiteral("HH:mm")), left);
            } else if (remaining <= 0) {
                tone = Red;
                title = slide.run.running ? tr("Время вышло · считаем дальше") : tr("Время вышло");
                detail = slide.run.running ? tr("Прошло %1 сверх времени").arg(plain(TimerFormat::durationText(remaining, CountdownFormat::Auto)))
                                           : tr("«Старт» — начать заново");
            } else if (remaining != s.durationMs) {
                tone = Amber;
                title = tr("На паузе");
                detail = tr("Осталось %1 — «Старт» продолжит").arg(left);
            } else {
                title = tr("Готов к запуску");
                detail = tr("%1 · нажмите «Старт»").arg(left);
            }
        } else {
            const QDateTime target = TimerFormat::nextTarget(s, now);
            if (remaining > 0) {
                tone = Green;
                title = tr("Идёт отсчёт");
                detail = tr("До %1 · осталось %2").arg(target.toString(QStringLiteral("dd.MM HH:mm")), left);
            } else {
                tone = Red;
                title = tr("Время вышло");
                detail = tr("В %1").arg(target.toString(QStringLiteral("dd.MM HH:mm")));
            }
        }
        break;
    }
    }

    if (m_engine->liveScreenId() == s.id)
        title += tr(" · в эфире");

    static const QString backgrounds[] = {QStringLiteral("#e8f9ee"), QStringLiteral("#f3f4f6"), QStringLiteral("#fef9c3"),
                                          QStringLiteral("#fee2e2")};
    static const QString foregrounds[] = {QStringLiteral("#15803d"), QStringLiteral("#4b5563"), QStringLiteral("#a16207"),
                                          QStringLiteral("#b91c1c")};
    styleFrame(m_stateBox, QStringLiteral("StateBox"), QStringLiteral("background: %1; border-radius: 10px;").arg(backgrounds[tone]));
    m_stateDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(foregrounds[tone]));
    m_stateTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: 13px; font-weight: 700;").arg(foregrounds[tone]));
    m_stateDetail->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: 12.5px; font-weight: 500;").arg(foregrounds[tone]));
    m_stateTitle->setText(title);
    m_stateDetail->setText(detail);
}

// ---- Actions ---------------------------------------------------------------

void TimersPanel::addScreen(TimerScreenType type)
{
    const TimerScreen base = current();
    TimerScreen screen = TimerEngine::makeScreen(TimerFormat::typeName(type), type);
    // Start from the current screen's look so a set of screens stays consistent.
    screen.background = base.background;
    screen.customBackgroundPath = base.customBackgroundPath;
    screen.fontFamily = base.fontFamily;
    screen.textColor = base.textColor;
    screen.fontSize = base.fontSize;
    screen.align = base.align;
    screen.dimPercent = base.dimPercent;
    screen.textShadow = base.textShadow;
    if (type == TimerScreenType::Message)
        screen.message = tr("Текст сообщения");
    const QString id = m_engine->addScreen(screen, m_engine->indexOf(m_selectedId) + 1);
    select(id);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void TimersPanel::duplicateScreen(const QString &id)
{
    TimerScreen copy = m_engine->screen(id);
    if (copy.id.isEmpty())
        return;
    copy.id.clear();
    copy.name = tr("%1 (копия)").arg(copy.name);
    select(m_engine->addScreen(copy, m_engine->indexOf(id) + 1));
}

void TimersPanel::deleteScreen(const QString &id)
{
    if (m_engine->screens().size() <= 1)
        return;
    const TimerScreen screen = m_engine->screen(id);
    if (QMessageBox::question(this, tr("Удалить экран"), tr("Удалить экран «%1»?").arg(screen.name)) != QMessageBox::Yes)
        return;
    const int index = m_engine->indexOf(id);
    m_engine->removeScreen(id);
    const QList<TimerScreen> &screens = m_engine->screens();
    select(screens.at(qMin(index, int(screens.size()) - 1)).id);
}

void TimersPanel::showPreview()
{
    const TimerSlide slide = m_engine->slide(m_selectedId);
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Предпросмотр — %1").arg(slide.screen.name));
    dialog->resize(800, 450);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *render = new SlideRenderWidget(dialog);
    SlideContent content;
    content.kind = SlideKind::Timer;
    content.timer = slide;
    render->setContent(content);
    layout->addWidget(render);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void TimersPanel::showOnScreenMenu(QWidget *anchor)
{
    QMenu menu(this);
    menu.addAction(tr("Вывести на экран"), this, [this]() { m_engine->goLive(m_selectedId); });
    if (current().hasStartStop())
        menu.addAction(tr("Вывести и запустить"), this, [this]() { m_engine->goLive(m_selectedId, true); });
    menu.addSeparator();
    QAction *raise = menu.addAction(tr("Показать окно проектора поверх окон"), this, [this]() {
        if (m_controller)
            m_controller->raiseDisplayWindow();
    });
    raise->setEnabled(m_controller && m_controller->isDisplayWindowVisible());
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
}

void TimersPanel::showCardMenu(const QString &id, const QPoint &pos)
{
    select(id);
    const int index = m_engine->indexOf(id);
    QMenu menu(this);
    menu.addAction(tr("Вывести на экран"), this, [this, id]() { m_engine->goLive(id); });
    if (m_engine->screen(id).hasStartStop())
        menu.addAction(tr("Вывести и запустить"), this, [this, id]() { m_engine->goLive(id, true); });
    menu.addSeparator();
    menu.addAction(tr("Дублировать"), this, [this, id]() { duplicateScreen(id); });
    QAction *left = menu.addAction(tr("Переместить влево"), this, [this, id, index]() { m_engine->moveScreen(id, index - 1); });
    left->setEnabled(index > 0);
    QAction *right = menu.addAction(tr("Переместить вправо"), this, [this, id, index]() { m_engine->moveScreen(id, index + 1); });
    right->setEnabled(index < m_engine->screens().size() - 1);
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Удалить"), this, [this, id]() { deleteScreen(id); });
    remove->setEnabled(m_engine->screens().size() > 1);
    menu.exec(pos);
}

void TimersPanel::chooseCustomBackground()
{
    const QString source = QFileDialog::getOpenFileName(this, tr("Выберите изображение для фона"), QString(),
                                                        tr("Изображения (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (source.isEmpty())
        return;
    // Copied into the app's data folder so the screen keeps working if the
    // original file is moved or deleted.
    const QString dest = Database::backgroundsDir() + QStringLiteral("/timer-%1.%2")
                                                          .arg(QUuid::createUuid().toString(QUuid::Id128),
                                                               QFileInfo(source).suffix().toLower());
    if (!QFile::copy(source, dest)) {
        QMessageBox::warning(this, tr("Фон"), tr("Не удалось скопировать изображение."));
        return;
    }
    modify([dest](TimerScreen &s) {
        s.customBackgroundPath = dest;
        s.background = TimerBackground::Custom;
    });
}

void TimersPanel::chooseWarnMinutes(bool yellow, QWidget *anchor)
{
    QMenu menu(this);
    for (int minutes : {1, 2, 3, 5, 10, 15, 30}) {
        menu.addAction(tr("за %1").arg(minutesLabel(minutes)), this, [this, yellow, minutes]() {
            modify([yellow, minutes](TimerScreen &s) {
                if (yellow)
                    s.warnYellowMinutes = minutes;
                else
                    s.warnRedMinutes = minutes;
                s.warnEnabled = true;
            });
        });
    }
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
}
