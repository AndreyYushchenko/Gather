#include "SettingsPanel.h"
#include "DisplaySettings.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSqlError>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const char *kLastBackupKey = "backup/lastExportedAt";
}

namespace {

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

QCheckBox *makeToggle(bool checked)
{
    auto *box = new QCheckBox;
    box->setObjectName(QStringLiteral("Toggle"));
    box->setCursor(Qt::PointingHandCursor);
    box->setChecked(checked);
    return box;
}

QLabel *sectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    return label;
}

QWidget *makeRow(const QString &label, QWidget *control, const QString &description = QString())
{
    auto *row = new QWidget;
    row->setMaximumWidth(620);
    auto *rootLayout = new QVBoxLayout(row);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(4);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(16);
    auto *labelWidget = new PlainTextLabel(label, 14, QColor(Theme::TextDarkPrimary));
    topRow->addWidget(labelWidget);
    topRow->addStretch(1);
    topRow->addWidget(control, 0, Qt::AlignVCenter);
    rootLayout->addLayout(topRow);

    if (!description.isEmpty()) {
        // A wrapping QLabel needs to be a *direct* child of the layout that
        // determines the row's height. Nested one level inside the top
        // QHBoxLayout (which also has to negotiate space with the control),
        // its heightForWidth didn't reliably reach the row, so the row came
        // out too short and the next row's text bled into this one. As a
        // direct QVBoxLayout child spanning the full row width, it doesn't
        // have to compete with anything for width and heightForWidth just
        // works.
        auto *desc = new QLabel(description);
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;").arg(Theme::TextDarkSecondary));
        rootLayout->addWidget(desc);
    }
    return row;
}

QWidget *makeSubsection(QVBoxLayout *parent, const QString &title)
{
    parent->addWidget(sectionTitle(title));
    auto *rows = new QWidget;
    auto *rowsLayout = new QVBoxLayout(rows);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(14);
    parent->addWidget(rows);
    return rows;
}

QComboBox *makeValueBox(const QStringList &items, int current = 0)
{
    auto *box = new QComboBox;
    box->setObjectName(QStringLiteral("FieldBox"));
    box->addItems(items);
    box->setCurrentIndex(current);
    box->setFixedHeight(34);
    box->setMinimumWidth(180);
    return box;
}

QLabel *makeStaticValue(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    // See the comment in makeRow(): a wrapping label placed as a row's
    // right-hand "control" needs an explicit max width, otherwise its
    // height-for-width doesn't propagate through the row's QHBoxLayout and
    // the row ends up too short, letting the next row overlap it.
    label->setMaximumWidth(320);
    label->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(Theme::TextDarkSecondary));
    return label;
}

QPushButton *makeActionButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setObjectName(QStringLiteral("OutlineButton"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLabel *makeKeyValue(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("KeyValueBadge"));
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumWidth(50);
    label->setFixedHeight(28);
    label->setStyleSheet(QStringLiteral("QLabel#KeyValueBadge { border: 1px solid %1; border-radius: 7px; font-weight: 600; font-size: 12.5px; padding: 0 10px; color: %2; }")
                              .arg(Theme::BorderLight, Theme::TextDarkPrimary));
    return label;
}

QWidget *makePage(const QString &title)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    layout->addWidget([&] {
        auto *label = new QLabel(title);
        label->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
        return label;
    }());
    return page;
}

QVBoxLayout *pageLayout(QWidget *page)
{
    return qobject_cast<QVBoxLayout *>(page->layout());
}

} // namespace

void SettingsPanel::addFontSizeRows(QVBoxLayout *rows, ContentType type)
{
    // A live sample so the effect of the controls below is obvious right
    // here — otherwise there's nothing on screen to show it changed unless
    // something happens to already be live on the projector.
    constexpr int previewRefHeight = 260;
    auto *preview = new QLabel(type == ContentType::BibleVerse
        ? tr("Від Івана 3:16\n\nТак бо Бог полюбив світ, що дав Сина Свого Однородженого…")
        : tr("Слава Тобі, Господь наш,\nхай буде благословенне Ім'я Твоє!"));
    preview->setObjectName(QStringLiteral("FontPreview"));
    preview->setAlignment(DisplaySettings::qtAlignment());
    preview->setWordWrap(true);
    preview->setMinimumHeight(140);
    preview->setStyleSheet(QStringLiteral(
        "QLabel#FontPreview { background: #0b0e14; border-radius: 12px; color: #ffffff; padding: 20px; }"));
    auto refreshPreview = [preview, type]() {
        QFont font(DisplaySettings::fontFamily(type));
        font.setPointSize(DisplaySettings::fontPointSize(type, previewRefHeight));
        font.setWeight(QFont::Bold);
        preview->setFont(font);
        preview->setAlignment(DisplaySettings::qtAlignment());
    };
    refreshPreview();
    rows->addWidget(preview);

    const QStringList fonts = DisplaySettings::availableFonts();
    auto *fontBox = makeValueBox(fonts, qMax(0, fonts.indexOf(DisplaySettings::preferredFontFamily(type))));
    connect(fontBox, &QComboBox::currentIndexChanged, this, [this, fonts, type, refreshPreview](int index) {
        if (index >= 0 && index < fonts.size()) {
            DisplaySettings::setFontFamily(type, fonts.at(index));
            refreshPreview();
            emit displaySettingsChanged();
        }
    });
    rows->addWidget(makeRow(tr("Шрифт"), fontBox));

    auto *scaleRow = new QWidget;
    auto *scaleLayout = new QHBoxLayout(scaleRow);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleLayout->setSpacing(10);
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(50, 200);
    slider->setSingleStep(5);
    slider->setPageStep(10);
    slider->setValue(DisplaySettings::textScalePercent(type));
    slider->setFixedWidth(160);
    auto *percentLabel = new QLabel(tr("%1%").arg(slider->value()));
    percentLabel->setFixedWidth(44);
    percentLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(Theme::TextDarkPrimary));
    connect(slider, &QSlider::valueChanged, this, [this, percentLabel, type, refreshPreview](int value) {
        percentLabel->setText(tr("%1%").arg(value));
        DisplaySettings::setTextScalePercent(type, value);
        refreshPreview();
        emit displaySettingsChanged();
    });
    scaleLayout->addWidget(slider);
    scaleLayout->addWidget(percentLabel);
    rows->addWidget(makeRow(tr("Размер текста на экране показа"), scaleRow));
}

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
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(4);

    auto *title = new QLabel(tr("Настройки"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    root->addWidget(title);

    auto *subtitle = new QLabel(tr("Настройте Gather под нужды вашей церкви"));
    subtitle->setStyleSheet(QStringLiteral("font-size: 13.5px; color: %1;").arg(Theme::TextDarkSecondary));
    root->addWidget(subtitle);
    root->addSpacing(16);

    auto *bodyRow = new QHBoxLayout;
    bodyRow->setSpacing(24);
    root->addLayout(bodyRow, 1);

    // ---- Nav column ----
    auto *navColumn = new QWidget;
    navColumn->setFixedWidth(230);
    auto *navLayout = new QVBoxLayout(navColumn);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(2);

    struct NavEntry { const char *icon; const char *label; };
    const NavEntry entries[] = {
        {"settings", "Общие"},
        {"monitor", "Показ"},
        {"book-open", "Библия"},
        {"music", "Песни"},
        {"database", "База данных"},
        {"keyboard", "Горячие клавиши"},
        {"wifi", "OBS и сеть"},
        {"cloud", "Резервная копия"},
    };

    m_stack = new QStackedWidget;
    populateStack();

    auto *navGroup = new QButtonGroup(this);
    int index = 0;
    for (const NavEntry &entry : entries) {
        auto *row = makeNavRow(QString::fromUtf8(entry.icon), tr(entry.label));
        navGroup->addButton(row, index);
        navLayout->addWidget(row);
        ++index;
    }
    navGroup->button(0)->setChecked(true);
    connect(navGroup, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);
    navLayout->addStretch();
    bodyRow->addWidget(navColumn);

    // ---- Card ----
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("SettingsCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 26, 28, 22);
    cardLayout->setSpacing(20);

    // The settings pages can be taller than the window (lots of rows on a
    // short display) — without a scroll area, Qt's layout engine "solves"
    // that overflow by force-shrinking whichever rows have wrapping text
    // (since they're the only ones that CAN shrink) down to near zero,
    // which reads as rows merging/overlapping. Scrolling instead of
    // shrinking is the actual fix.
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; }"));
    scrollArea->viewport()->setStyleSheet(QStringLiteral("background: #ffffff;"));
    scrollArea->setWidget(m_stack);
    cardLayout->addWidget(scrollArea, 1);

    // Every control above applies immediately (via DisplaySettings/QSettings)
    // rather than staging a draft, so there's nothing for a Save/Cancel pair
    // to actually do — only a real action belongs here: resetting the
    // presentation-styling settings back to their defaults.
    auto *actionsRow = new QHBoxLayout;
    auto *resetButton = new QPushButton(tr("  Сбросить оформление показа"));
    resetButton->setObjectName(QStringLiteral("OutlineButton"));
    resetButton->setIcon(IconProvider::icon(QStringLiteral("rotate-ccw"), QColor(Theme::TextDarkPrimary), 15));
    resetButton->setCursor(Qt::PointingHandCursor);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        const auto reply = QMessageBox::question(this, tr("Сброс оформления показа"),
            tr("Сбросить шрифты, размер текста, выравнивание и видимость подписей/ссылок "
               "(разделы «Показ» и «Библия») к значениям по умолчанию?"));
        if (reply != QMessageBox::Yes)
            return;
        DisplaySettings::resetAll();
        emit displaySettingsChanged();
        const int current = m_stack->currentIndex();
        while (m_stack->count() > 0) {
            QWidget *page = m_stack->widget(0);
            m_stack->removeWidget(page);
            page->deleteLater();
        }
        populateStack();
        m_stack->setCurrentIndex(current);
    });
    actionsRow->addWidget(resetButton);
    actionsRow->addStretch();
    cardLayout->addLayout(actionsRow);

    bodyRow->addWidget(card, 1);

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
            border: 1px solid %2; border-radius: 9px; padding: 6px 10px;
            font-size: 13px; color: %1; background: #ffffff;
        }
        QCheckBox#Toggle::indicator { width: 36px; height: 20px; border-radius: 10px; border: none; background: #d1d5db; }
        QCheckBox#Toggle::indicator:checked { background: %3; }
        QPushButton#OutlineButton {
            background: #ffffff; border: 1px solid %2; border-radius: 9px;
            padding: 9px 16px; font-weight: 600; font-size: 13.5px; color: %1;
        }
        QPushButton#OutlineButton:hover { background: #f3f4f6; }
        QPushButton#PrimaryButton {
            background: %3; border: none; border-radius: 9px; padding: 9px 16px;
            font-weight: 600; font-size: 13.5px; color: #ffffff;
        }
        QPushButton#PrimaryButton:hover { background: #255ed1; }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::AccentBlue, Theme::TextDarkSecondary, Theme::AccentBlueBg));
}

void SettingsPanel::populateStack()
{
    m_stack->addWidget(buildGeneralPage());
    m_stack->addWidget(buildShowPage());
    m_stack->addWidget(buildBiblePage());
    m_stack->addWidget(buildSongsPage());
    m_stack->addWidget(buildDatabasePage());
    m_stack->addWidget(buildHotkeysPage());
    m_stack->addWidget(buildObsPage());
    m_stack->addWidget(buildBackupPage());
    // The pages above just created fresh copies of m_dataDirLabel and co.,
    // so the live values need reapplying — same call the last real setInfo()
    // made, replayed from cache.
    if (m_hasInfo)
        setInfo(m_lastDataDir, m_lastObsUrl, m_lastBibleTranslation, m_lastBibleBookCount, m_lastBibleVerseCount, m_lastCounts);
}

QWidget *SettingsPanel::buildGeneralPage()
{
    auto *page = makePage(tr("Общие"));
    auto *layout = pageLayout(page);

    auto *notesRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Заметки оператора"))->layout());
    notesRows->addWidget(makeRow(tr("Автосохранение"),
        makeStaticValue(tr("Включено — сохраняются автоматически через ~0.6 секунды после ввода"))));

    auto *showRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Проектор и показ"))->layout());
    showRows->addWidget(makeRow(tr("Полноэкранный режим на проекторе"),
        makeStaticValue(tr("Включается автоматически, если подключён второй монитор"))));
    showRows->addWidget(makeRow(tr("Горячая клавиша для чёрного экрана"), [] {
        auto *label = new QLabel(QStringLiteral("B"));
        label->setAlignment(Qt::AlignCenter);
        label->setFixedSize(40, 28);
        label->setStyleSheet(QStringLiteral("border: 1px solid %1; border-radius: 7px; font-weight: 600;").arg(Theme::BorderLight));
        return label;
    }()));
    auto *alignBox = makeValueBox({tr("По центру"), tr("Слева")},
        DisplaySettings::alignment() == DisplaySettings::Alignment::Left ? 1 : 0);
    connect(alignBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        DisplaySettings::setAlignment(index == 1 ? DisplaySettings::Alignment::Left : DisplaySettings::Alignment::Center);
        emit displaySettingsChanged();
    });
    showRows->addWidget(makeRow(tr("Выравнивание текста на слайдах"), alignBox));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildShowPage()
{
    auto *page = makePage(tr("Показ"));
    auto *layout = pageLayout(page);

    auto *slideRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Внешний вид слайдов (песни, объявления, фото)"))->layout());
    addFontSizeRows(slideRows, ContentType::Song);
    slideRows->addWidget(makeRow(tr("Тень текста для читаемости"),
        makeStaticValue(tr("Включена — применяется всегда, когда за текстом есть фон"))));

    auto *labelsToggle = makeToggle(DisplaySettings::showVerseLabels());
    connect(labelsToggle, &QCheckBox::toggled, this, [this](bool checked) {
        DisplaySettings::setShowVerseLabels(checked);
        emit displaySettingsChanged();
    });
    slideRows->addWidget(makeRow(tr("Показывать «Куплет»/«Припев» в тексте песен"), labelsToggle,
        tr("Если выключено, эти пометки скрываются только на экране показа — в тексте песни в библиотеке они остаются")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildBiblePage()
{
    auto *page = makePage(tr("Библия"));
    auto *layout = pageLayout(page);

    auto *displayRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Внешний вид на экране показа"))->layout());
    addFontSizeRows(displayRows, ContentType::BibleVerse);
    auto *refToggle = makeToggle(DisplaySettings::showBibleReference());
    connect(refToggle, &QCheckBox::toggled, this, [this](bool checked) {
        DisplaySettings::setShowBibleReference(checked);
        emit displaySettingsChanged();
    });
    displayRows->addWidget(makeRow(tr("Показывать ссылку (книга, глава:стих) на экране"), refToggle,
        tr("Ссылка выводится первой строкой перед текстом стиха на проекторе и в OBS")));

    auto *translationRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Перевод"))->layout());
    m_bibleInfoLabel = makeStaticValue(tr("нет данных"));
    translationRows->addWidget(makeRow(tr("Загруженный перевод"), m_bibleInfoLabel,
        tr("Другой перевод импортируется отдельно через scripts/import_bible.py — сменить его из интерфейса пока нельзя")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildSongsPage()
{
    // No chords/columns, chorus auto-detection, verse numbering, or a
    // ChordPro importer actually exist in the app — those rows used to sit
    // here as decorative controls with nothing behind them. The song model
    // (ContentItem::text) is plain lyrics text; the only real per-song
    // format toggle (show "Куплет"/"Припев" labels) already lives on the
    // Показ page, since it affects the projected slide, not song storage.
    auto *page = makePage(tr("Песни"));
    auto *layout = pageLayout(page);

    auto *importRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Импорт песен"))->layout());
    auto *importButton = makeActionButton(tr("Импортировать файлы песен..."));
    connect(importButton, &QPushButton::clicked, this, &SettingsPanel::songImportRequested);
    importRows->addWidget(makeRow(tr("Загрузить файлы"), importButton,
        tr("Поддерживаются .txt (один файл — одна песня) и сборники .sps/.spb (каждая песня внутри — отдельная запись)")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildDatabasePage()
{
    auto *page = makePage(tr("База данных"));
    auto *layout = pageLayout(page);

    auto *storageRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Хранилище"))->layout());
    m_dataDirLabel = makeStaticValue(QString());
    storageRows->addWidget(makeRow(tr("Расположение базы данных"), m_dataDirLabel));
    m_dbSizeLabel = makeStaticValue(QString());
    storageRows->addWidget(makeRow(tr("Размер базы данных"), m_dbSizeLabel));

    auto *maintenanceRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Обслуживание"))->layout());
    auto *optimizeButton = makeActionButton(tr("Запустить"));
    connect(optimizeButton, &QPushButton::clicked, this, [this]() {
        QSqlQuery query(QStringLiteral("VACUUM"));
        if (query.lastError().isValid())
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось оптимизировать базу данных: %1").arg(query.lastError().text()));
        else
            QMessageBox::information(this, tr("Готово"), tr("База данных оптимизирована."));
    });
    maintenanceRows->addWidget(makeRow(tr("Оптимизация базы данных"), optimizeButton,
        tr("Сжимает файл базы данных (VACUUM) — полезно после удаления большого числа записей")));
    m_countsLabel = makeStaticValue(QString());
    maintenanceRows->addWidget(makeRow(tr("Записей в библиотеке"), m_countsLabel));

    auto *backupRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Резервное копирование"))->layout());
    auto *ioButton = makeActionButton(tr("Экспорт / Импорт базы данных..."));
    connect(ioButton, &QPushButton::clicked, this, &SettingsPanel::importExportRequested);
    backupRows->addWidget(ioButton, 0, Qt::AlignLeft);

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildHotkeysPage()
{
    auto *page = makePage(tr("Горячие клавиши"));
    auto *layout = pageLayout(page);

    auto *showRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Управление показом"))->layout());
    showRows->addWidget(makeRow(tr("Чёрный экран"), makeKeyValue(QStringLiteral("B"))));
    showRows->addWidget(makeRow(tr("Пауза показа"), makeKeyValue(tr("Пробел"))));
    showRows->addWidget(makeRow(tr("Следующий слайд"), makeKeyValue(QStringLiteral("→"))));
    showRows->addWidget(makeRow(tr("Предыдущий слайд"), makeKeyValue(QStringLiteral("←"))));

    auto *presentRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Показ"))->layout());
    presentRows->addWidget(makeRow(tr("Начать показ"), makeKeyValue(QStringLiteral("F5"))));
    presentRows->addWidget(makeRow(tr("Закрыть окно показа"), makeKeyValue(tr("Esc")),
        tr("Работает, когда активно окно показа (проектор)")));

    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildObsPage()
{
    auto *page = makePage(tr("OBS и сеть"));
    auto *layout = pageLayout(page);

    auto *outputRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Вывод на OBS"))->layout());
    m_obsUrlLabel = makeStaticValue(QString());
    outputRows->addWidget(makeRow(tr("Веб-адрес для источника Browser"), m_obsUrlLabel));
    outputRows->addWidget(makeRow(tr("Локальный сервер"), makeStaticValue(tr("Запускается автоматически при старте приложения"))));

    auto *networkRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Сеть"))->layout());
    m_obsIpLabel = makeStaticValue(QString());
    networkRows->addWidget(makeRow(tr("IP-адрес устройства"), m_obsIpLabel));

    auto *hint = new QLabel(tr("Добавьте источник «Браузер» (Browser Source) в OBS с этим адресом — слайд будет обновляться в сцене синхронно с показом. Оба устройства должны быть в одной локальной сети.\n\n"
                                "Сервер отдаёт данные без пароля — используйте его только в доверенной локальной сети."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("font-size: 12.5px; color: %1;").arg(Theme::TextDarkSecondary));
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

QWidget *SettingsPanel::buildBackupPage()
{
    // No scheduled/automatic backups exist — only the manual export/import
    // flow (MainWindow::onImportExport). A fake "auto-backup" toggle with a
    // frequency/retention dropdown used to sit here doing nothing.
    auto *page = makePage(tr("Резервная копия"));
    auto *layout = pageLayout(page);

    auto *manualRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Резервное копирование"))->layout());
    auto *createButton = makeActionButton(tr("Создать сейчас"));
    connect(createButton, &QPushButton::clicked, this, &SettingsPanel::importExportRequested);
    manualRows->addWidget(makeRow(tr("Создать резервную копию"), createButton));
    auto *restoreButton = makeActionButton(tr("Выбрать файл"));
    connect(restoreButton, &QPushButton::clicked, this, &SettingsPanel::importExportRequested);
    manualRows->addWidget(makeRow(tr("Восстановить из резервной копии"), restoreButton));

    auto *lastRows = qobject_cast<QVBoxLayout *>(makeSubsection(layout, tr("Последняя копия"))->layout());
    m_lastBackupLabel = makeStaticValue(tr("ещё не создавалась"));
    lastRows->addWidget(makeRow(tr("Дата последней копии"), m_lastBackupLabel));

    layout->addStretch();
    return page;
}

void SettingsPanel::setInfo(const QString &dataDir, const QString &obsUrl,
                             const QString &bibleTranslation, int bibleBookCount, int bibleVerseCount,
                             const QMap<ContentType, int> &counts)
{
    m_hasInfo = true;
    m_lastDataDir = dataDir;
    m_lastObsUrl = obsUrl;
    m_lastBibleTranslation = bibleTranslation;
    m_lastBibleBookCount = bibleBookCount;
    m_lastBibleVerseCount = bibleVerseCount;
    m_lastCounts = counts;

    if (m_dataDirLabel)
        m_dataDirLabel->setText(dataDir);
    if (m_dbSizeLabel) {
        QFileInfo dbFile(dataDir + QStringLiteral("/gather.db"));
        m_dbSizeLabel->setText(dbFile.exists()
            ? tr("%1 МБ").arg(dbFile.size() / 1024.0 / 1024.0, 0, 'f', 1)
            : tr("файл ещё не создан"));
    }
    if (m_obsUrlLabel)
        m_obsUrlLabel->setText(obsUrl);
    if (m_obsIpLabel) {
        const QString host = QUrl(obsUrl).host();
        m_obsIpLabel->setText(host.isEmpty() ? tr("недоступно") : host);
    }
    if (m_bibleInfoLabel) {
        m_bibleInfoLabel->setText(bibleTranslation.isEmpty()
            ? tr("нет данных")
            : tr("%1 (%2 книг, %3 стихов)").arg(bibleTranslation).arg(bibleBookCount).arg(bibleVerseCount));
    }
    if (m_countsLabel) {
        m_countsLabel->setText(tr("Песни: %1 · Стихи (в избранном): %2 · Объявления: %3 · Фото: %4")
            .arg(counts.value(ContentType::Song, 0))
            .arg(counts.value(ContentType::BibleVerse, 0))
            .arg(counts.value(ContentType::Announcement, 0))
            .arg(counts.value(ContentType::Photo, 0)));
    }
    if (m_lastBackupLabel) {
        const QDateTime last = QSettings().value(QLatin1String(kLastBackupKey)).toDateTime();
        m_lastBackupLabel->setText(last.isValid() ? last.toString(QStringLiteral("dd.MM.yyyy HH:mm")) : tr("ещё не создавалась"));
    }
}
