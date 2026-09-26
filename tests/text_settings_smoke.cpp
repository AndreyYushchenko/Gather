#include "core/AppSettings.h"
#include "core/ContentItem.h"
#include "display/DisplayServer.h"
#include "display/SlideContentBuilder.h"
#include "display/TextSlideWidget.h"
#include "ui/DisplaySettings.h"
#include "ui/SettingsPanel.h"
#include "ui/Theme.h"
#include "ui/ToggleSwitch.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

static void check(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
    std::printf("PASS: %s\n", message);
}

template<class T> static T *control(QWidget &panel, const QString &key)
{
    for (T *widget : panel.findChildren<T *>())
        if (widget->property("settingKey").toString() == key) return widget;
    return nullptr;
}

static QPushButton *action(SettingsPanel &panel, const QString &text)
{
    for (auto *button : panel.findChildren<QPushButton *>())
        if (button->text().trimmed() == text) return button;
    return nullptr;
}

static int visiblePixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) > 10) ++count;
    return count;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir data;
    qputenv("SERMON_PROFILE", "v2-smoke");
    qputenv("SERMON_PROFILE_DIR", data.path().toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("SermonV2Smoke"));
    QCoreApplication::setApplicationName(QStringLiteral("SermonV2Smoke"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
    Theme::apply(app);
    app.setFont(QFont(Theme::fontFamily()));

    const auto song = [](const char *field) { return DisplaySettings::styleKey(ContentType::Song, QString::fromLatin1(field)); };
    const auto bible = [](const char *field) { return DisplaySettings::styleKey(ContentType::BibleVerse, QString::fromLatin1(field)); };
    SettingsPanel panel;
    SettingsPanel::Context context;
    context.dataDir = data.path();
    panel.setContext(context);
    panel.resize(1432, 940);
    panel.showPage(3);
    panel.show();
    app.processEvents();
    check(!panel.hasPendingChanges(), "opening V2 settings does not change preferences");
    auto *songSize = control<QDoubleSpinBox>(panel, song("fontSize"));
    auto *bibleSize = control<QDoubleSpinBox>(panel, bible("fontSize"));
    check(songSize && bibleSize, "both pages expose bound font size controls");
    auto *songPreview = panel.findChild<QWidget *>(QStringLiteral("SongSettingsV2"))->findChild<QWidget *>(QStringLiteral("TextStylePreview"));
    const QImage before = songPreview->grab().toImage();
    songSize->setValue(110);
    app.processEvents();
    check(panel.hasPendingChanges() && AppSettings::value(song("fontSize")).toInt() == 72, "editing is pending until Save");
    check(before != songPreview->grab().toImage(), "preview changes before Save");
    check(bibleSize->value() == 72, "song edits do not affect Bible settings");
    const QString announcementFont = DisplaySettings::fontFamily(ContentType::Announcement);
    action(panel, QStringLiteral("Отмена"))->click();
    check(songSize->value() == 72 && !panel.hasPendingChanges(), "Cancel restores controls and clean state");
    songSize->setValue(96);
    action(panel, QStringLiteral("Сохранить"))->click();
    check(AppSettings::value(song("fontSize")).toInt() == 96 && !panel.hasPendingChanges(), "Save persists the song profile");
    panel.showPage(2);
    bibleSize->setValue(88);
    action(panel, QStringLiteral("Сохранить"))->click();
    action(panel, QStringLiteral("Сбросить"))->click();
    check(bibleSize->value() == 72 && songSize->value() == 96 && panel.hasPendingChanges(), "Reset applies only to the current page and stays pending");
    action(panel, QStringLiteral("Отмена"))->click();
    check(bibleSize->value() == 88, "Cancel after Reset restores saved values");
    auto *outline = control<ToggleSwitch>(panel, bible("outline"));
    auto *outlineWidth = control<QDoubleSpinBox>(panel, bible("outlineWidth"));
    check(outline && outlineWidth && !outlineWidth->isEnabled(), "outline amount is disabled while effect is off");
    outline->click();
    check(outlineWidth->isEnabled(), "enabling outline enables its amount control");
    action(panel, QStringLiteral("Отмена"))->click();
    panel.showPage(3);
    auto *lineHeight = control<QDoubleSpinBox>(panel, song("lineHeight"));
    lineHeight->setValue(1.7);
    action(panel, QStringLiteral("Сохранить"))->click();
    check(AppSettings::value(song("lineHeight")).toInt() == 170, "song line height ratio saves as percent");
    AppSettings::setValue(song("fontFamily"), QStringLiteral("Georgia"));
    check(DisplaySettings::fontFamily(ContentType::Announcement) == announcementFont, "V2 song typography leaves announcement typography unchanged");
    panel.grab().save(QStringLiteral("settings-song-v2-panel.png"));
    panel.showPage(2); app.processEvents();
    panel.grab().save(QStringLiteral("settings-bible-v2-panel.png"));
    panel.resize(1032, 720); app.processEvents();
    panel.grab().save(QStringLiteral("settings-bible-v2-small.png"));

    ContentItem item;
    item.type = ContentType::Song;
    item.text = QStringLiteral("Куплет 1\none\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine");
    const QString source = item.text;
    const auto pages = item.presentationSlides();
    check(pages.size() == 3 && item.text == source && item.slides().size() == 1, "long stanzas paginate without changing source lyrics");
    AppSettings::setValue(song("textCase"), QStringLiteral("upper"));
    check(item.presentationSlides().join(QLatin1Char('\n')).contains(QStringLiteral("one")), "case conversion does not rewrite paginated source text");
    AppSettings::setValue(song("splitLong"), false);
    check(item.presentationSlides().size() == 1, "disabling splitting preserves a long stanza");
    item.text = QStringLiteral("Куплет 1\nverse\n\nПрипев\nchorus");
    AppSettings::setValue(song("separateChorus"), false);
    check(item.presentationSlides().size() == 1 && item.text.contains(QStringLiteral("\n\n")), "chorus can join a verse without changing saved stanza boundaries");
    ContentItem verse;
    verse.type = ContentType::BibleVerse;
    verse.refBook = QStringLiteral("Иоанна"); verse.refLocation = QStringLiteral("3:16");
    verse.text = QStringLiteral("¹⁶ Ибо так возлюбил Бог мир");
    AppSettings::setValue(bible("separateVerseNumber"), true);
    auto content = buildSlideContent(verse, verse.presentationSlides(), 0);
    check(content.reference == QStringLiteral("Иоанна 3:16") && content.text.startsWith(QStringLiteral("16\n")), "Bible reference and separate verse number reach the renderer");
    AppSettings::setValue(AppSettings::BibleShowReference, false);
    check(buildSlideContent(verse, verse.presentationSlides(), 0).reference.isEmpty(), "hiding the Bible reference affects projected content");

    TextSlideWidget text;
    text.resize(1920, 1080);
    text.setText(content.text, content.reference, false, DisplaySettings::textStyle(ContentType::BibleVerse));
    QImage rendered(text.size(), QImage::Format_ARGB32_Premultiplied); rendered.fill(Qt::transparent); text.render(&rendered);
    check(visiblePixels(rendered) > 1000, "text renderer produces visible pixels offscreen");
    rendered.save(QStringLiteral("v2-text-render.png"));
    DisplayServer server;
    check(server.start(0), "OBS test server starts on an ephemeral port");
    server.setContent(content);
    QNetworkAccessManager network;
    const auto fetch = [&](const QString &path) {
        auto *reply = network.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.port()).arg(path))));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        check(reply->isFinished() && reply->error() == QNetworkReply::NoError, "OBS endpoint responds successfully");
        const QByteArray bytes = reply->readAll(); reply->deleteLater(); return bytes;
    };
    check(fetch(QStringLiteral("/display")).contains("/text-layer"), "OBS page uses shared text renderer");
    const QImage obs = QImage::fromData(fetch(QStringLiteral("/text-layer")), "PNG");
    check(obs.size() == QSize(1920, 1080) && visiblePixels(obs) > 1000 && qAlpha(obs.pixel(0, 0)) == 0,
        "OBS text layer is a visible transparent 1080p image");
    check(obs.convertToFormat(QImage::Format_ARGB32) == rendered.convertToFormat(QImage::Format_ARGB32), "OBS and native renderer produce identical text pixels");
    std::puts("All V2 settings checks passed.");
    return 0;
}
