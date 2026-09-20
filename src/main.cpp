#include "core/Database.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Gather"));
    QCoreApplication::setApplicationName(QStringLiteral("Gather"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-256.png")));
    app.setFont(QFont(Theme::fontFamily()));

    if (!Database::open()) {
        QMessageBox::critical(nullptr, QObject::tr("Ошибка"),
                               QObject::tr("Не удалось открыть базу данных приложения."));
        return 1;
    }

    MainWindow window;
    window.resize(1440, 800);
    window.show();

    return app.exec();
}
