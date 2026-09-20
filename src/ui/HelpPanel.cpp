#include "HelpPanel.h"
#include "IconProvider.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QFrame *makeCategoryCard(const QString &iconName, const QString &title, const QString &desc)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("HelpCard"));
    card->setCursor(Qt::PointingHandCursor);
    auto *layout = new QVBoxLayout(card);
    layout->setSpacing(8);

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
    root->setContentsMargins(28, 22, 28, 22);
    root->setSpacing(4);

    auto *title = new QLabel(tr("Справка"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: %1;").arg(Theme::TextDarkPrimary));
    root->addWidget(title);
    auto *subtitle = new QLabel(tr("Инструкции, ответы на частые вопросы и поддержка"));
    subtitle->setStyleSheet(QStringLiteral("font-size: 13.5px; color: %1;").arg(Theme::TextDarkSecondary));
    root->addWidget(subtitle);
    root->addSpacing(18);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll, 1);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 4, 4);
    layout->setSpacing(20);
    scroll->setWidget(content);

    auto *searchBox = new QFrame;
    searchBox->setObjectName(QStringLiteral("SearchBox"));
    auto *searchLayout = new QHBoxLayout(searchBox);
    searchLayout->setContentsMargins(12, 0, 12, 0);
    searchLayout->setSpacing(8);
    auto *searchIcon = new QLabel;
    searchIcon->setPixmap(IconProvider::pixmap(QStringLiteral("search"), QColor(Theme::TextDarkSecondary), 16));
    searchLayout->addWidget(searchIcon);
    auto *searchEdit = new QLineEdit;
    searchEdit->setPlaceholderText(tr("Поиск по справке (например: OBS, горячие клавиши)"));
    searchEdit->setFrame(false);
    searchLayout->addWidget(searchEdit, 1);
    layout->addWidget(searchBox);

    auto *cardsRow = new QHBoxLayout;
    cardsRow->setSpacing(16);
    struct Category { const char *icon; const char *title; const char *desc; };
    const Category categories[] = {
        {"rocket", "Начало работы", "Первые шаги в Gather"},
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
    faqLayout->setSpacing(4);

    const QList<QPair<QString, QString>> faq = {
        {tr("Как подключить вывод к OBS?"),
         tr("Откройте Настройки → OBS и сеть, скопируйте веб-адрес и добавьте его как источник «Браузер» "
            "(Browser Source) в сцену OBS на втором устройстве. Оба устройства должны быть в одной локальной сети.")},
        {tr("Как добавить новую песню или сборник?"),
         tr("Нажмите «Добавить» в сайдбаре, выберите категорию «Песня» и вставьте текст — он автоматически "
            "разобьётся на слайды по пустым строкам между куплетами.")},
        {tr("Как настроить горячие клавиши?"),
         tr("Сейчас в показе работают ← / → (слайды), B (чёрный экран) и Пробел (пауза) — список см. в "
            "Настройки → Горячие клавиши. Их переназначение появится в следующих версиях.")},
        {tr("Как создать резервную копию?"),
         tr("В сайдбаре откройте «Импорт / Экспорт» и нажмите «Экспортировать» — Gather сохранит базу данных "
            "и все фото в отдельную папку, которую можно перенести на другой компьютер.")},
        {tr("Как работает поиск по Библии?"),
         tr("В разделе «Стих из Библии» выберите книгу, главу и диапазон стихов на вкладке «По ссылке», либо "
            "используйте вкладку «По тексту» для поиска по содержимому всей загруженной Библии.")},
    };
    for (const auto &[question, answer] : faq)
        faqLayout->addWidget(new FaqRow(question, answer));
    layout->addWidget(faqCard);

    auto *supportCard = new QFrame;
    supportCard->setObjectName(QStringLiteral("SupportCard"));
    auto *supportLayout = new QHBoxLayout(supportCard);
    auto *supportTextCol = new QVBoxLayout;
    supportTextCol->setSpacing(4);
    auto *supportTitle = new QLabel(tr("Не нашли ответ?"));
    supportTitle->setStyleSheet(QStringLiteral("font-size: 14.5px; font-weight: 700; color: %1;").arg(Theme::TextLightPrimary));
    auto *supportDesc = new QLabel(tr("Напишите нам, и мы поможем в течение 24 часов"));
    supportDesc->setStyleSheet(QStringLiteral("font-size: 12.5px; color: %1;").arg(Theme::TextLightSecondary));
    supportTextCol->addWidget(supportTitle);
    supportTextCol->addWidget(supportDesc);
    supportLayout->addLayout(supportTextCol, 1);

    auto *supportButton = new QPushButton(tr("  Написать в поддержку"));
    supportButton->setObjectName(QStringLiteral("SupportButton"));
    supportButton->setCursor(Qt::PointingHandCursor);
    supportButton->setIcon(IconProvider::icon(QStringLiteral("mail"), QColor(Theme::TextDarkPrimary), 15));
    connect(supportButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("Поддержка"),
            tr("Контакты поддержки появятся здесь позже. Пока опишите проблему автору проекта напрямую."));
    });
    supportLayout->addWidget(supportButton);
    layout->addWidget(supportCard);

    setStyleSheet(QStringLiteral(R"(
        QWidget#HelpPanel { background: #ffffff; }
        QFrame#SearchBox { background: #ffffff; border: 1px solid %2; border-radius: 9px; min-height: 40px; }
        QLineEdit { border: none; background: transparent; font-size: 13px; color: %1; }
        QFrame#HelpCard { background: %3; border: 1px solid %2; border-radius: 12px; }
        QPushButton#FaqQuestion {
            text-align: left; border: none; background: transparent; padding: 12px 4px;
            font-size: 13.5px; font-weight: 600; color: %1;
        }
        QFrame#SupportCard { background: %4; border-radius: 14px; }
        QPushButton#SupportButton {
            background: #ffffff; border: none; border-radius: 9px; padding: 10px 16px;
            font-weight: 600; font-size: 13.5px; color: %1;
        }
    )").arg(Theme::TextDarkPrimary, Theme::BorderLight, Theme::BgPanel, Theme::BgDark));
}

#include "HelpPanel.moc"
