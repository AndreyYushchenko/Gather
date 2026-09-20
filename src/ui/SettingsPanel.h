#pragma once

#include "core/ContentItem.h"

#include <QMap>
#include <QWidget>

class QComboBox;
class QLabel;
class QStackedWidget;
class QVBoxLayout;

// "Settings" screen: a left section nav + a card on the right, one page per
// section (Общие/Показ/Библия/Песни и сборники/База данных/Горячие
// клавиши/OBS и сеть/Резервная копия). Every interactive control here is
// wired to something the app actually does — no decorative toggles/dropdowns
// that silently do nothing when changed. Rows that only ever reflect
// existing, non-configurable behavior (e.g. "shadow is always on") are shown
// as plain read-only info, not as a control the user could reasonably expect
// to flip. See the code-review note from 2026-09-20: the previous version of
// this file mirrored the design.pen mockup wholesale, including ~25 controls
// (theme/language switching, chords, transitions, cloud sync, password auth,
// scheduled backups, ...) with no backing implementation at all.
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
    // (Re)fills m_stack with the 8 pages below, in nav order. Split out so
    // "Сбросить оформление показа" can rebuild the stack after clearing
    // DisplaySettings, without duplicating the addWidget list.
    void populateStack();
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

    QStackedWidget *m_stack = nullptr;

    QLabel *m_dataDirLabel = nullptr;
    QLabel *m_dbSizeLabel = nullptr;
    QLabel *m_obsUrlLabel = nullptr;
    QLabel *m_obsIpLabel = nullptr;
    QLabel *m_bibleInfoLabel = nullptr;
    QLabel *m_countsLabel = nullptr;
    QLabel *m_lastBackupLabel = nullptr;

    // Cached from the last setInfo() call, so populateStack() can restore
    // the labels above after a full page rebuild (see "Сбросить...").
    bool m_hasInfo = false;
    QString m_lastDataDir;
    QString m_lastObsUrl;
    QString m_lastBibleTranslation;
    int m_lastBibleBookCount = 0;
    int m_lastBibleVerseCount = 0;
    QMap<ContentType, int> m_lastCounts;
};
