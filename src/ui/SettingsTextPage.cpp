#include "SettingsPanel.h"
#include "DisplaySettings.h"
#include "IconProvider.h"
#include "Theme.h"
#include "ToggleSwitch.h"
#include "core/AppSettings.h"
#include "display/TextSlideWidget.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QLabel *label(const QString &text, int size = 13, bool muted = false, bool bold = false)
{
    auto *w = new QLabel(text);
    w->setWordWrap(true);
    w->setMinimumWidth(0);
    w->setStyleSheet(QStringLiteral("color:%1; font-size:%2px; font-weight:%3; background:transparent;")
        .arg(muted ? Theme::TextDarkSecondary : Theme::TextDarkPrimary).arg(size).arg(bold ? 600 : 400));
    return w;
}

QVBoxLayout *card(QWidget *&widget, const QString &title, const QString &subtitle = {})
{
    widget = new QFrame;
    widget->setObjectName(QStringLiteral("TextSettingsCard"));
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    if (!title.isEmpty()) layout->addWidget(label(title, 17, false, true));
    if (!subtitle.isEmpty()) layout->addWidget(label(subtitle, 12, true));
    return layout;
}

QWidget *row(const QString &title, QWidget *control, const QString &hint = {}, int height = 40)
{
    auto *w = new QWidget;
    w->setMinimumHeight(height);
    auto *layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto *text = new QVBoxLayout;
    text->setSpacing(3);
    text->addWidget(label(title));
    if (!hint.isEmpty()) text->addWidget(label(hint, 11, true));
    layout->addLayout(text, 1);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    control->setAccessibleName(title);
    return w;
}

// Stack the columns at small window sizes without clipping controls.
class DisplayColumns : public QWidget {
public:
    QBoxLayout *columns = new QBoxLayout(QBoxLayout::LeftToRight, this);
    DisplayColumns() { columns->setContentsMargins(0, 0, 0, 0); columns->setSpacing(16); }
    QSize minimumSizeHint() const override {
        QSize hint = QWidget::minimumSizeHint(); hint.setWidth(0); return hint;
    }
protected:
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        const auto direction = width() < 860 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
        if (columns->direction() != direction) columns->setDirection(direction);
    }
};

class StylePreview : public QWidget {
public:
    explicit StylePreview(QWidget *parent = nullptr) : QWidget(parent), m_text(new TextSlideWidget(this)) {
        setMinimumHeight(180);
        setMaximumHeight(220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void refresh(const DisplaySettings::TextStyle &style, const QString &background,
                 bool reference, bool separateNumber, bool split) {
        m_style = style;
        QString path = DisplaySettings::backgroundPath(background);
        if (background == QLatin1String("builtin:blue_waves")) path = QStringLiteral(":/backgrounds/blue_waves_thumb.png");
        if (background == QLatin1String("builtin:warm_glow")) path = QStringLiteral(":/backgrounds/warm_glow_thumb.png");
        if (m_path != path) { m_path = path; m_background.load(path); }
        QString text = style.bible ? tr("Ибо так возлюбил Бог мир,\nчто отдал Сына Своего Единородного,\nдабы всякий верующий в Него не погиб,\nно имел жизнь вечную.")
                                   : tr("Великий Ты, Господь\nДостоин славы\nчести и хвалы");
        if (split) text = text.split(QLatin1Char('\n')).mid(0, style.maxLines).join(QLatin1Char('\n'));
        if (style.bible && separateNumber) text.prepend(QStringLiteral("16\n"));
        m_text->setText(text, style.bible ? (reference ? tr("Иоанна 3:16") : QString()) : tr("ВЕЛИКИЙ БОГ"), false, style);
        update();
    }
protected:
    void resizeEvent(QResizeEvent *e) override { QWidget::resizeEvent(e); m_text->setGeometry(rect()); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip; clip.addRoundedRect(QRectF(rect()), Theme::radius(8), Theme::radius(8));
        p.setClipPath(clip);
        p.fillRect(rect(), QColor(QStringLiteral("#101827")));
        if (!m_background.isNull()) {
            const QPixmap image = m_background.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            p.drawPixmap((width() - image.width()) / 2, (height() - image.height()) / 2, image);
        }
        if (m_style.dim) p.fillRect(rect(), QColor(0, 0, 0, qRound(255 * m_style.dimOpacity / 100.0)));
    }
private:
    TextSlideWidget *m_text;
    DisplaySettings::TextStyle m_style;
    QPixmap m_background;
    QString m_path;
};
}

QWidget *SettingsPanel::buildBiblePage() { return buildTextDisplayPage(ContentType::BibleVerse); }
QWidget *SettingsPanel::buildSongsPage() { return buildTextDisplayPage(ContentType::Song); }

QWidget *SettingsPanel::buildTextDisplayPage(ContentType type)
{
    const bool bible = type == ContentType::BibleVerse;
    const auto key = [type](const char *field) { return DisplaySettings::styleKey(type, QString::fromLatin1(field)); };
    auto *page = new QWidget;
    page->setObjectName(bible ? QStringLiteral("BibleSettingsV2") : QStringLiteral("SongSettingsV2"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 4, 0);
    root->setSpacing(16);

    auto *banner = new QFrame;
    banner->setObjectName(QStringLiteral("TextSettingsBanner"));
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(16, 14, 16, 14);
    bannerLayout->setSpacing(16);
    auto *icon = new QLabel;
    icon->setFixedSize(36, 36);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral("background:transparent;"));
    icon->setPixmap(IconProvider::pixmap(bible ? QStringLiteral("book-open") : QStringLiteral("music"), QColor(Theme::AccentBlue), 26));
    bannerLayout->addWidget(icon);
    auto *bannerText = new QVBoxLayout;
    bannerText->setSpacing(4);
    auto *bannerTitle = label(bible ? tr("Отдельные настройки показа Библии") : tr("Отдельные настройки показа песен"), 14, false, true);
    bannerTitle->setStyleSheet(QStringLiteral("color:%1; font-size:14px; font-weight:600; background:transparent;").arg(Theme::AccentBlue));
    bannerText->addWidget(bannerTitle);
    bannerText->addWidget(label(bible ? tr("Эти параметры применяются только к тексту Библии и не влияют на песни и объявления.")
                                    : tr("Эти параметры применяются только к песням и сборникам и не влияют на Библию и объявления."), 12, true));
    bannerLayout->addLayout(bannerText, 1);
    if (!bible) {
        auto *close = new QPushButton;
        close->setIcon(IconProvider::icon(QStringLiteral("x"), QColor(Theme::TextDarkSecondary), 16));
        close->setFixedSize(22, 22);
        close->setFlat(true);
        close->setToolTip(tr("Скрыть подсказку"));
        connect(close, &QPushButton::clicked, banner, &QWidget::hide);
        bannerLayout->addWidget(close, 0, Qt::AlignTop);
    }
    root->addWidget(banner);

    const auto number = [this](const QString &setting, double minimum, double maximum, double step, const QString &unit, bool slider, double divisor = 1.0) -> QWidget * {
        auto *wrap = new QWidget;
        wrap->setFixedWidth(slider ? 188 : 154);
        auto *layout = new QHBoxLayout(wrap);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName(QStringLiteral("TextSettingsSpin"));
        spin->setProperty("settingKey", setting);
        spin->setRange(minimum / divisor, maximum / divisor);
        spin->setDecimals(step / divisor < 1 ? 1 : 0);
        spin->setSingleStep(step / divisor);
        spin->setKeyboardTracking(false);
        spin->setFixedHeight(36);
        QSlider *bar = nullptr;
        if (slider) {
            bar = new QSlider(Qt::Horizontal);
            bar->setRange(qRound(minimum), qRound(maximum));
            bar->setProperty("settingKey", setting);
            spin->setFixedWidth(54);
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            layout->addWidget(bar, 1);
        }
        layout->addWidget(spin, slider ? 0 : 1);
        if (!unit.isEmpty()) { auto *suffix = label(unit, 12, true); suffix->setFixedWidth(18); layout->addWidget(suffix); }
        const int binding = bind(setting, [spin, bar, divisor](const QVariant &v) {
            const QSignalBlocker blocker(spin); spin->setValue(v.toDouble() / divisor);
            if (bar) { const QSignalBlocker block(bar); bar->setValue(qRound(v.toDouble())); }
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, setting, binding, bar, divisor](double v) {
            if (bar) { const QSignalBlocker block(bar); bar->setValue(qRound(v)); }
            setPending(setting, v * divisor, binding);
        });
        if (bar) connect(bar, &QSlider::valueChanged, spin, &QDoubleSpinBox::setValue);
        return wrap;
    };
    const auto switchFor = [this](const QString &setting) {
        auto *sw = new ToggleSwitch;
        sw->setProperty("settingKey", setting);
        const int binding = bind(setting, [sw](const QVariant &v) { const QSignalBlocker block(sw); sw->setChecked(v.toBool()); });
        connect(sw, &ToggleSwitch::toggled, this, [this, setting, binding](bool on) { setPending(setting, on, binding); });
        return sw;
    };
    const auto choice = [this](const QString &setting, const QList<QPair<QString, QVariant>> &options) {
        auto *box = combo(setting, options, 0);
        box->setFixedWidth(184);
        box->setProperty("settingKey", setting);
        return box;
    };

    auto *columns = new DisplayColumns;
    root->addWidget(columns);
    QWidget *basic;
    auto *basicLayout = card(basic, tr("Основные параметры"), bible ? QString() : tr("Настройки отображения текста песен на экране"));
    columns->columns->addWidget(basic, 4);
    QList<QPair<QString, QVariant>> fonts;
    for (const QString &family : QFontDatabase::families()) fonts.append({family, family});
    basicLayout->addWidget(row(tr("Шрифт"), choice(key("fontFamily"), fonts)));
    basicLayout->addWidget(row(tr("Размер текста"), number(key("fontSize"), 16, 200, 1, QStringLiteral("px"), bible)));
    basicLayout->addWidget(row(tr("Начертание"), choice(key("fontWeight"), {{tr("Обычный"), 400}, {tr("Средний"), 500}, {tr("Полужирный"), 600}, {tr("Жирный"), 700}})));
    basicLayout->addWidget(row(tr("Межбуквенный интервал"), number(key("letterSpacing"), -5, 30, 1, bible ? QStringLiteral("%") : QStringLiteral("px"), bible)));
    // Both pages persist line height as percent; the song stepper presents a ratio.
    auto *lineHeight = number(key("lineHeight"), 80, 250, bible ? 1 : 10, bible ? QStringLiteral("%") : QString(), bible, bible ? 1 : 100);
    basicLayout->addWidget(row(tr("Межстрочный интервал"), lineHeight));
    basicLayout->addWidget(row(tr("Регистр текста"), choice(key("textCase"), {{tr("Как в оригинале"), QStringLiteral("original")}, {tr("ПРОПИСНЫЕ"), QStringLiteral("upper")}, {tr("строчные"), QStringLiteral("lower")}})));
    auto *alignment = new QFrame;
    alignment->setObjectName(QStringLiteral("TextAlignment"));
    alignment->setFixedSize(184, 38);
    auto *alignmentLayout = new QHBoxLayout(alignment);
    alignmentLayout->setContentsMargins(2, 2, 2, 2); alignmentLayout->setSpacing(2);
    auto *group = new QButtonGroup(alignment);
    for (const auto &name : {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}) {
        auto *button = new QPushButton;
        button->setObjectName(QStringLiteral("TextAlignmentButton"));
        button->setCheckable(true); button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(32);
        button->setProperty("settingKey", key("alignment"));
        button->setProperty("settingValue", name);
        button->setToolTip(name == QLatin1String("left") ? tr("По левому краю") : name == QLatin1String("right") ? tr("По правому краю") : tr("По центру"));
        button->setAccessibleName(button->toolTip());
        const auto recolor = [button, name](bool selected) { button->setIcon(IconProvider::icon(QStringLiteral("align-") + name, QColor(selected ? Theme::TextLightPrimary : Theme::TextDarkPrimary), 17)); };
        recolor(false); connect(button, &QPushButton::toggled, button, recolor);
        group->addButton(button); alignmentLayout->addWidget(button, 1);
        bind(key("alignment"), [button, name](const QVariant &v) { button->setChecked(v.toString() == name); });
        connect(button, &QPushButton::clicked, this, [this, setting = key("alignment"), name]() { setPending(setting, name); });
    }
    basicLayout->addWidget(row(tr("Выравнивание"), alignment));
    basicLayout->addWidget(row(tr("Макс. строк на слайде"), number(key("maxLines"), 1, 20, 1, {}, bible)));
    basicLayout->addWidget(row(tr("Поля от краёв экрана"), number(key("margin"), 0, 25, 1, QStringLiteral("%"), bible)));
    basicLayout->addWidget(row(tr("Максимальная ширина блока"), number(key("maxWidth"), 30, 100, 1, QStringLiteral("%"), bible)));
    basicLayout->addStretch();

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0); rightLayout->setSpacing(14);
    columns->columns->addWidget(right, 6);
    QWidget *previewCard;
    auto *previewLayout = card(previewCard, {});
    previewLayout->setContentsMargins(16, 14, 16, 12);
    previewLayout->setSpacing(8);
    auto *previewHeader = new QHBoxLayout;
    previewHeader->addWidget(label(tr("Предпросмотр 16:9"), 16, false, true), 1);
    auto *background = choice(key("background"), {{tr("Фон: без изображения"), QString()},
        {tr("Фон: изображение"), bible ? QStringLiteral("builtin:mountains") : QStringLiteral("builtin:worship")},
        {tr("Горы"), QStringLiteral("builtin:mountains")}, {tr("Зал церкви"), QStringLiteral("builtin:church_hall")},
        {tr("Выбрать изображение…"), QStringLiteral("__choose__")}});
    background->setFixedWidth(188);
    connect(background, &QComboBox::activated, this, [this, background, setting = key("background")] {
        if (background->currentData().toString() != QLatin1String("__choose__")) return;
        const QString file = QFileDialog::getOpenFileName(this, tr("Фон слайда"), QString(), tr("Изображения (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (!file.isEmpty()) setPending(setting, file);
        else { const QSignalBlocker block(background); background->setCurrentIndex(background->findData(current(setting).toString())); }
    });
    previewHeader->addWidget(background);
    previewLayout->addLayout(previewHeader);
    if (!bible) previewLayout->addWidget(label(tr("Как будут выглядеть песни на экране"), 12, true));
    auto *preview = new StylePreview;
    preview->setObjectName(QStringLiteral("TextStylePreview"));
    previewLayout->addWidget(preview, 1);
    rightLayout->addWidget(previewCard);

    QWidget *extra;
    auto *extraLayout = card(extra, tr("Дополнительные параметры"), bible ? QString() : tr("Помогают тексту хорошо помещаться на экране и быть читаемым"));
    extraLayout->setContentsMargins(18, 14, 18, 14);
    extraLayout->setSpacing(8);
    auto *extraGrid = new QGridLayout;
    extraGrid->setContentsMargins(0, 0, 0, 0); extraGrid->setHorizontalSpacing(18); extraGrid->setVerticalSpacing(5);
    extraLayout->addLayout(extraGrid);
    const auto addToggle = [&](int index, const QString &title, const QString &setting, const QString &hint = {}) {
        extraGrid->addWidget(row(title, switchFor(setting), hint, 28), index, 0);
    };
    addToggle(0, tr("Автоподбор размера текста"), key("autoFit"), tr("Автоматически уменьшать текст, если он не помещается"));
    if (bible) {
        addToggle(1, tr("Автоматически разбивать длинные стихи"), key("splitLong"), tr("Разделять длинные стихи на несколько слайдов"));
        addToggle(2, tr("Номер стиха отдельно"), key("separateVerseNumber"), tr("Показывать номер стиха в отдельной строке"));
        addToggle(3, tr("Показывать ссылку (книгу и главу)"), AppSettings::BibleShowReference);
    } else {
        addToggle(1, tr("Автоматически переносить длинные строки"), key("wordWrap"));
        addToggle(2, tr("Разбивать длинные куплеты на несколько слайдов"), key("splitLong"));
        addToggle(3, tr("Показывать припев отдельно"), key("separateChorus"));
    }
    const QStringList effects{tr("Тень текста"), tr("Контур текста"), tr("Затемнение фона")};
    const QStringList captions{tr("Сила тени"), tr("Толщина контура"), tr("Прозрачность")};
    const char *effectFields[] = {"shadow", "outline", "dim"};
    const char *amountFields[] = {"shadowOpacity", "outlineWidth", "dimOpacity"};
    for (int i = 0; i < 3; ++i) {
        const QString setting = key(effectFields[i]);
        auto *effect = new QWidget;
        auto *effectLayout = new QVBoxLayout(effect);
        effectLayout->setContentsMargins(0, 2, 0, 2); effectLayout->setSpacing(4);
        auto *sw = switchFor(setting);
        auto *amount = number(key(amountFields[i]), i == 1 ? 1 : 0, i == 1 ? 10 : 100, 1, i == 1 ? QStringLiteral("px") : QStringLiteral("%"), true);
        amount->setFixedWidth(bible ? 146 : 188);
        auto *amountRow = row(captions[i], amount, {}, 32);
        bind(setting, [amountRow](const QVariant &v) { amountRow->setEnabled(v.toBool()); });
        if (bible) {
            effectLayout->addWidget(row(effects[i], sw, {}, 26));
            effectLayout->addWidget(amountRow);
            extraGrid->addWidget(effect, i, 1);
        } else {
            auto *horizontal = new QHBoxLayout;
            horizontal->setSpacing(18);
            horizontal->addWidget(row(effects[i], sw, {}, 32), 1);
            horizontal->addWidget(amountRow, 2);
            effectLayout->addLayout(horizontal);
            extraGrid->addWidget(effect, i + 4, 0);
        }
    }
    if (bible) { extraGrid->setColumnStretch(0, 1); extraGrid->setColumnStretch(1, 1); }
    rightLayout->addWidget(extra);
    rightLayout->addStretch();

    // Preserve library/import preferences beneath the new presentation editor.
    auto *libraryButton = new QPushButton(bible ? tr("Переводы, формат ссылок и история") : tr("Сборники, аккорды и импорт"));
    libraryButton->setObjectName(QStringLiteral("TextSettingsLibraryButton"));
    libraryButton->setCheckable(true); libraryButton->setCursor(Qt::PointingHandCursor);
    libraryButton->setIcon(IconProvider::icon(QStringLiteral("chevron-down"), QColor(Theme::TextDarkSecondary), 14));
    root->addWidget(libraryButton);
    auto *library = bible ? buildBibleLibraryPage() : buildSongsLibraryPage();
    library->hide(); root->addWidget(library);
    connect(libraryButton, &QPushButton::toggled, library, &QWidget::setVisible);
    root->addStretch();
    const auto refresh = [this, type, preview, key] {
        preview->refresh(DisplaySettings::textStyle(type, [this](const QString &setting) { return current(setting); }),
            current(key("background")).toString(), current(AppSettings::BibleShowReference).toBool(),
            current(key("separateVerseNumber")).toBool(), current(key("splitLong")).toBool());
    };
    m_previewRefreshers << refresh;
    refresh();
    page->setStyleSheet(QStringLiteral(R"(
        QFrame#TextSettingsCard { background:%1; border:1px solid %2; border-radius:12px; }
        QFrame#TextSettingsBanner { background:%5; border:1px solid %5; border-radius:12px; }
        QDoubleSpinBox#TextSettingsSpin { background:%1; color:%3; border:1px solid %2; border-radius:7px; padding:0 6px; font-size:13px; }
        QDoubleSpinBox#TextSettingsSpin:focus { border-color:%4; }
        QDoubleSpinBox#TextSettingsSpin:disabled { color:%6; background:%7; }
        QDoubleSpinBox::up-button { width:18px; border:none; subcontrol-position:top right; }
        QDoubleSpinBox::down-button { width:18px; border:none; subcontrol-position:bottom right; }
        QDoubleSpinBox::up-arrow { image:url(:/icons/chevron-up.svg); width:10px; height:10px; }
        QDoubleSpinBox::down-arrow { image:url(:/icons/chevron-down.svg); width:10px; height:10px; }
        QSlider::groove:horizontal { height:4px; background:%2; border-radius:2px; }
        QSlider::sub-page:horizontal { background:%4; border-radius:2px; }
        QSlider::handle:horizontal { width:12px; margin:-4px 0; background:%4; border-radius:6px; }
        QSlider::sub-page:horizontal:disabled, QSlider::handle:horizontal:disabled { background:#c4c9d3; }
        QFrame#TextAlignment { background:%1; border:1px solid %2; border-radius:8px; }
        QPushButton#TextAlignmentButton { border:none; background:transparent; border-radius:6px; }
        QPushButton#TextAlignmentButton:checked { background:%4; }
        QPushButton#TextSettingsLibraryButton { border:none; background:transparent; text-align:left; color:%6; font-size:12px; padding:4px 0; }
    )").arg(Theme::BgWhite, Theme::BorderLight, Theme::TextDarkPrimary, Theme::AccentBlue, Theme::AccentBlueBg, Theme::TextDarkSecondary, Theme::SurfaceSubtle));
    return page;
}
