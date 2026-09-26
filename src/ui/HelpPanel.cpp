#include "HelpPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr char SupportEmail[] = "sermon.support@gmail.com";
}

namespace {

QFrame *makeCategoryCard(const QString &iconName, const QString &title, const QString &desc)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("HelpCard"));
    card->setCursor(Qt::PointingHandCursor);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto *iconBox = new QLabel;
    iconBox->setFixedSize(38, 38);
    iconBox->setAlignment(Qt::AlignCenter);
    iconBox->setStyleSheet(QStringLiteral("background: %1; border-radius: 10px;").arg(Theme::AccentBlueBg));
    iconBox->setPixmap(IconProvider::pixmap(iconName, QColor(Theme::AccentBlue), 18));
    layout->addWidget(iconBox);

    auto *titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    layout->addWidget(titleLabel);

    auto *descLabel = new QLabel(desc);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;").arg(Theme::TextDarkSecondary));
    layout->addWidget(descLabel);

    return card;
}

class FaqRow : public QWidget {
    Q_OBJECT
public:
    FaqRow(const QString &question, const QString &answer, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_button = new QPushButton(question);
        m_button->setObjectName(QStringLiteral("FaqQuestion"));
        m_button->setCursor(Qt::PointingHandCursor);
        m_button->setIcon(IconProvider::icon(QStringLiteral("chevron-down"), QColor(Theme::TextDarkSecondary), 14));
        m_button->setLayoutDirection(Qt::RightToLeft);
        layout->addWidget(m_button);

        m_answer = new QLabel(answer);
        m_answer->setWordWrap(true);
        m_answer->setStyleSheet(QStringLiteral("font-size: 12.5px; color: %1; padding: 0 4px 14px 4px;").arg(Theme::TextDarkSecondary));
        m_answer->setVisible(false);
        layout->addWidget(m_answer);

        connect(m_button, &QPushButton::clicked, this, [this]() { m_answer->setVisible(!m_answer->isVisible()); });
    }

private:
    QPushButton *m_button;
    QLabel *m_answer;
};

} // namespace

HelpPanel::HelpPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("HelpPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
}

void HelpPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(48, 32, 48, 32);
    root->setSpacing(18);

    auto *title = new QLabel(tr("Справка"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    root->addWidget(title);
    auto *subtitle = new QLabel(tr("Инструкции, ответы на частые вопросы и поддержка"));
    subtitle->setStyleSheet(QStringLiteral("font-size: 13.5px; color: %1;").arg(Theme::TextDarkSecondary));
    root->addWidget(subtitle);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // QScrollArea's viewport paints its own palette background (light gray
    // on Windows) unless told otherwise. Scoped to the viewport: a bare rule
    // cascaded into the cards and labels and painted over them.
    scroll->viewport()->setStyleSheet(QStringLiteral("QWidget#qt_scrollarea_viewport { background: #ffffff; }"));
    root->addWidget(scroll, 1);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("HelpContent"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral("QWidget#HelpContent { background: #ffffff; }"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 4, 4);
    layout->setSpacing(18);
    scroll->setWidget(content);

    auto *searchBox = new QFrame;
    searchBox->setObjectName(QStringLiteral("SearchBox"));
    auto *searchLayout = new QHBoxLayout(searchBox);
    searchLayout->setContentsMargins(16, 12, 16, 12);
    searchLayout->setSpacing(10);
    auto *searchIcon = new QLabel;
    searchIcon->setPixmap(IconProvider::pixmap(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16));
    searchLayout->addWidget(searchIcon);
    auto *searchEdit = new QLineEdit;
    searchEdit->setObjectName(QStringLiteral("SearchField")); // Ctrl+F (Горячие клавиши → Поиск)
    searchEdit->setPlaceholderText(tr("Поиск по справке (например: OBS, горячие клавиши)"));
    searchEdit->setFrame(false);
    searchLayout->addWidget(searchEdit, 1);
    layout->addWidget(searchBox);

    auto *cardsRow = new QHBoxLayout;
    cardsRow->setSpacing(16);
    struct Category { const char *icon; const char *title; const char *desc; };
    const Category categories[] = {
        {"rocket", "Начало работы", "Первые шаги в Sermon"},
        {"book-open", "Песни и Библия", "Добавление и редактирование"},
        {"monitor", "Показ и OBS", "Настройка трансляции"},
        {"keyboard", "Горячие клавиши", "Управление с клавиатуры"},
    };
    for (const Category &category : categories) {
        auto *card = makeCategoryCard(QString::fromUtf8(category.icon), tr(category.title), tr(category.desc));
        cardsRow->addWidget(card);
    }
    layout->addLayout(cardsRow);

    auto *faqHeading = new QLabel(tr("Частые вопросы"));
    faqHeading->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    layout->addWidget(faqHeading);

    auto *faqCard = new QFrame;
    faqCard->setObjectName(QStringLiteral("HelpCard"));
    auto *faqLayout = new QVBoxLayout(faqCard);
    faqLayout->setSpacing(0);

    const QList<QPair<QString, QString>> faq = {
        {tr("Как подключить вывод к OBS?"),
         tr("Откройте Настройки → OBS и сеть, скопируйте веб-адрес и добавьте его как источник «Браузер» "
            "(Browser Source) в сцену OBS на втором устройстве. Оба устройства должны быть в одной локальной сети.")},
        {tr("Как добавить новую песню или сборник?"),
         tr("Нажмите «Добавить» в сайдбаре, выберите категорию «Песня» и вставьте текст — он автоматически "
            "разобьётся на слайды по пустым строкам между куплетами.")},
        {tr("Как настроить горячие клавиши?"),
         tr("Откройте Настройки → Горячие клавиши, щёлкните поле нужного действия и нажмите новую клавишу, затем «Сохранить». По умолчанию: ← / → — слайды (пульт-кликер: PageDown / PageUp), Пробел — пауза, F5 — на экран, Esc — завершить показ.")},
        {tr("Как создать резервную копию?"),
         tr("Настройки → Резервная копия → «Создать сейчас». Там же можно включить автобэкап (ежедневно или еженедельно), выбрать папку для копий и восстановить библиотеку из копии.")},
        {tr("Как работает поиск по Библии?"),
         tr("В разделе «Библия» выберите книгу, главу и стихи на вкладке «По тексту» или найдите слово на вкладке «По поиску». Кнопка с часами рядом со ссылкой открывает недавние места.")},
    };
    for (const auto &[question, answer] : faq)
        faqLayout->addWidget(new FaqRow(question, answer));
    layout->addWidget(faqCard);

    auto *supportCard = new QFrame;
    supportCard->setObjectName(QStringLiteral("SupportCard"));
    // design.pen "Support Card": fill $accent-blue-bg, radius 12. Styled on
    // the widget itself — the panel-wide rule never reached it.
    supportCard->setAttribute(Qt::WA_StyledBackground, true);
    supportCard->setStyleSheet(QStringLiteral("QFrame#SupportCard { background: %1; border-radius: 12px; }").arg(Theme::AccentBlueBg));
    auto *supportLayout = new QHBoxLayout(supportCard);
    supportLayout->setContentsMargins(22, 22, 22, 22);
    supportLayout->setSpacing(16);
    auto *supportTextCol = new QVBoxLayout;
    supportTextCol->setSpacing(4);
    auto *supportTitle = new QLabel(tr("Не нашли ответ?"));
    supportTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    auto *supportDesc = new QLabel(tr("Напишите нам на %1 — поможем в течение 24 часов").arg(QString::fromLatin1(SupportEmail)));
    supportDesc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    supportDesc->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(Theme::TextDarkSecondary));
    supportTextCol->addWidget(supportTitle);
    supportTextCol->addWidget(supportDesc);
    supportLayout->addLayout(supportTextCol, 1);

    auto *supportButton = new QPushButton(tr("  Написать в поддержку"));
    supportButton->setObjectName(QStringLiteral("SupportButton"));
    supportButton->setStyleSheet(QStringLiteral("QPushButton#SupportButton { background: %1; border: none; border-radius: 9px; "
                                                "padding: 11px 18px; font-weight: 600; font-size: 13.5px; color: #ffffff; }")
                                     .arg(Theme::AccentBlue));
    supportButton->setCursor(Qt::PointingHandCursor);
    supportButton->setIcon(IconProvider::icon(QStringLiteral("mail"), QColor(Theme::TextLightPrimary), 15));
    supportButton->setToolTip(QString::fromLatin1(SupportEmail));
    connect(supportButton, &QPushButton::clicked, this, [this]() {
        // Opens the mail program with the address filled in; without one,
        // the address goes to the clipboard so it can be pasted anywhere.
        const QString email = QString::fromLatin1(SupportEmail);
        const QUrl mail(QStringLiteral("mailto:%1?subject=%2").arg(email, QString::fromUtf8(QUrl::toPercentEncoding(tr("Sermon: вопрос")))));
        if (!QDesktopServices::openUrl(mail)) {
            QGuiApplication::clipboard()->setText(email);
            QMessageBox::information(this, tr("Поддержка"),
                tr("Почта поддержки: %1\nАдрес скопирован — вставьте его в своём почтовом сервисе.").arg(email));
        }
    });
    supportLayout->addWidget(supportButton);
    layout->addWidget(supportCard);
    // Without this the blocks shared out the spare height: a huge search box,
    // the FAQ pushed down and the support card squashed.
    layout->addStretch();

    setStyleSheet(QStringLiteral(R"(
        QWidget#HelpPanel { background: #ffffff; }
        QFrame#SearchBox { background: #ffffff; border: 1px solid %2; border-radius: 10px; }
        QLineEdit { border: none; background: transparent; font-size: 14px; color: %1; }
        QFrame#HelpCard { background: %3; border: 1px solid %2; border-radius: 12px; }
        QPushButton#FaqQuestion {
            text-align: left; border: none; background: transparent; padding: 16px 18px;
            font-size: 13.5px; font-weight: 600; color: %1;
        }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::BgPanel));
}

#include "HelpPanel.moc"
