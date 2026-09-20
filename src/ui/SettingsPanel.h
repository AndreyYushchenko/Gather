#pragma once

#include "core/ContentItem.h"

#include <QMap>
#include <QWidget>

class QComboBox;
class QLabel;
class QStackedWidget;
class QVBoxLayout;

// "Settings" screen: a left section nav + a card on the right, one page per
// design frame (Общие/Показ/Библия/Песни и сборники/База данных/Горячие
// клавиши/OBS и сеть/Резервная копия/Внешний вид). Rows mirror the design;
// a handful are wired to real app state (data folder, DB size, OBS URL/IP,
// bible translation, library counts) where the app already tracks it.
class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget *parent = nullptr);

    void setInfo(const QString &dataDir, const QString &obsUrl,
                 const QString &bibleTranslation, int bibleBookCount, int bibleVerseCount,
                 const QMap<ContentType, int> &counts);

signals:
    void importExportRequested();
    void songImportRequested();
    void displaySettingsChanged();

private:
    void buildUi();
    // Adds the font-family + text-scale rows for the given profile
    // (ContentType::BibleVerse or ::Song) to a subsection's row layout.
    void addFontSizeRows(QVBoxLayout *rows, ContentType type);
    QWidget *buildGeneralPage();
    QWidget *buildShowPage();
    QWidget *buildBiblePage();
    QWidget *buildSongsPage();
    QWidget *buildDatabasePage();
    QWidget *buildHotkeysPage();
    QWidget *buildObsPage();
    QWidget *buildBackupPage();
    QWidget *buildAppearancePage();

    QStackedWidget *m_stack = nullptr;

    QLabel *m_dataDirLabel = nullptr;
    QLabel *m_dbSizeLabel = nullptr;
    QLabel *m_obsUrlLabel = nullptr;
    QLabel *m_obsIpLabel = nullptr;
    QComboBox *m_bibleInfoLabel = nullptr;
    QLabel *m_countsLabel = nullptr;
};
