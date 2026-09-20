#include "Theme.h"

#include <QFontDatabase>
#include <QStringList>

QString Theme::fontFamily()
{
    static const QString resolved = [] {
        const QStringList families = QFontDatabase::families();
        if (families.contains(QStringLiteral("Inter")))
            return QStringLiteral("Inter");
#ifdef Q_OS_WIN
        return QStringLiteral("Segoe UI");
#else
        return QStringLiteral("Sans Serif");
#endif
    }();
    return resolved;
}
