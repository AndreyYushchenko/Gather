#pragma once

#include "core/BibleRepository.h"
#include "core/ContentRepository.h"
#include "core/PlaylistRepository.h"

#include <QMainWindow>

class Sidebar;
class LibraryListPanel;
class DetailPanel;
class DisplayControlPanel;
class BiblePanel;
class SettingsPanel;
class PlaylistListPanel;
class PlaylistDetailPanel;
class VideoListPanel;
class VideoDetailPanel;
class ImportExportPanel;
class HelpPanel;
class PresentationController;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void setupShortcuts();
    void refreshAll();
    void refreshPlaylists();
    void refreshVideos();
    void toggleFavorite(int id);
    void toggleVideoFavorite(int id);
    void onImportExport();
    void onSongImport();
    void onSettings();

    ContentRepository m_repository;
    BibleRepository m_bibleRepository;
    PlaylistRepository m_playlistRepository;
    PresentationController *m_presentation = nullptr;

    Sidebar *m_sidebar = nullptr;
    QStackedWidget *m_outerStack = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    QWidget *m_libraryPage = nullptr;
    LibraryListPanel *m_listPanel = nullptr;
    DetailPanel *m_detailPanel = nullptr;
    BiblePanel *m_biblePanel = nullptr;
    SettingsPanel *m_settingsPanel = nullptr;
    QWidget *m_playlistPage = nullptr;
    PlaylistListPanel *m_playlistListPanel = nullptr;
    PlaylistDetailPanel *m_playlistDetailPanel = nullptr;
    QWidget *m_videoPage = nullptr;
    VideoListPanel *m_videoListPanel = nullptr;
    VideoDetailPanel *m_videoDetailPanel = nullptr;
    ImportExportPanel *m_importExportPanel = nullptr;
    HelpPanel *m_helpPanel = nullptr;
    DisplayControlPanel *m_displayPanel = nullptr;
};
