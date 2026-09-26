#include "core/AppSettings.h"
#include "core/Database.h"
#include "core/OldNameMigration.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QLocale>
#include <QTimer>
#include <QTranslator>
#include <QSettings>

int main(int argc, char *argv[])
{
    // Developer switch: SERMON_PROFILE=<name> keeps settings and data apart
    // from the real library (for trying things out / automated snapshots).
    const QString profile = qEnvironmentVariable("SERMON_PROFILE");
    QCoreApplication::setOrganizationName(profile.isEmpty() ? QStringLiteral("Sermon") : QStringLiteral("Sermon-") + profile);
    QCoreApplication::setApplicationName(QStringLiteral("Sermon"));
    const QString profileDir = qEnvironmentVariable("SERMON_PROFILE_DIR");
    if (!profile.isEmpty() && !profileDir.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profileDir);
    }
    // The app used to be called Gather: take its settings over once.
    if (profile.isEmpty())
        OldNameMigration::migrateSettings();

    // "Масштаб шрифта интерфейса" × "Плотность интерфейса" (compact = 90%):
    // Qt's scale factor has to be set before the application object exists.
    double scale = qBound(50, AppSettings::value(AppSettings::UiScale).toInt(), 200) / 100.0;
    if (AppSettings::value(AppSettings::Density).toString() == QLatin1String("compact"))
        scale *= 0.9;
    if (!qFuzzyCompare(scale, 1.0))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));

    QApplication app(argc, argv);
    if (profile.isEmpty())
        OldNameMigration::migrateData();

    // "Язык интерфейса": the source strings are Russian; Ukrainian and
    // English come from the embedded translations. Dates follow along.
    const QString language = AppSettings::value(AppSettings::Language).toString();
    const QLocale locale(language == QLatin1String("uk") ? QLocale::Ukrainian
                         : language == QLatin1String("en") ? QLocale::English : QLocale::Russian);
    QLocale::setDefault(locale);
    auto *qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(QStringLiteral(":/i18n/qtbase_%1.qm").arg(language == QLatin1String("uk") || language == QLatin1String("en") ? language : QStringLiteral("ru"))))
        app.installTranslator(qtTranslator);
    if (language == QLatin1String("uk") || language == QLatin1String("en")) {
        auto *translator = new QTranslator(&app);
        if (translator->load(QStringLiteral(":/i18n/sermon_%1.qm").arg(language)))
            app.installTranslator(translator);
    }
    // Colour scheme and corners (Настройки → Внешний вид) before any widget.
    Theme::apply(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-256.png")));
    app.setFont(QFont(Theme::fontFamily()));
    app.setStyleSheet(Theme::scrollBarCss());

    if (!Database::open()) {
        QMessageBox::critical(nullptr, QObject::tr("Ошибка"),
                               QObject::tr("Не удалось открыть базу данных приложения."));
        return 1;
    }

    MainWindow window;
    window.show();

    // Developer switch: SERMON_SNAPSHOT=<png> [SERMON_SNAPSHOT_OPEN=<section>|settings:<page>]
    // [SERMON_SNAPSHOT_SIZE=WxH] renders the window to a file and quits.
    const QString snapshot = qEnvironmentVariable("SERMON_SNAPSHOT");
    if (!snapshot.isEmpty()) {
        const QStringList size = qEnvironmentVariable("SERMON_SNAPSHOT_SIZE", QStringLiteral("1536x793")).split(QLatin1Char('x'));
        window.resize(size.value(0).toInt(), size.value(1).toInt());
        window.openForSnapshot(qEnvironmentVariable("SERMON_SNAPSHOT_OPEN"));
        // SERMON_SNAPSHOT_CLICK=<objectName>: press that (visible) button first.
        const QString click = qEnvironmentVariable("SERMON_SNAPSHOT_CLICK");
        if (!click.isEmpty()) {
            QTimer::singleShot(800, &window, [&window, click]() {
                for (QAbstractButton *button : window.findChildren<QAbstractButton *>(click)) {
                    if (button->isVisible()) {
                        button->click();
                        break;
                    }
                }
            });
        }
        // SERMON_SNAPSHOT_SCROLL=bottom: scroll every visible scroll area down.
        if (qEnvironmentVariable("SERMON_SNAPSHOT_SCROLL") == QLatin1String("bottom")) {
            QTimer::singleShot(1600, &window, [&window]() {
                for (QScrollArea *area : window.findChildren<QScrollArea *>()) {
                    if (area->isVisible())
                        area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
                }
            });
        }
        QTimer::singleShot(2500, &window, [&window, snapshot]() {
            window.grab().save(snapshot);
            QCoreApplication::exit(0);
        });
    }

    return app.exec();
}
