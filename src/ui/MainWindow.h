#pragma once

#include "core/BibleRepository.h"
#include "core/ContentRepository.h"
#include "core/PlaylistRepository.h"

#include <QHash>
#include <QMainWindow>

class Sidebar;
class LibraryListPanel;
class DetailPanel;
class SlideStripPanel;
class DisplayControlPanel;
class BiblePanel;
class SettingsPanel;
class PlaylistsPanel;
class AnnouncementsPanel;
class VideosPanel;
class ImportExportPanel;
class HelpPanel;
class TimersPanel;
class PhotosPanel;
class PresentationController;
class ObsClient;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    // Developer snapshots (see main.cpp): a section key, or "settings:<page>".
    void openForSnapshot(const QString &what);

private:
    void buildUi();
    void setupShortcuts();
    void refreshAll();
    void refreshPlaylists();
    void toggleFavorite(int id);
    void onImportExport();
    void onSongImport();
    // The import itself, for files already chosen.
    void importSongFiles(const QStringList &paths);
    void onBibleImport();
    void onSettings();
    // Re-reads what Настройки shows (lists, counts) without switching to it.
    void refreshSettingsContext();
    // Delete buttons in Настройки → Песни / Библия (confirm + backup first).
    void removeSongCollection(const QString &collection);
    void removeBibleTranslation(const QString &translation);
    bool backupBeforeRemoving();
    // "Сборник для новых песен" (Настройки → Песни), "Мои песни" by default.
    QString defaultSongCollection() const;
    bool exportSongCollection(const QString &collection, const QString &path, QString *error);
    void showPreviewDialog(const ContentItem &item, int slideIndex = 0);
    // "Сохранить" in Настройки: apply what changed.
    void applySettings(const QStringList &keys);
    void restartApp();
    void runAutomaticBackup();
    void applyShortcutKeys();
    // "При запуске открывать" / "Запоминать последний открытый элемент".
    void restoreStartupState();
    QString currentSectionKey() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private:

    ContentRepository m_repository;
    BibleRepository m_bibleRepository;
    PlaylistRepository m_playlistRepository;
    PresentationController *m_presentation = nullptr;
    ObsClient *m_obs = nullptr;

    Sidebar *m_sidebar = nullptr;
    QStackedWidget *m_outerStack = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    QWidget *m_libraryPage = nullptr;
    LibraryListPanel *m_listPanel = nullptr;
    DetailPanel *m_detailPanel = nullptr;
    SlideStripPanel *m_slideStrip = nullptr;
    BiblePanel *m_biblePanel = nullptr;
    SettingsPanel *m_settingsPanel = nullptr;
    PlaylistsPanel *m_playlistsPanel = nullptr;
    AnnouncementsPanel *m_announcementsPanel = nullptr;
    VideosPanel *m_videosPanel = nullptr;
    ImportExportPanel *m_importExportPanel = nullptr;
    HelpPanel *m_helpPanel = nullptr;
    TimersPanel *m_timersPanel = nullptr;
    PhotosPanel *m_photosPanel = nullptr;
    DisplayControlPanel *m_displayPanel = nullptr;
    class HotkeyRouter *m_hotkeys = nullptr;
};
