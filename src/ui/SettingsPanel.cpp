#include "SettingsPanel.h"
#include "IconProvider.h"
#include "Theme.h"
#include "ToggleSwitch.h"
#include "core/AppSettings.h"
#include "core/Database.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Hidden pages must not force the active page to the tallest page's height.
class SettingsPageStack : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint(); }
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint(); }
};

// A plain QLabel here kept rendering in Qt's own link-blue on some rows no
// matter what combination of stylesheet/palette/rich-text was used to try
// to override it (root cause not identified). Painting the text ourselves
// sidesteps the style/palette resolution entirely.
class PlainTextLabel : public QWidget {
public:
    explicit PlainTextLabel(const QString &text, int pixelSize, QColor color, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text), m_color(color)
    {
        m_font.setPixelSize(pixelSize);
        const QFontMetrics fm(m_font);
        setFixedSize(fm.horizontalAdvance(m_text) + 1, fm.height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setFont(m_font);
        painter.setPen(m_color);
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

private:
    QString m_text;
    QColor m_color;
    QFont m_font;
};

QPushButton *makeNavRow(const QString &iconName, const QString &label)
{
    auto *button = new QPushButton;
    button->setObjectName(QStringLiteral("SettingsNavRow"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(IconProvider::icon(iconName, QColor(Theme::TextDarkSecondary), 17));
    button->setIconSize(QSize(17, 17));
    button->setText(QStringLiteral("  %1").arg(label));
    return button;
}

QLabel *sectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: %1; padding-top: 6px;").arg(Theme::TextDarkPrimary));
    return label;
}

QLabel *hintLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;").arg(Theme::TextDarkSecondary));
    return label;
}

// design.pen settings row: label (and an optional grey description under
// it) on the left, the control vertically centred on the right.
QWidget *makeRow(const QString &label, QWidget *control, const QString &description = QString())
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(16);
    auto *textColumn = new QVBoxLayout;
    textColumn->setSpacing(3);
    textColumn->addWidget(new PlainTextLabel(label, 14, QColor(Theme::TextDarkPrimary)));
    if (!description.isEmpty()) {
        QLabel *desc = hintLabel(description);
        desc->setMaximumWidth(460);
        textColumn->addWidget(desc);
    }
    layout->addLayout(textColumn, 1);
    if (control)
        layout->addWidget(control, 0, Qt::AlignVCenter | Qt::AlignRight);
    return row;
}

QVBoxLayout *makeSubsection(QVBoxLayout *parent, const QString &title)
{
    parent->addWidget(sectionTitle(title));
    auto *rows = new QWidget;
    auto *rowsLayout = new QVBoxLayout(rows);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(8);
    parent->addWidget(rows);
    return rowsLayout;
}

QLabel *makeStaticValue(const QString &text = QString())
{
    // One line: a wrapping label gets squeezed to nothing next to the
    // row's label. Long values (paths) are capped; the tooltip has it all.
    auto *label = new QLabel(text);
    label->setMaximumWidth(460);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(Theme::TextDarkSecondary));
    return label;
}

QPushButton *makeActionButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setObjectName(QStringLiteral("OutlineButton"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(34);
    return button;
}

QWidget *makePage(const QString &title)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 8, 8);
    layout->setSpacing(10);
    auto *label = new QLabel(title);
    label->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    layout->addWidget(label);
    return page;
}

QVBoxLayout *pageLayout(QWidget *page)
{
    return qobject_cast<QVBoxLayout *>(page->layout());
}

QString shortPath(const QString &path)
{
    const QString home = QDir::homePath();
    QString shown = QDir::toNativeSeparators(path);
    if (path.startsWith(home))
        shown = QStringLiteral("~") + QDir::toNativeSeparators(path.mid(home.size()));
    return shown;
}

// design.pen "Hotkey Field": the key, and a × that clears it.
class KeyField : public QFrame {
public:
    KeyField()
    {
        setObjectName(QStringLiteral("KeyField"));
        setFixedSize(112, 34);
        setStyleSheet(QStringLiteral("QFrame#KeyField { background: #ffffff; border: 1px solid %1; border-radius: 8px; }").arg(Theme::BorderLight));
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 0, 4, 0);
        layout->setSpacing(2);
        m_edit = new QKeySequenceEdit;
        m_edit->setMaximumSequenceLength(1);
        m_edit->setClearButtonEnabled(false);
        m_edit->setStyleSheet(QStringLiteral("QLineEdit { border: none; background: transparent; font-size: 13px; font-weight: 600; color: %1; }")
                                  .arg(Theme::TextDarkPrimary));
        m_edit->setToolTip(QObject::tr("Нажмите поле и затем нужную клавишу"));
        layout->addWidget(m_edit, 1);
        auto *clear = new QPushButton;
        clear->setFlat(true);
        clear->setCursor(Qt::PointingHandCursor);
        clear->setFixedSize(20, 20);
        clear->setIcon(IconProvider::icon(QStringLiteral("x"), QColor(Theme::TextDarkSecondary), 13));
        clear->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; }"));
        clear->setToolTip(QObject::tr("Убрать клавишу"));
        layout->addWidget(clear);
        QObject::connect(m_edit, &QKeySequenceEdit::editingFinished, this, [this]() {
            if (onChanged)
                onChanged(m_edit->keySequence().toString(QKeySequence::PortableText));
        });
        QObject::connect(clear, &QPushButton::clicked, this, [this]() {
            m_edit->clear();
            if (onChanged)
                onChanged(QString());
        });
    }

    void setKey(const QString &portable)
    {
        const QSignalBlocker blocker(m_edit);
        m_edit->setKeySequence(QKeySequence::fromString(portable, QKeySequence::PortableText));
    }

    std::function<void(const QString &)> onChanged;

private:
    QKeySequenceEdit *m_edit = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------

SettingsPanel::SettingsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SettingsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
}

void SettingsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(32, 32, 32, 24);
    root->setSpacing(18);

    auto *title = new QLabel(tr("Настройки"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    root->addWidget(title);
    auto *subtitle = new QLabel(tr("Настройте Sermon под нужды вашей церкви"));
    subtitle->setStyleSheet(QStringLiteral("font-size: 13.5px; color: %1;").arg(Theme::TextDarkSecondary));
    root->addWidget(subtitle);

    auto *bodyRow = new QHBoxLayout;
    bodyRow->setSpacing(16);
    root->addLayout(bodyRow, 1);

    // ---- Nav column ----
    auto *navColumn = new QWidget;
    navColumn->setMinimumWidth(180);
    navColumn->setMaximumWidth(230);
    auto *navLayout = new QVBoxLayout(navColumn);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(2);

    const QList<QPair<QString, QString>> entries = {
        {QStringLiteral("settings"), tr("Общие")},
        {QStringLiteral("monitor"), tr("Показ")},
        {QStringLiteral("book-open"), tr("Библия")},
        {QStringLiteral("music"), tr("Песни и сборники")},
        {QStringLiteral("database"), tr("База данных")},
        {QStringLiteral("keyboard"), tr("Горячие клавиши")},
        {QStringLiteral("wifi"), tr("OBS и сеть")},
        {QStringLiteral("cloud"), tr("Резервная копия")},
        {QStringLiteral("palette"), tr("Внешний вид")},
    };

    m_stack = new SettingsPageStack;
    connect(m_stack, &QStackedWidget::currentChanged, this, [this] { m_stack->updateGeometry(); });
    const std::function<QWidget *()> builders[] = {
        [this] { return buildGeneralPage(); },  [this] { return buildShowPage(); },   [this] { return buildBiblePage(); },
        [this] { return buildSongsPage(); },    [this] { return buildDatabasePage(); }, [this] { return buildHotkeysPage(); },
        [this] { return buildObsPage(); },      [this] { return buildBackupPage(); }, [this] { return buildAppearancePage(); },
    };
    for (int i = 0; i < 9; ++i) {
        m_buildingPage = i;
        m_stack->addWidget(builders[i]());
    }

    auto *navGroup = new QButtonGroup(this);
    m_navGroup = navGroup;
    for (int i = 0; i < entries.size(); ++i) {
        auto *row = makeNavRow(entries.at(i).first, entries.at(i).second);
        navGroup->addButton(row, i);
        navLayout->addWidget(row);
    }
    navGroup->button(0)->setChecked(true);
    connect(navGroup, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);
    navLayout->addStretch();
    bodyRow->addWidget(navColumn);

    // ---- Card + actions under it ----
    auto *cardColumn = new QVBoxLayout;
    cardColumn->setSpacing(12);
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("SettingsCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 18, 12, 18);
    // Pages can be taller than the window: scroll instead of squeezing rows.
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }") + Theme::scrollBarCss());
    scrollArea->viewport()->setStyleSheet(QStringLiteral("background: #ffffff;"));
    scrollArea->setWidget(m_stack);
    cardLayout->addWidget(scrollArea, 1);
    connect(m_stack, &QStackedWidget::currentChanged, this, [card, cardLayout, scrollArea, navColumn](int page) {
        const bool displayPage = page == 2 || page == 3;
        card->setObjectName(displayPage ? QStringLiteral("TextSettingsContainer") : QStringLiteral("SettingsCard"));
        card->setStyleSheet(displayPage ? QStringLiteral("QFrame#TextSettingsContainer { border: none; }") : QString());
        cardLayout->setContentsMargins(displayPage ? 0 : 20, displayPage ? 0 : 18, displayPage ? 0 : 12, displayPage ? 0 : 18);
        navColumn->setFixedWidth(displayPage ? 184 : 210);
        scrollArea->verticalScrollBar()->setValue(0);
    });
    cardColumn->addWidget(card, 1);

    auto *actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(12);
    auto *resetButton = new QPushButton(tr("  Сбросить"));
    resetButton->setObjectName(QStringLiteral("OutlineButton"));
    resetButton->setIcon(IconProvider::icon(QStringLiteral("rotate-ccw"), QColor(Theme::TextDarkPrimary), 15));
    resetButton->setCursor(Qt::PointingHandCursor);
    resetButton->setToolTip(tr("Вернуть значения по умолчанию на этой странице"));
    connect(resetButton, &QPushButton::clicked, this, &SettingsPanel::resetPage);
    actionsRow->addWidget(resetButton);
    actionsRow->addStretch();
    m_cancelButton = new QPushButton(tr("Отмена"));
    m_cancelButton->setObjectName(QStringLiteral("OutlineButton"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsPanel::cancel);
    actionsRow->addWidget(m_cancelButton);
    m_saveButton = new QPushButton(tr("  Сохранить"));
    m_saveButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_saveButton->setIcon(IconProvider::icon(QStringLiteral("save"), QColor(Theme::TextLightPrimary), 15));
    m_saveButton->setCursor(Qt::PointingHandCursor);
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsPanel::save);
    actionsRow->addWidget(m_saveButton);
    cardColumn->addLayout(actionsRow);
    bodyRow->addLayout(cardColumn, 1);

    setStyleSheet(QStringLiteral(R"(
        QWidget#SettingsPanel { background: #ffffff; }
        QPushButton#SettingsNavRow {
            text-align: left; border: none; background: transparent; border-radius: 9px;
            padding: 9px 10px; font-size: 13.5px; color: %1;
        }
        QPushButton#SettingsNavRow:hover { background: #f3f4f6; }
        QPushButton#SettingsNavRow:checked { background: %5; color: %3; font-weight: 600; }
        QFrame#SettingsCard { background: #ffffff; border: 1px solid %2; border-radius: 14px; }
        QComboBox#FieldBox {
            border: 1px solid %2; border-radius: 9px; padding: 7px 12px;
            font-size: 13px; font-weight: 500; color: %1; background: #ffffff;
        }
        QComboBox#FieldBox:disabled { color: %4; background: #f7f8fa; }
        QComboBox#FieldBox::drop-down {
            subcontrol-origin: padding; subcontrol-position: top right;
            width: 26px; border: none; background: transparent;
        }
        QComboBox#FieldBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
        QLineEdit#SettingsLine, QSpinBox#SettingsSpin {
            border: 1px solid %2; border-radius: 9px; padding: 0 12px; min-height: 36px;
            font-size: 13px; color: %1; background: #ffffff;
        }
        QLineEdit#SettingsLine:focus, QSpinBox#SettingsSpin:focus { border: 1px solid %3; }
        QPushButton#OutlineButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 7px 16px; font-weight: 600; font-size: 13px; color: %1;
        }
        QPushButton#OutlineButton:hover { background: #f3f4f6; }
        QPushButton#OutlineButton:disabled { color: %4; }
        QPushButton#PrimaryButton {
            background: %3; border: none; border-radius: 9px; padding: 9px 18px;
            font-weight: 600; font-size: 13.5px; color: #ffffff;
        }
        QPushButton#PrimaryButton:hover { background: #255ed1; }
        QPushButton#PrimaryButton:disabled { background: #9db8f2; }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue, Theme::TextDarkSecondary, Theme::AccentBlueBg));

    updateActions();
    for (const auto &refresh : std::as_const(m_previewRefreshers)) refresh();
}

void SettingsPanel::showPage(int index)
{
    if (index < 0 || index >= m_stack->count())
        return;
    m_navGroup->button(index)->setChecked(true);
    m_stack->setCurrentIndex(index);
}

// ---- Pending-value plumbing ---------------------------------------------------

QVariant SettingsPanel::current(const QString &key) const
{
    return m_pending.contains(key) ? m_pending.value(key) : AppSettings::value(key);
}

int SettingsPanel::bind(const QString &key, const std::function<void(const QVariant &)> &show)
{
    m_bindings.append({key, m_buildingPage, show});
    show(current(key));
    return int(m_bindings.size()) - 1;
}

void SettingsPanel::setPending(const QString &key, const QVariant &value, int sourceBinding)
{
    // Stored values come back from QSettings as strings: compare as text.
    if (AppSettings::value(key).toString() == value.toString())
        m_pending.remove(key);
    else
        m_pending.insert(key, value);
    for (int i = 0; i < m_bindings.size(); ++i) {
        if (i != sourceBinding && m_bindings.at(i).key == key)
            m_bindings.at(i).show(value);
    }
    updateActions();
    for (const auto &refresh : std::as_const(m_previewRefreshers)) refresh();
}

void SettingsPanel::refreshBindings()
{
    for (const Binding &binding : std::as_const(m_bindings))
        binding.show(current(binding.key));
    for (const auto &refresh : std::as_const(m_previewRefreshers)) refresh();
}

void SettingsPanel::updateActions()
{
    if (!m_saveButton)
        return;
    const bool dirty = !m_pending.isEmpty();
    m_saveButton->setEnabled(dirty);
    m_cancelButton->setEnabled(dirty);
}

void SettingsPanel::save()
{
    // One key for two actions would make neither of them predictable.
    const QList<QPair<QString, QString>> hotkeys{
        {AppSettings::KeyPause, tr("Пауза показа")}, {AppSettings::KeyNext, tr("Следующий слайд")},
        {AppSettings::KeyPrev, tr("Предыдущий слайд")}, {AppSettings::KeyGoLive, tr("Начать показ")},
        {AppSettings::KeyEndShow, tr("Завершить показ")}, {AppSettings::KeySearch, tr("Поиск")},
        {AppSettings::KeyAdd, tr("Добавить")}};
    QHash<QKeySequence, QString> taken;
    for (const auto &[key, title] : hotkeys) {
        const QKeySequence sequence = QKeySequence::fromString(current(key).toString(), QKeySequence::PortableText);
        if (sequence.isEmpty())
            continue;
        if (taken.contains(sequence)) {
            QMessageBox::warning(this, tr("Горячие клавиши"),
                                 tr("Клавиша «%1» назначена и на «%2», и на «%3». Выберите для одного из действий другую клавишу.")
                                     .arg(sequence.toString(QKeySequence::NativeText), taken.value(sequence), title));
            return;
        }
        taken.insert(sequence, title);
    }

    QStringList changed;
    bool restart = false;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        AppSettings::setValue(it.key(), it.value());
        changed << it.key();
        restart = restart || AppSettings::needsRestart(it.key());
    }
    m_pending.clear();
    updateActions();
    if (changed.isEmpty())
        return;
    emit settingsSaved(changed);
    if (restart) {
        const auto answer = QMessageBox::question(this, tr("Настройки сохранены"),
            tr("Тема, язык, масштаб и шрифт интерфейса применяются после перезапуска Sermon.\nПерезапустить сейчас?"));
        if (answer == QMessageBox::Yes)
            emit settingsSaved({QStringLiteral("__restart__")});
    }
}

void SettingsPanel::cancel()
{
    m_pending.clear();
    refreshBindings();
    updateActions();
}

void SettingsPanel::resetPage()
{
    const int page = m_stack->currentIndex();
    for (int i = 0; i < m_bindings.size(); ++i) {
        const Binding &binding = m_bindings.at(i);
        if (binding.page == page)
            setPending(binding.key, AppSettings::defaultValue(binding.key));
    }
}

// ---- Bound controls ---------------------------------------------------------

QComboBox *SettingsPanel::combo(const QString &key, const QList<QPair<QString, QVariant>> &options, int minWidth)
{
    auto *box = new QComboBox;
    box->setObjectName(QStringLiteral("FieldBox"));
    box->setFixedHeight(38);
    box->setMinimumWidth(minWidth);
    box->setCursor(Qt::PointingHandCursor);
    // Data as text: that's how QSettings hands values back after a restart.
    for (const auto &option : options)
        box->addItem(option.first, option.second.toString());
    const int index = bind(key, [box](const QVariant &value) {
        const QSignalBlocker blocker(box);
        int found = box->findData(value.toString());
        if (found < 0) {
            // A value not in the list (e.g. an older custom one): show it as is.
            box->addItem(value.toString(), value.toString());
            found = box->count() - 1;
        }
        box->setCurrentIndex(found);
    });
    connect(box, &QComboBox::currentIndexChanged, this, [this, box, key, index]() {
        if (box->currentData().toString() == QLatin1String("__choose__"))
            return; // handled by the owner (opens a file dialog)
        setPending(key, box->currentData(), index);
    });
    return box;
}

QWidget *SettingsPanel::toggle(const QString &key)
{
    return toggle(key, [](const QVariant &value) { return value.toBool(); }, [](bool on) { return QVariant(on); });
}

// design.pen "Toggle" + its "Включено"/"Выключено" caption.
QWidget *SettingsPanel::toggle(const QString &key, const std::function<bool(const QVariant &)> &read,
                               const std::function<QVariant(bool)> &write)
{
    auto *wrap = new QWidget;
    auto *layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *switchButton = new ToggleSwitch;
    auto *caption = new QLabel;
    caption->setFixedWidth(78);
    layout->addWidget(switchButton);
    layout->addWidget(caption);
    const auto paintCaption = [caption](bool on) {
        caption->setText(on ? tr("Включено") : tr("Выключено"));
        caption->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(on ? Theme::AccentBlue : Theme::TextDarkSecondary));
    };
    const int index = bind(key, [switchButton, paintCaption, read](const QVariant &value) {
        const QSignalBlocker blocker(switchButton);
        switchButton->setChecked(read(value));
        paintCaption(read(value));
    });
    connect(switchButton, &ToggleSwitch::toggled, this, [this, key, index, write, paintCaption](bool on) {
        paintCaption(on);
        setPending(key, write(on), index);
    });
    return wrap;
}

QWidget *SettingsPanel::keyField(const QString &key)
{
    auto *field = new KeyField;
    const int index = bind(key, [field](const QVariant &value) { field->setKey(value.toString()); });
    field->onChanged = [this, key, index](const QString &value) { setPending(key, value, index); };
    return field;
}

// ---- Pages --------------------------------------------------------------------

QWidget *SettingsPanel::buildGeneralPage()
{
    auto *page = makePage(tr("Общие"));
    auto *layout = pageLayout(page);

    // "Интерфейс": theme and language side by side, captions above.
    layout->addWidget(sectionTitle(tr("Интерфейс")));
    auto *interfaceRow = new QHBoxLayout;
    interfaceRow->setSpacing(20);
    const auto column = [&](const QString &caption, QComboBox *box) {
        auto *col = new QVBoxLayout;
        col->setSpacing(6);
        col->addWidget(hintLabel(caption));
        col->addWidget(box);
        interfaceRow->addLayout(col, 1);
    };
    column(tr("Тема приложения"), combo(AppSettings::Theme, {{tr("Светлая (по умолчанию)"), QStringLiteral("light")},
                                                              {tr("Тёмная"), QStringLiteral("dark")}}));
    column(tr("Язык интерфейса"), combo(AppSettings::Language, {{QStringLiteral("Русский"), QStringLiteral("ru")},
                                                                 {QStringLiteral("Українська"), QStringLiteral("uk")},
                                                                 {QStringLiteral("English"), QStringLiteral("en")}}));
    layout->addLayout(interfaceRow);

    auto *startup = makeSubsection(layout, tr("Запуск и поведение"));
    startup->addWidget(makeRow(tr("При запуске открывать"),
        combo(AppSettings::StartupSection, {{tr("Последний использованный раздел"), QStringLiteral("last")},
                                            {tr("Песни"), QStringLiteral("songs")},
                                            {tr("Библия"), QStringLiteral("bible")},
                                            {tr("Объявления"), QStringLiteral("announcements")},
                                            {tr("Фото"), QStringLiteral("photos")},
                                            {tr("Видео"), QStringLiteral("videos")},
                                            {tr("Плейлисты"), QStringLiteral("playlists")},
                                            {tr("Таймеры"), QStringLiteral("timers")}}, 250)));
    startup->addWidget(makeRow(tr("Запоминать последний открытый элемент"), toggle(AppSettings::RememberItem),
                               tr("Песня, объявление или плейлист, выбранные перед закрытием, откроются снова")));
    startup->addWidget(makeRow(tr("Автосохранение изменений"), toggle(AppSettings::AutosaveEnabled),
                               tr("Несохранённые правки объявлений сохраняются сами через заданный интервал")));
    startup->addWidget(makeRow(tr("Интервал автосохранения"),
        combo(AppSettings::AutosaveInterval, {{tr("30 секунд"), 30}, {tr("1 минута"), 60}, {tr("2 минуты"), 120}, {tr("5 минут"), 300}})));

    auto *text = makeSubsection(layout, tr("Текст и отображение"));
    text->addWidget(makeRow(tr("Масштаб шрифта интерфейса"),
        combo(AppSettings::UiScale, {{tr("100% (по умолчанию)"), 100}, {QStringLiteral("110%"), 110}, {QStringLiteral("125%"), 125},
                                     {QStringLiteral("150%"), 150}})));
    text->addWidget(makeRow(tr("Показывать \"***\" в конце песни"), toggle(AppSettings::LineStars),
                            tr("Добавлять три звёздочки в конце последнего слайда песни для удобства вокалистов.")));
    text->addWidget(makeRow(tr("Отступ перед \"***\""),
        combo(AppSettings::LineStarsGap, {{tr("Без отступа"), 0}, {tr("25% строки"), 25}, {tr("50% строки"), 50},
                                          {tr("75% строки"), 75}, {tr("Пустая строка"), 100}, {tr("Полторы строки"), 150}}),
        tr("Расстояние между последней строкой песни и звёздочками.")));

    auto *show = makeSubsection(layout, tr("Проектор и показ"));
    show->addWidget(makeRow(tr("Автоматически включать полный экран на проекторе"), toggle(AppSettings::AutoFullscreen)));
    show->addWidget(makeRow(tr("Включить плавное появление слайдов (fade)"),
        toggle(AppSettings::Transition, [](const QVariant &v) { return v.toString() != QLatin1String("none"); },
               [](bool on) { return QVariant(on ? QStringLiteral("fade") : QStringLiteral("none")); })));

    // Default background: built-ins, or a file of one's own.
    QComboBox *background = combo(AppSettings::DefaultBackground,
        {{tr("Без фона (чёрный)"), QString()},
         {tr("Тёмный фон (горы)"), QStringLiteral("builtin:mountains")},
         {tr("Зал церкви"), QStringLiteral("builtin:church_hall")},
         {tr("Синие волны (видео)"), QStringLiteral("builtin:blue_waves")},
         {tr("Тёплый свет (видео)"), QStringLiteral("builtin:warm_glow")}}, 220);
    background->addItem(tr("Свой файл…"), QStringLiteral("__choose__"));
    // Custom paths show by file name.
    const auto relabel = [background]() {
        for (int i = 0; i < background->count(); ++i) {
            const QString data = background->itemData(i).toString();
            if (!data.isEmpty() && !data.startsWith(QLatin1String("builtin:")) && data != QLatin1String("__choose__"))
                background->setItemText(i, QFileInfo(data).fileName());
        }
    };
    relabel();
    connect(background, &QComboBox::activated, this, [this, background, relabel](int index) {
        if (background->itemData(index).toString() != QLatin1String("__choose__"))
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Фоновое изображение по умолчанию"), QString(),
            tr("Изображения и видео (*.jpg *.jpeg *.png *.bmp *.webp *.mp4 *.mov *.mkv *.webm)"));
        if (path.isEmpty()) {
            refreshBindings();
            return;
        }
        setPending(AppSettings::DefaultBackground, path);
        relabel();
    });
    show->addWidget(makeRow(tr("Фоновое изображение по умолчанию"), background,
                            tr("Для песен и стихов без собственного фона")));
    show->addWidget(makeRow(tr("Выравнивание текста на слайдах"),
        combo(AppSettings::Alignment, {{tr("По центру"), QStringLiteral("center")},
                                       {tr("По левому краю"), QStringLiteral("left")},
                                       {tr("По правому краю"), QStringLiteral("right")}})));
    show->addWidget(makeRow(tr("Сохранять состояние предпросмотра"), toggle(AppSettings::RememberDisplayWindow),
                            tr("Запоминать, было ли открыто окно показа при закрытии приложения, и открывать его снова.")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildShowPage()
{
    auto *page = makePage(tr("Показ"));
    auto *layout = pageLayout(page);

    auto *screen = makeSubsection(layout, tr("Экран показа"));
    m_screenBox = combo(AppSettings::DisplayScreen, {{tr("Автоматически (второй монитор)"), QString()}}, 240);
    const auto fillScreens = [this]() {
        const QSignalBlocker blocker(m_screenBox);
        while (m_screenBox->count() > 1)
            m_screenBox->removeItem(1);
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i) {
            const QSize size = screens.at(i)->size() * screens.at(i)->devicePixelRatio();
            m_screenBox->addItem(tr("Монитор %1 (%2×%3)%4").arg(i + 1).arg(size.width()).arg(size.height())
                                     .arg(screens.at(i) == QGuiApplication::primaryScreen() ? tr(", основной") : QString()),
                                 screens.at(i)->name());
        }
        m_screenBox->setCurrentIndex(qMax(0, m_screenBox->findData(current(AppSettings::DisplayScreen).toString())));
    };
    fillScreens();
    m_contextRefreshers << fillScreens;
    screen->addWidget(makeRow(tr("Монитор для показа"), m_screenBox));
    screen->addWidget(makeRow(tr("Разрешение"),
        combo(AppSettings::DisplayResolution, {{tr("Автоматически"), QStringLiteral("auto")},
                                               {QStringLiteral("1920×1080"), QStringLiteral("1920x1080")},
                                               {QStringLiteral("1280×720"), QStringLiteral("1280x720")}}),
        tr("Размер кадра для окна показа и вывода в OBS; «Автоматически» — во весь экран")));

    auto *slides = makeSubsection(layout, tr("Внешний вид слайдов"));
    QList<QPair<QString, QVariant>> fonts;
    for (const QString &family : {QStringLiteral("Montserrat"), QStringLiteral("Inter"), QStringLiteral("Segoe UI"), QStringLiteral("Arial"),
                                  QStringLiteral("Georgia"), QStringLiteral("Times New Roman")}) {
        if (QFontDatabase::families().contains(family) || family == QLatin1String("Montserrat"))
            fonts.append({family, family});
    }
    slides->addWidget(makeRow(tr("Шрифт слайдов"), combo(AppSettings::SongFont, fonts)));
    const QList<QPair<QString, QVariant>> sizes = {{tr("Мелкий"), 80}, {tr("Средний"), 100}, {tr("Крупный"), 120}, {tr("Очень крупный"), 140}};
    slides->addWidget(makeRow(tr("Размер текста"), combo(AppSettings::SongScale, sizes)));
    slides->addWidget(makeRow(tr("Тень текста для читаемости"), toggle(AppSettings::TextShadow)));

    auto *transitions = makeSubsection(layout, tr("Переходы между слайдами"));
    transitions->addWidget(makeRow(tr("Анимация перехода"),
        combo(AppSettings::Transition, {{tr("Плавное затухание"), QStringLiteral("fade")},
                                        {tr("Без анимации"), QStringLiteral("none")},
                                        {tr("Сдвиг"), QStringLiteral("slide")}})));
    transitions->addWidget(makeRow(tr("Длительность перехода"),
        combo(AppSettings::TransitionMs, {{tr("0.15 сек"), 150}, {tr("0.3 сек"), 300}, {tr("0.6 сек"), 600}})));

    auto *logo = makeSubsection(layout, tr("Логотип и заставка"));
    logo->addWidget(makeRow(tr("Показывать логотип церкви"), toggle(AppSettings::ShowLogo)));
    auto *logoButton = makeActionButton(QString());
    const int logoIndex = bind(AppSettings::LogoPath, [logoButton](const QVariant &value) {
        logoButton->setText(value.toString().isEmpty() ? tr("Логотип Sermon — выбрать…") : QFileInfo(value.toString()).fileName());
    });
    connect(logoButton, &QPushButton::clicked, this, [this, logoIndex]() {
        QMessageBox box(this);
        box.setWindowTitle(tr("Логотип церкви"));
        box.setText(tr("Выберите файл логотипа (лучше PNG с прозрачным фоном)."));
        QPushButton *choose = box.addButton(tr("Выбрать файл…"), QMessageBox::AcceptRole);
        QPushButton *reset = box.addButton(tr("Логотип Sermon"), QMessageBox::ResetRole);
        box.addButton(tr("Отмена"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == reset) {
            setPending(AppSettings::LogoPath, QString());
        } else if (box.clickedButton() == choose) {
            const QString path = QFileDialog::getOpenFileName(this, tr("Логотип церкви"), QString(), tr("Изображения (*.png *.jpg *.jpeg *.svg *.webp)"));
            if (!path.isEmpty())
                setPending(AppSettings::LogoPath, path);
        }
        Q_UNUSED(logoIndex);
    });
    logo->addWidget(makeRow(tr("Файл логотипа"), logoButton));
    logo->addWidget(makeRow(tr("Экран между службами"),
        combo(AppSettings::IdleScreen, {{tr("Логотип с фоном"), QStringLiteral("logo")},
                                        {tr("Чёрный экран"), QStringLiteral("black")},
                                        {tr("Ничего"), QStringLiteral("none")}}),
        tr("Что видно на проекторе и в OBS, когда ничего не показывается. «Ничего» — прозрачный фон в OBS.")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::loadedList(bool bible)
{
    auto *list = new QFrame;
    list->setObjectName(QStringLiteral("LoadedList"));
    list->setStyleSheet(QStringLiteral("QFrame#LoadedList { background: #ffffff; border: 1px solid %1; border-radius: 10px; }"
                                       "QFrame#LoadedRow { background: transparent; border: none; }"
                                       "QFrame#LoadedRowAlt { background: %2; border: none; }"
                                       "QLabel { background: transparent; border: none; }")
                            .arg(Theme::BorderLight, Theme::BgPanel));
    auto *rows = new QVBoxLayout(list);
    rows->setContentsMargins(1, 1, 1, 1);
    rows->setSpacing(0);

    const auto rebuild = [this, list, rows, bible]() {
        while (QLayoutItem *item = rows->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        struct Entry { QString key; QString name; QString meta; QString badge; };
        QList<Entry> entries;
        if (bible) {
            // "Первый доступный" (empty setting) means the first one listed.
            QString main = current(AppSettings::BibleTranslation).toString();
            if (main.isEmpty() && !m_context.translationInfos.isEmpty())
                main = m_context.translationInfos.first().name;
            for (const auto &t : std::as_const(m_context.translationInfos))
                entries.append({t.name, t.name, tr("%1 книг · %2 стихов").arg(t.books).arg(QLocale().toString(t.verses)),
                                t.name == main ? tr("Основной") : QString()});
        } else {
            const QString active = current(AppSettings::ActiveCollection).toString();
            for (const auto &[name, count] : std::as_const(m_context.collectionCounts))
                entries.append({name, name.isEmpty() ? tr("Без сборника") : name, tr("Песен: %1").arg(count),
                                !name.isEmpty() && name == active ? tr("Активный") : QString()});
        }
        list->setVisible(!entries.isEmpty());
        for (int i = 0; i < entries.size(); ++i) {
            const Entry &entry = entries.at(i);
            auto *row = new QFrame;
            row->setObjectName(i % 2 ? QStringLiteral("LoadedRowAlt") : QStringLiteral("LoadedRow"));
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(14, 12, 14, 12);
            layout->setSpacing(12);
            auto *icon = new QLabel;
            icon->setPixmap(IconProvider::pixmap(bible ? QStringLiteral("book-open") : QStringLiteral("music"), QColor(Theme::AccentBlue), 16));
            layout->addWidget(icon);
            auto *text = new QVBoxLayout;
            text->setSpacing(2);
            auto *name = new QLabel(entry.name);
            name->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::TextDarkPrimary));
            auto *meta = new QLabel(entry.meta);
            meta->setStyleSheet(QStringLiteral("font-size: 12.5px; color: %1;").arg(Theme::TextDarkSecondary));
            text->addWidget(name);
            text->addWidget(meta);
            layout->addLayout(text, 1);
            if (!entry.badge.isEmpty()) {
                auto *badge = new QLabel(entry.badge);
                badge->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: 6px; padding: 3px 8px; "
                                                    "font-size: 11.5px; font-weight: 600; }").arg(Theme::AccentBlueBg, Theme::AccentBlue));
                layout->addWidget(badge);
            }
            if (!bible) {
                auto *save = new QPushButton;
                save->setFixedSize(34, 34);
                save->setCursor(Qt::PointingHandCursor);
                save->setIcon(IconProvider::icon(QStringLiteral("download"), QColor(Theme::TextDarkPrimary), 15));
                save->setToolTip(tr("Сохранить сборник в файл .sps"));
                save->setStyleSheet(QStringLiteral("QPushButton { background: #ffffff; border: 1px solid %1; border-radius: 8px; }"
                                                   "QPushButton:hover { background: %2; }").arg(Theme::BorderLight, Theme::BgPanel));
                const QString key = entry.key;
                connect(save, &QPushButton::clicked, this, [this, key]() { emit exportCollectionRequested(key); });
                layout->addWidget(save);
            }
            auto *remove = new QPushButton;
            remove->setFixedSize(34, 34);
            remove->setCursor(Qt::PointingHandCursor);
            remove->setIcon(IconProvider::icon(QStringLiteral("trash-2"), QColor(0xef, 0x44, 0x44), 15));
            remove->setToolTip(bible ? tr("Удалить перевод") : tr("Удалить сборник и его песни"));
            remove->setStyleSheet(QStringLiteral("QPushButton { background: #ffffff; border: 1px solid %1; border-radius: 8px; }"
                                                 "QPushButton:hover { background: #fef2f2; }").arg(Theme::BorderLight));
            const QString key = entry.key;
            connect(remove, &QPushButton::clicked, this, [this, bible, key]() {
                if (bible)
                    emit removeTranslationRequested(key);
                else
                    emit removeCollectionRequested(key);
            });
            layout->addWidget(remove);
            rows->addWidget(row);
        }
    };
    m_contextRefreshers << rebuild;
    rebuild();
    return list;
}

QWidget *SettingsPanel::buildBibleLibraryPage()
{
    auto *page = makePage(tr("Библия"));
    auto *layout = pageLayout(page);

    auto *loaded = makeSubsection(layout, tr("Загруженные переводы Библии"));
    loaded->addWidget(hintLabel(tr("Переводы, доступные в разделе «Библия». Удалённый перевод можно загрузить снова из файла .spb.")));
    loaded->addWidget(loadedList(true));

    auto *translations = makeSubsection(layout, tr("Перевод по умолчанию"));
    m_translationBox = combo(AppSettings::BibleTranslation, {{tr("Первый доступный"), QString()}}, 220);
    translations->addWidget(makeRow(tr("Основной перевод"), m_translationBox));
    translations->addWidget(makeRow(tr("Показывать альтернативный перевод"), toggle(AppSettings::BibleAltEnabled),
                                    tr("Под основным текстом стиха на экране — тот же стих в другом переводе")));
    m_altTranslationBox = combo(AppSettings::BibleAltTranslation, {{tr("Не выбран"), QString()}}, 220);
    translations->addWidget(makeRow(tr("Альтернативный перевод"), m_altTranslationBox));
    const auto fillTranslations = [this]() {
        for (QComboBox *box : {m_translationBox, m_altTranslationBox}) {
            const QSignalBlocker blocker(box);
            while (box->count() > 1)
                box->removeItem(1);
            for (const QString &name : std::as_const(m_context.translations))
                box->addItem(name, name);
        }
        m_translationBox->setCurrentIndex(qMax(0, m_translationBox->findData(current(AppSettings::BibleTranslation).toString())));
        m_altTranslationBox->setCurrentIndex(qMax(0, m_altTranslationBox->findData(current(AppSettings::BibleAltTranslation).toString())));
    };
    m_contextRefreshers << fillTranslations;

    auto *format = makeSubsection(layout, tr("Формат отображения"));
    format->addWidget(makeRow(tr("Формат ссылки"),
        combo(AppSettings::BibleRefFormat, {{tr("Полное название книги"), QStringLiteral("full")},
                                            {tr("Сокращение"), QStringLiteral("short")}}, 220)));
    format->addWidget(makeRow(tr("Показывать номера стихов"), toggle(AppSettings::BibleVerseNumbers),
                              tr("Номер стиха (¹⁶) перед его текстом на слайде")));
    format->addWidget(makeRow(tr("Разделять по стихам по умолчанию"), toggle(AppSettings::BibleSplitVerses),
                              tr("Каждый стих — отдельный слайд; иначе весь отрывок на одном слайде")));
    format->addWidget(makeRow(tr("Показывать ссылку (книга, глава:стих) на экране"), toggle(AppSettings::BibleShowReference)));

    auto *history = makeSubsection(layout, tr("История и избранное"));
    history->addWidget(makeRow(tr("Хранить историю поиска"), toggle(AppSettings::BibleKeepHistory)));
    history->addWidget(makeRow(tr("Количество недавних мест"),
        combo(AppSettings::BibleRecentCount, {{QStringLiteral("5"), 5}, {QStringLiteral("10"), 10}, {QStringLiteral("15"), 15}, {QStringLiteral("20"), 20}}, 100)));

    auto *importBible = makeSubsection(layout, tr("Импорт Библии"));
    auto *importBibleBtn = makeActionButton(tr("Загрузить Библию…"));
    connect(importBibleBtn, &QPushButton::clicked, this, &SettingsPanel::bibleImportRequested);
    importBible->addWidget(makeRow(tr("Загрузить файл Библии"), importBibleBtn, tr("Формат .spb")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildSongsLibraryPage()
{
    auto *page = makePage(tr("Песни и сборники"));
    auto *layout = pageLayout(page);

    auto *loaded = makeSubsection(layout, tr("Загруженные сборники"));
    loaded->addWidget(hintLabel(tr("Песни в библиотеке по сборникам. ⤓ — сохранить сборник в файл .sps (резервная копия "
                                   "или перенос на другой компьютер). Удаление сборника убирает все его песни "
                                   "(перед этим — подтверждение и резервная копия базы).")));
    loaded->addWidget(loadedList(false));

    auto *collections = makeSubsection(layout, tr("Сборники"));
    m_collectionBox = combo(AppSettings::ActiveCollection, {{tr("Все сборники"), QString()}}, 220);
    collections->addWidget(makeRow(tr("Активный сборник"), m_collectionBox,
                                   tr("Список «Песни» показывает только песни этого сборника")));
    m_contextRefreshers << [this]() {
        const QSignalBlocker blocker(m_collectionBox);
        while (m_collectionBox->count() > 1)
            m_collectionBox->removeItem(1);
        for (const QString &name : std::as_const(m_context.collections))
            m_collectionBox->addItem(name, name);
        m_collectionBox->setCurrentIndex(qMax(0, m_collectionBox->findData(current(AppSettings::ActiveCollection).toString())));
    };

    // "Сборник для новых песен": an existing songbook or a new name typed in.
    auto *defaultBox = new QComboBox;
    defaultBox->setObjectName(QStringLiteral("FieldBox"));
    defaultBox->setFixedHeight(38);
    defaultBox->setMinimumWidth(220);
    defaultBox->setEditable(true);
    defaultBox->setInsertPolicy(QComboBox::NoInsert);
    const auto showDefault = [this, defaultBox]() {
        const QSignalBlocker blocker(defaultBox);
        defaultBox->clear();
        const QString mine = tr("Мои песни");
        QStringList names{mine};
        for (const QString &name : std::as_const(m_context.collections))
            if (!names.contains(name))
                names << name;
        defaultBox->addItems(names);
        const QString value = current(AppSettings::DefaultCollection).toString();
        defaultBox->setEditText(value.isEmpty() ? mine : value);
    };
    const int defaultIndex = bind(AppSettings::DefaultCollection, [showDefault](const QVariant &) { showDefault(); });
    m_contextRefreshers << showDefault;
    connect(defaultBox, &QComboBox::currentTextChanged, this, [this, defaultIndex](const QString &text) {
        const QString name = text.trimmed();
        setPending(AppSettings::DefaultCollection, name == tr("Мои песни") ? QString() : name, defaultIndex);
    });
    collections->addWidget(makeRow(tr("Сборник для новых песен"), defaultBox,
                                   tr("Подставляется в окне «Добавить песню»; можно поменять при добавлении")));

    auto *format = makeSubsection(layout, tr("Формат текста"));
    format->addWidget(makeRow(tr("Показывать аккорды"), toggle(AppSettings::ShowChords),
                              tr("Аккорды из песен ChordPro ([C], [Am] …) — над словами на экране показа")));
    format->addWidget(makeRow(tr("Автоматически определять припев"), toggle(AppSettings::AutoChorus),
                              tr("Повторять припев после каждого куплета, даже если в тексте он записан один раз")));
    format->addWidget(makeRow(tr("Нумеровать куплеты"), toggle(AppSettings::NumberVerses),
                              tr("Номер куплета в углу слайда и в полосе слайдов")));
    format->addWidget(makeRow(tr("Показывать «Куплет»/«Припев» в тексте песен"), toggle(AppSettings::ShowVerseLabels),
                              tr("Если выключено, эти пометки скрываются только на экране показа")));

    auto *import = makeSubsection(layout, tr("Импорт песен"));
    import->addWidget(makeRow(tr("Формат импорта по умолчанию"),
        combo(AppSettings::ImportFormat, {{tr("Сборник (.sps / .spb)"), QStringLiteral("sps")},
                                          {tr("ChordPro (.cho)"), QStringLiteral("cho")},
                                          {tr("Обычный текст (.txt)"), QStringLiteral("txt")}}, 220)));
    auto *folderButton = makeActionButton(QString());
    const int folderIndex = bind(AppSettings::ImportDir, [folderButton](const QVariant &value) {
        folderButton->setText(value.toString().isEmpty() ? tr("Документы") : shortPath(value.toString()));
    });
    connect(folderButton, &QPushButton::clicked, this, [this, folderIndex]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка импорта"), current(AppSettings::ImportDir).toString());
        if (!dir.isEmpty())
            setPending(AppSettings::ImportDir, dir);
        Q_UNUSED(folderIndex);
    });
    import->addWidget(makeRow(tr("Папка импорта"), folderButton, tr("Где открывается окно выбора файлов песен")));
    auto *importButton = makeActionButton(tr("Импортировать файлы песен…"));
    connect(importButton, &QPushButton::clicked, this, &SettingsPanel::songImportRequested);
    import->addWidget(makeRow(tr("Загрузить файлы"), importButton,
                              tr(".sps/.spb — сборник (каждая песня — отдельная запись), .cho — ChordPro, .txt — одна песня")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildDatabasePage()
{
    auto *page = makePage(tr("База данных"));
    auto *layout = pageLayout(page);

    auto *storage = makeSubsection(layout, tr("Хранилище"));
    m_dataDirLabel = makeStaticValue();
    storage->addWidget(makeRow(tr("Расположение базы данных"), m_dataDirLabel));
    m_dbSizeLabel = makeStaticValue();
    storage->addWidget(makeRow(tr("Размер базы данных"), m_dbSizeLabel));
    auto *openFolder = makeActionButton(tr("Открыть папку"));
    connect(openFolder, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(Database::dataDir())); });
    storage->addWidget(makeRow(tr("Папка данных"), openFolder));

    auto *maintenance = makeSubsection(layout, tr("Обслуживание"));
    auto *optimize = makeActionButton(tr("Запустить"));
    connect(optimize, &QPushButton::clicked, this, &SettingsPanel::optimizeRequested);
    maintenance->addWidget(makeRow(tr("Оптимизация базы данных"), optimize,
                                   tr("Сжимает файл базы и обновляет индексы поиска")));
    m_lastOptimizeLabel = makeStaticValue();
    maintenance->addWidget(makeRow(tr("Последняя оптимизация"), m_lastOptimizeLabel));
    m_countsLabel = makeStaticValue();
    maintenance->addWidget(makeRow(tr("Записей в библиотеке"), m_countsLabel));

    auto *sync = makeSubsection(layout, tr("Синхронизация"));
    auto *syncToggle = new ToggleSwitch;
    syncToggle->setEnabled(false);
    auto *syncWrap = new QWidget;
    auto *syncLayout = new QHBoxLayout(syncWrap);
    syncLayout->setContentsMargins(0, 0, 0, 0);
    syncLayout->setSpacing(10);
    syncLayout->addWidget(syncToggle);
    auto *soon = new QLabel(tr("Скоро"));
    soon->setFixedWidth(78);
    soon->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(Theme::TextDarkSecondary));
    syncLayout->addWidget(soon);
    sync->addWidget(makeRow(tr("Синхронизировать с облаком"), syncWrap, tr("Появится в одной из следующих версий")));

    auto *transfer = makeSubsection(layout, tr("Перенос"));
    auto *io = makeActionButton(tr("Экспорт / Импорт…"));
    connect(io, &QPushButton::clicked, this, &SettingsPanel::importExportRequested);
    transfer->addWidget(makeRow(tr("Экспорт и импорт базы данных"), io));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildHotkeysPage()
{
    auto *page = makePage(tr("Горячие клавиши"));
    auto *layout = pageLayout(page);
    layout->addWidget(hintLabel(tr("Щёлкните поле и нажмите новую клавишу. × — отключить сочетание. PageDown / PageUp (пульт-кликер) всегда листают вперёд / назад.")));

    auto *show = makeSubsection(layout, tr("Управление показом"));
    show->addWidget(makeRow(tr("Пауза показа"), keyField(AppSettings::KeyPause)));
    show->addWidget(makeRow(tr("Следующий слайд"), keyField(AppSettings::KeyNext)));
    show->addWidget(makeRow(tr("Предыдущий слайд"), keyField(AppSettings::KeyPrev)));

    auto *present = makeSubsection(layout, tr("Показ"));
    present->addWidget(makeRow(tr("Начать показ"), keyField(AppSettings::KeyGoLive), tr("Выбранное — на экран")));
    present->addWidget(makeRow(tr("Завершить показ"), keyField(AppSettings::KeyEndShow), tr("Убрать с экрана всё, что показывается")));

    auto *nav = makeSubsection(layout, tr("Навигация"));
    nav->addWidget(makeRow(tr("Поиск"), keyField(AppSettings::KeySearch), tr("Перейти в поле поиска текущего раздела")));
    nav->addWidget(makeRow(tr("Добавить"), keyField(AppSettings::KeyAdd), tr("То же, что кнопка «Добавить» в текущем разделе")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildObsPage()
{
    auto *page = makePage(tr("OBS и сеть"));
    auto *layout = pageLayout(page);

    layout->addWidget(sectionTitle(tr("Интеграция с OBS")));
    layout->addWidget(hintLabel(tr("Подключение к OBS (obs-websocket) для управления сценой показа")));
    layout->addWidget(makeRow(tr("Включить интеграцию с OBS"), toggle(AppSettings::ObsEnabled)));

    const auto fieldColumn = [layout](const QString &caption, QWidget *field) {
        auto *col = new QVBoxLayout;
        col->setSpacing(6);
        col->addWidget(new PlainTextLabel(caption, 13, QColor(Theme::TextDarkPrimary)));
        col->addWidget(field);
        layout->addLayout(col);
    };
    const auto lineFor = [this](const QString &key, bool password) {
        auto *edit = new QLineEdit;
        edit->setObjectName(QStringLiteral("SettingsLine"));
        if (password) {
            edit->setEchoMode(QLineEdit::Password);
            QAction *eye = edit->addAction(IconProvider::icon(QStringLiteral("eye"), QColor(Theme::TextDarkSecondary), 16),
                                           QLineEdit::TrailingPosition);
            connect(eye, &QAction::triggered, edit, [edit]() {
                edit->setEchoMode(edit->echoMode() == QLineEdit::Password ? QLineEdit::Normal : QLineEdit::Password);
            });
        }
        const int index = bind(key, [edit](const QVariant &value) {
            if (edit->text() != value.toString()) {
                const QSignalBlocker blocker(edit);
                edit->setText(value.toString());
            }
        });
        connect(edit, &QLineEdit::textEdited, this, [this, key, index](const QString &text) { setPending(key, text, index); });
        return edit;
    };
    QLineEdit *host = lineFor(AppSettings::ObsHost, false);
    host->setPlaceholderText(QStringLiteral("localhost"));
    fieldColumn(tr("Адрес сервера"), host);
    auto *port = new QSpinBox;
    port->setObjectName(QStringLiteral("SettingsSpin"));
    port->setRange(1, 65535);
    const int portIndex = bind(AppSettings::ObsPort, [port](const QVariant &value) {
        const QSignalBlocker blocker(port);
        port->setValue(value.toInt());
    });
    connect(port, &QSpinBox::valueChanged, this, [this, portIndex](int value) { setPending(AppSettings::ObsPort, value, portIndex); });
    fieldColumn(tr("Порт"), port);
    QLineEdit *password = lineFor(AppSettings::ObsPassword, true);
    fieldColumn(tr("Пароль"), password);

    auto *testRow = new QHBoxLayout;
    testRow->setSpacing(10);
    auto *test = makeActionButton(tr("Проверить подключение"));
    connect(test, &QPushButton::clicked, this, [this]() {
        m_obsStatusLabel->setText(tr("Подключение…"));
        m_obsStatusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600; color: %1; background: #f3f4f6; border-radius: 8px; padding: 6px 12px;")
                                            .arg(Theme::TextDarkSecondary));
        m_obsStatusLabel->show();
        emit obsTestRequested(current(AppSettings::ObsHost).toString(), current(AppSettings::ObsPort).toInt(),
                              current(AppSettings::ObsPassword).toString());
    });
    testRow->addWidget(test);
    m_obsStatusLabel = new QLabel;
    m_obsStatusLabel->hide();
    testRow->addWidget(m_obsStatusLabel);
    testRow->addStretch();
    layout->addLayout(testRow);

    m_sceneBox = combo(AppSettings::ObsScene, {{tr("Не переключать сцену"), QString()}});
    fieldColumn(tr("Сцена показа"), m_sceneBox);
    layout->addWidget(hintLabel(tr("При выводе на экран Sermon переключает OBS на эту сцену. Если OBS не подключён, слайды всё равно будут выводиться на экран показа.")));

    auto *output = makeSubsection(layout, tr("Слайды в OBS (источник «Браузер»)"));
    m_obsUrlLabel = makeStaticValue();
    m_obsUrlLabel->setWordWrap(false); // a URL must not break at "//"
    output->addWidget(makeRow(tr("Веб-адрес для источника Browser"), m_obsUrlLabel,
                              tr("Добавьте в OBS источник «Браузер» с этим адресом — слайды обновляются синхронно с показом")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildBackupPage()
{
    auto *page = makePage(tr("Резервная копия"));
    auto *layout = pageLayout(page);

    auto *automatic = makeSubsection(layout, tr("Автоматическое резервное копирование"));
    automatic->addWidget(makeRow(tr("Включить автобэкап"), toggle(AppSettings::BackupAuto)));
    automatic->addWidget(makeRow(tr("Частота"),
        combo(AppSettings::BackupFrequency, {{tr("Ежедневно"), QStringLiteral("daily")},
                                             {tr("Еженедельно"), QStringLiteral("weekly")},
                                             {tr("Вручную"), QStringLiteral("manual")}})));
    automatic->addWidget(makeRow(tr("Хранить копий"),
        combo(AppSettings::BackupKeep, {{QStringLiteral("3"), 3}, {QStringLiteral("7"), 7}, {QStringLiteral("14"), 14}, {QStringLiteral("30"), 30}}, 100),
        tr("Более старые автоматические копии удаляются")));
    auto *dirButton = makeActionButton(QString());
    bind(AppSettings::BackupDir, [dirButton](const QVariant &value) {
        dirButton->setText(value.toString().isEmpty() ? tr("Папка данных Sermon") : shortPath(value.toString()));
    });
    connect(dirButton, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка для резервных копий"), AppSettings::backupDir());
        if (!dir.isEmpty())
            setPending(AppSettings::BackupDir, dir);
    });
    automatic->addWidget(makeRow(tr("Папка для копий"), dirButton, tr("Можно выбрать флешку или папку Google Drive / OneDrive")));

    auto *manual = makeSubsection(layout, tr("Ручное управление"));
    auto *create = makeActionButton(tr("Создать сейчас"));
    connect(create, &QPushButton::clicked, this, &SettingsPanel::backupNowRequested);
    manual->addWidget(makeRow(tr("Создать резервную копию"), create));
    auto *restore = makeActionButton(tr("Выбрать файл"));
    connect(restore, &QPushButton::clicked, this, &SettingsPanel::restoreRequested);
    manual->addWidget(makeRow(tr("Восстановить из резервной копии"), restore));

    auto *last = makeSubsection(layout, tr("Последняя копия"));
    m_lastBackupLabel = makeStaticValue();
    last->addWidget(makeRow(tr("Дата последней копии"), m_lastBackupLabel));
    m_backupStatusLabel = new QLabel;
    last->addWidget(makeRow(tr("Статус"), m_backupStatusLabel));

    layout->addStretch();
    refreshStatusRows();
    return page;
}

QWidget *SettingsPanel::buildAppearancePage()
{
    auto *page = makePage(tr("Внешний вид"));
    auto *layout = pageLayout(page);

    auto *theme = makeSubsection(layout, tr("Тема"));
    theme->addWidget(makeRow(tr("Цветовая схема"),
        combo(AppSettings::Theme, {{tr("Светлая"), QStringLiteral("light")}, {tr("Тёмная"), QStringLiteral("dark")}})));

    auto *interface = makeSubsection(layout, tr("Интерфейс"));
    interface->addWidget(makeRow(tr("Плотность интерфейса"),
        combo(AppSettings::Density, {{tr("Обычная"), QStringLiteral("normal")}, {tr("Компактная"), QStringLiteral("compact")}}),
        tr("Компактная — всё немного мельче, помещается больше")));
    interface->addWidget(makeRow(tr("Закруглённые углы"), toggle(AppSettings::Rounded)));

    auto *font = makeSubsection(layout, tr("Шрифт интерфейса"));
    QList<QPair<QString, QVariant>> fonts;
    for (const QString &family : {QStringLiteral("Inter"), QStringLiteral("Segoe UI"), QStringLiteral("Montserrat"), QStringLiteral("Arial"),
                                  QStringLiteral("Verdana"), QStringLiteral("Tahoma")}) {
        if (QFontDatabase::families().contains(family) || family == QLatin1String("Inter"))
            fonts.append({family, family});
    }
    font->addWidget(makeRow(tr("Основной шрифт"), combo(AppSettings::UiFont, fonts)));

    layout->addStretch();
    return page;
}

// ---- Context / status -----------------------------------------------------------

void SettingsPanel::setContext(const Context &context)
{
    const auto withTip = [](QLabel *label) { label->setToolTip(label->text()); };
    m_context = context;
    m_dataDirLabel->setText(QDir::toNativeSeparators(context.dataDir + QStringLiteral("/sermon.db")));
    const QFileInfo dbFile(context.dataDir + QStringLiteral("/sermon.db"));
    m_dbSizeLabel->setText(dbFile.exists() ? tr("%1 МБ").arg(dbFile.size() / 1024.0 / 1024.0, 0, 'f', 1) : tr("файл ещё не создан"));
    m_obsUrlLabel->setText(context.obsUrl);
    m_countsLabel->setText(tr("Песни: %1 · Объявления: %2 · Фото: %3 · Видео: %4")
                               .arg(context.counts.value(ContentType::Song, 0))
                               .arg(context.counts.value(ContentType::Announcement, 0))
                               .arg(context.counts.value(ContentType::Photo, 0))
                               .arg(context.counts.value(ContentType::Video, 0)));
    for (QLabel *label : {m_dataDirLabel, m_dbSizeLabel, m_obsUrlLabel, m_countsLabel})
        withTip(label);
    for (const auto &refresh : std::as_const(m_contextRefreshers))
        refresh();
    refreshStatusRows();
}

void SettingsPanel::refreshStatusRows()
{
    const QDateTime optimized = QDateTime::fromString(AppSettings::value(AppSettings::LastOptimize).toString(), Qt::ISODate);
    if (m_lastOptimizeLabel)
        m_lastOptimizeLabel->setText(optimized.isValid() ? optimized.toString(QStringLiteral("dd.MM.yyyy, HH:mm")) : tr("ещё не выполнялась"));
    if (!m_lastBackupLabel)
        return;
    const QDateTime last = QDateTime::fromString(AppSettings::value(AppSettings::BackupLast).toString(), Qt::ISODate);
    m_lastBackupLabel->setText(last.isValid() ? last.toString(QStringLiteral("dd.MM.yyyy, HH:mm")) : tr("ещё не создавалась"));
    const bool ok = AppSettings::value(AppSettings::BackupLastOk).toBool();
    const QString color = !last.isValid() ? Theme::TextDarkSecondary : ok ? QStringLiteral("#16a34a") : QStringLiteral("#dc2626");
    m_backupStatusLabel->setText(QStringLiteral("● ") + (!last.isValid() ? tr("Нет данных") : ok ? tr("Успешно") : tr("Ошибка")));
    m_backupStatusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600; color: %1;").arg(color));
}

void SettingsPanel::setObsStatus(bool connected, const QString &text, const QStringList &scenes)
{
    m_obsStatusLabel->setText(QStringLiteral("● ") + text);
    m_obsStatusLabel->setStyleSheet(QStringLiteral("font-size: 12.5px; font-weight: 600; color: %1; background: %2; border-radius: 8px; padding: 6px 12px;")
                                        .arg(connected ? QStringLiteral("#16a34a") : QStringLiteral("#dc2626"),
                                             connected ? QStringLiteral("#e6f6ec") : QStringLiteral("#fdeaea")));
    m_obsStatusLabel->show();
    if (!connected)
        return;
    const QSignalBlocker blocker(m_sceneBox);
    while (m_sceneBox->count() > 1)
        m_sceneBox->removeItem(1);
    for (const QString &scene : scenes)
        m_sceneBox->addItem(scene, scene);
    const QString chosen = current(AppSettings::ObsScene).toString();
    int index = m_sceneBox->findData(chosen);
    if (index < 0 && !chosen.isEmpty()) {
        m_sceneBox->addItem(chosen, chosen);
        index = m_sceneBox->count() - 1;
    }
    m_sceneBox->setCurrentIndex(qMax(0, index));
}
