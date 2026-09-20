#include "MainWindow.h"
#include "BiblePanel.h"
#include "DetailPanel.h"
#include "DisplayControlPanel.h"
#include "ItemEditDialog.h"
#include "LibraryListPanel.h"
#include "HelpPanel.h"
#include "ImportExportPanel.h"
#include "PlaylistDetailPanel.h"
#include "PlaylistListPanel.h"
#include "SettingsPanel.h"
#include "Sidebar.h"
#include "Theme.h"
#include "VideoDetailPanel.h"
#include "VideoListPanel.h"

#include "core/Database.h"
#include "display/DisplayServer.h"
#include "display/PresentationController.h"
#include "display/SlideContentBuilder.h"
#include "display/SlideRenderWidget.h"

#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSqlDatabase>
#include <QStackedWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace {

bool copyDirRecursively(const QString &source, const QString &destination)
{
    QDir().mkpath(destination);
    QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcFile = it.next();
        const QString relative = QDir(source).relativeFilePath(srcFile);
        const QString destFile = destination + QStringLiteral("/") + relative;
        QDir().mkpath(QFileInfo(destFile).path());
        QFile::remove(destFile);
        if (!QFile::copy(srcFile, destFile))
            return false;
    }
    return true;
}

// Parses the "##" / "#$#" songbook dump format used by some Ukrainian/Russian
// hymnal collections (.sps/.spb): each song is one line of "#$#"-separated
// fields — number, title, ..., text (with "@%" as a line break inside a
// verse and "@$" as a blank line between verses, matching how the app
// already splits song text into slides).
QList<ContentItem> parseSongCollection(const QString &path)
{
    QList<ContentItem> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("##")) || line.trimmed().isEmpty())
            continue;

        const QStringList fields = line.split(QStringLiteral("#$#"));
        if (fields.size() < 7)
            continue;

        const QString number = fields.at(0).trimmed();
        const QString title = fields.at(1).trimmed();
        QString text = fields.at(6);
        text.replace(QStringLiteral("@$"), QStringLiteral("\n\n"));
        text.replace(QStringLiteral("@%"), QStringLiteral("\n"));
        text = text.trimmed();
        if (title.isEmpty() || text.isEmpty())
            continue;

        ContentItem item;
        item.type = ContentType::Song;
        item.title = title;
        item.refLocation = number;
        item.text = text;
        result << item;
    }
    return result;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_presentation = new PresentationController(this);
    buildUi();
    setupShortcuts();
    refreshAll();
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Gather — управление показом"));
    setMinimumSize(1150, 680);
    resize(1440, 800);

    auto *central = new QWidget;
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_sidebar = new Sidebar;

    m_listPanel = new LibraryListPanel;
    m_detailPanel = new DetailPanel;
    m_libraryPage = new QWidget;
    auto *libraryLayout = new QHBoxLayout(m_libraryPage);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(0);
    libraryLayout->addWidget(m_listPanel);
    libraryLayout->addWidget(m_detailPanel, 1);

    m_biblePanel = new BiblePanel;

    m_playlistListPanel = new PlaylistListPanel;
    m_playlistDetailPanel = new PlaylistDetailPanel;
    m_playlistPage = new QWidget;
    auto *playlistLayout = new QHBoxLayout(m_playlistPage);
    playlistLayout->setContentsMargins(0, 0, 0, 0);
    playlistLayout->setSpacing(0);
    playlistLayout->addWidget(m_playlistListPanel);
    playlistLayout->addWidget(m_playlistDetailPanel, 1);

    m_videoListPanel = new VideoListPanel;
    m_videoDetailPanel = new VideoDetailPanel;
    m_videoPage = new QWidget;
    auto *videoLayout = new QHBoxLayout(m_videoPage);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);
    videoLayout->addWidget(m_videoListPanel);
    videoLayout->addWidget(m_videoDetailPanel, 1);

    m_contentStack = new QStackedWidget;
    m_contentStack->addWidget(m_libraryPage);
    m_contentStack->addWidget(m_biblePanel);
    m_contentStack->addWidget(m_playlistPage);
    m_contentStack->addWidget(m_videoPage);

    m_displayPanel = new DisplayControlPanel;

    auto *categoryPage = new QWidget;
    auto *categoryLayout = new QHBoxLayout(categoryPage);
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(0);
    categoryLayout->addWidget(m_contentStack, 1);
    categoryLayout->addWidget(m_displayPanel);

    m_settingsPanel = new SettingsPanel;
    m_importExportPanel = new ImportExportPanel;
    m_helpPanel = new HelpPanel;

    m_outerStack = new QStackedWidget;
    m_outerStack->addWidget(categoryPage);
    m_outerStack->addWidget(m_settingsPanel);
    m_outerStack->addWidget(m_importExportPanel);
    m_outerStack->addWidget(m_helpPanel);

    layout->addWidget(m_sidebar);
    layout->addWidget(m_outerStack, 1);

    setCentralWidget(central);

    m_displayPanel->setController(m_presentation);

    connect(m_sidebar, &Sidebar::categorySelected, this, [this](ContentType type) {
        m_outerStack->setCurrentIndex(0);
        m_contentStack->setCurrentWidget(type == ContentType::BibleVerse ? static_cast<QWidget *>(m_biblePanel) : m_libraryPage);
        m_listPanel->setCategory(type);
        refreshAll();
    });

    connect(m_biblePanel, &BiblePanel::previewRequested, this, [this](const ContentItem &item, int slideIndex) {
        const QStringList slides = item.slides();
        const SlideContent content = buildSlideContent(item, slides, slideIndex);

        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(tr("Предпросмотр"));
        dialog->resize(640, 360);
        auto *dialogLayout = new QVBoxLayout(dialog);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        auto *render = new SlideRenderWidget(dialog);
        render->setContent(content);
        dialogLayout->addWidget(render);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(m_biblePanel, &BiblePanel::goLiveRequested, this, [this](const ContentItem &item, int slideIndex) {
        m_presentation->goLive(item, slideIndex);
    });
    connect(m_biblePanel, &BiblePanel::saveToLibraryRequested, this, [this](ContentItem item) {
        if (!m_repository.add(item))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить запись."));
        else
            refreshAll();
    });
    connect(m_sidebar, &Sidebar::addRequested, this, [this]() {
        ItemEditDialog dialog(this);
        dialog.setDefaultType(m_sidebar->selectedCategory());
        if (dialog.exec() != QDialog::Accepted)
            return;
        ContentItem item = dialog.item();
        if (!m_repository.add(item))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить запись."));
        else
            refreshAll();
    });
    connect(m_sidebar, &Sidebar::playlistsRequested, this, [this]() {
        m_outerStack->setCurrentIndex(0);
        m_contentStack->setCurrentWidget(m_playlistPage);
        refreshPlaylists();
    });
    connect(m_sidebar, &Sidebar::videoRequested, this, [this]() {
        m_outerStack->setCurrentIndex(0);
        m_contentStack->setCurrentWidget(m_videoPage);
        refreshVideos();
    });

    connect(m_playlistListPanel, &PlaylistListPanel::filtersChanged, this, &MainWindow::refreshPlaylists);
    connect(m_playlistListPanel, &PlaylistListPanel::playlistSelected, this, [this](std::optional<int> id) {
        m_playlistDetailPanel->showPlaylist(id.has_value() ? m_playlistRepository.findById(*id) : std::nullopt,
                                             id.has_value() ? m_playlistRepository.entries(*id) : QList<PlaylistEntry>{});
    });
    connect(m_playlistListPanel, &PlaylistListPanel::favoriteToggled, this, [this](int id) {
        const auto playlist = m_playlistRepository.findById(id);
        if (playlist)
            m_playlistRepository.setFavorite(id, !playlist->favorite);
        refreshPlaylists();
    });
    connect(m_playlistListPanel, &PlaylistListPanel::createRequested, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Новый плейлист"), tr("Название:"),
                                                     QLineEdit::Normal, tr("Новый плейлист"), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        Playlist playlist;
        playlist.name = name.trimmed();
        if (m_playlistRepository.add(playlist)) {
            refreshPlaylists();
            m_playlistListPanel->selectPlaylistById(playlist.id);
        }
    });

    connect(m_playlistDetailPanel, &PlaylistDetailPanel::renameRequested, this, [this](int id, QString name) {
        m_playlistRepository.rename(id, name);
        refreshPlaylists();
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::deleteRequested, this, [this](int id) {
        m_playlistRepository.remove(id);
        refreshPlaylists();
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::favoriteToggleRequested, this, [this](int id) {
        const auto playlist = m_playlistRepository.findById(id);
        if (playlist)
            m_playlistRepository.setFavorite(id, !playlist->favorite);
        refreshPlaylists();
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::duplicateRequested, this, [this](int id) {
        const auto playlist = m_playlistRepository.findById(id);
        if (playlist)
            m_playlistRepository.duplicate(id, tr("%1 (копия)").arg(playlist->name));
        refreshPlaylists();
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::addItemRequested, this, [this](int playlistId, int itemId) {
        m_playlistRepository.appendItem(playlistId, itemId);
        refreshPlaylists();
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::removeEntryRequested, this, [this](int rowId) {
        m_playlistRepository.removeEntry(rowId);
        refreshPlaylists();
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::reorderRequested, this, [this](int, QList<int> orderedRowIds) {
        const auto id = m_playlistListPanel->selectedPlaylistId();
        if (id)
            m_playlistRepository.reorder(*id, orderedRowIds);
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::previewRequested, this, [this](const ContentItem &item, int slideIndex) {
        const QStringList slides = item.slides();
        const SlideContent content = buildSlideContent(item, slides, slideIndex);
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(tr("Предпросмотр"));
        dialog->resize(640, 360);
        auto *dialogLayout = new QVBoxLayout(dialog);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        auto *render = new SlideRenderWidget(dialog);
        render->setContent(content);
        dialogLayout->addWidget(render);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(m_playlistDetailPanel, &PlaylistDetailPanel::goLiveRequested, this, [this](const ContentItem &item, int slideIndex) {
        m_presentation->goLive(item, slideIndex);
    });

    connect(m_videoListPanel, &VideoListPanel::filtersChanged, this, &MainWindow::refreshVideos);
    connect(m_videoListPanel, &VideoListPanel::itemSelected, this, [this](std::optional<int> id) {
        m_videoDetailPanel->showItem(id.has_value() ? m_repository.findById(*id) : std::nullopt);
    });
    connect(m_videoListPanel, &VideoListPanel::favoriteToggled, this, &MainWindow::toggleVideoFavorite);
    connect(m_videoListPanel, &VideoListPanel::videoAdded, this, [this](ContentItem item) {
        if (!m_repository.add(item))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить видео."));
        else {
            refreshVideos();
            m_videoListPanel->selectItemById(item.id);
        }
    });

    connect(m_videoDetailPanel, &VideoDetailPanel::deleteRequested, this, [this](int id) {
        const auto item = m_repository.findById(id);
        if (!item)
            return;
        const auto reply = QMessageBox::question(this, tr("Удаление"),
                                                  tr("Удалить видео «%1»?").arg(item->displayTitle()));
        if (reply != QMessageBox::Yes)
            return;
        if (!m_repository.remove(id))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось удалить видео."));
        else
            refreshVideos();
    });
    connect(m_videoDetailPanel, &VideoDetailPanel::favoriteToggleRequested, this, &MainWindow::toggleVideoFavorite);
    connect(m_videoDetailPanel, &VideoDetailPanel::titleChangeRequested, this, [this](int id, const QString &title) {
        auto item = m_repository.findById(id);
        if (!item)
            return;
        item->title = title;
        if (!m_repository.update(*item))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изменения."));
        else
            refreshVideos();
    });
    connect(m_videoDetailPanel, &VideoDetailPanel::notesChanged, this, [this](int id, const QString &notes) {
        m_repository.setNotes(id, notes);
    });
    connect(m_videoDetailPanel, &VideoDetailPanel::replaceRequested, this, [this](int id, const QString &newLocalPath) {
        auto item = m_repository.findById(id);
        if (!item)
            return;
        const QString ext = QFileInfo(newLocalPath).suffix();
        const QString destName = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".") + ext;
        const QString destPath = Database::videosDir() + QStringLiteral("/") + destName;
        if (!QFile::copy(newLocalPath, destPath)) {
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить видео."));
            return;
        }
        item->imagePath = destPath;
        if (!m_repository.update(*item))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изменения."));
        else
            refreshVideos();
    });
    connect(m_videoDetailPanel, &VideoDetailPanel::previewRequested, this, [this](const ContentItem &item, bool loop) {
        const SlideContent content = buildSlideContent(item, QStringList{QString()}, 0, loop);
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(tr("Предпросмотр"));
        dialog->resize(640, 360);
        auto *dialogLayout = new QVBoxLayout(dialog);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        auto *render = new SlideRenderWidget(dialog);
        render->setContent(content);
        dialogLayout->addWidget(render);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(m_videoDetailPanel, &VideoDetailPanel::goLiveRequested, this, [this](const ContentItem &item, bool loop) {
        m_presentation->goLive(item, 0, loop);
    });

    connect(m_sidebar, &Sidebar::importExportRequested, this, [this]() {
        m_importExportPanel->setCounts(m_repository.categoryCounts(), m_playlistRepository.fetch(QString(), SortOrder::Alphabetical).size());
        m_outerStack->setCurrentIndex(2);
    });
    connect(m_sidebar, &Sidebar::settingsRequested, this, &MainWindow::onSettings);
    connect(m_sidebar, &Sidebar::helpRequested, this, [this]() { m_outerStack->setCurrentIndex(3); });
    connect(m_settingsPanel, &SettingsPanel::importExportRequested, this, &MainWindow::onImportExport);
    connect(m_settingsPanel, &SettingsPanel::songImportRequested, this, &MainWindow::onSongImport);
    connect(m_settingsPanel, &SettingsPanel::displaySettingsChanged, this, [this]() {
        m_presentation->refreshDisplayStyle();
    });
    connect(m_importExportPanel, &ImportExportPanel::actionRequested, this, &MainWindow::onImportExport);

    connect(m_listPanel, &LibraryListPanel::filtersChanged, this, &MainWindow::refreshAll);
    connect(m_listPanel, &LibraryListPanel::itemSelected, this, [this](std::optional<int> id) {
        m_detailPanel->showItem(id.has_value() ? m_repository.findById(*id) : std::nullopt);
    });
    connect(m_listPanel, &LibraryListPanel::favoriteToggled, this, &MainWindow::toggleFavorite);

    connect(m_detailPanel, &DetailPanel::editRequested, this, [this](int id) {
        const auto item = m_repository.findById(id);
        if (!item)
            return;
        ItemEditDialog dialog(*item, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        if (!m_repository.update(dialog.item()))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изменения."));
        else
            refreshAll();
    });
    connect(m_detailPanel, &DetailPanel::deleteRequested, this, [this](int id) {
        const auto item = m_repository.findById(id);
        if (!item)
            return;
        const auto reply = QMessageBox::question(this, tr("Удаление"),
                                                  tr("Удалить запись «%1»?").arg(item->displayTitle()));
        if (reply != QMessageBox::Yes)
            return;
        if (!m_repository.remove(id))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось удалить запись."));
        else
            refreshAll();
    });
    connect(m_detailPanel, &DetailPanel::favoriteToggleRequested, this, &MainWindow::toggleFavorite);
    connect(m_detailPanel, &DetailPanel::notesChanged, this, [this](int id, const QString &notes) {
        m_repository.setNotes(id, notes);
    });
    connect(m_detailPanel, &DetailPanel::itemTextUpdated, this, [this](const ContentItem &item) {
        if (!m_repository.update(item))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изменения."));
        else
            refreshAll();
    });
    connect(m_detailPanel, &DetailPanel::previewRequested, this, [this](const ContentItem &item, int slideIndex) {
        const QStringList slides = (item.type == ContentType::Song || item.type == ContentType::BibleVerse)
                                        ? item.slides()
                                        : QStringList{item.type == ContentType::Announcement ? item.text : QString()};
        const SlideContent content = buildSlideContent(item, slides, slideIndex);

        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(tr("Предпросмотр"));
        dialog->resize(640, 360);
        auto *dialogLayout = new QVBoxLayout(dialog);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        auto *render = new SlideRenderWidget(dialog);
        render->setContent(content);
        dialogLayout->addWidget(render);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    connect(m_detailPanel, &DetailPanel::goLiveRequested, this, [this](const ContentItem &item, int slideIndex) {
        m_presentation->goLive(item, slideIndex);
    });
}

void MainWindow::setupShortcuts()
{
    auto *left = new QShortcut(QKeySequence(Qt::Key_Left), this);
    connect(left, &QShortcut::activated, this, [this]() { m_presentation->stepBack(); });

    auto *right = new QShortcut(QKeySequence(Qt::Key_Right), this);
    connect(right, &QShortcut::activated, this, [this]() { m_presentation->stepForward(); });

    auto *black = new QShortcut(QKeySequence(Qt::Key_B), this);
    connect(black, &QShortcut::activated, this, [this]() { m_presentation->toggleBlack(); });

    auto *pause = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(pause, &QShortcut::activated, this, [this]() { m_presentation->toggleFrozen(); });

    auto *goLive = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(goLive, &QShortcut::activated, this, [this]() {
        if (m_outerStack->currentIndex() != 0)
            return;
        if (m_contentStack->currentWidget() == m_biblePanel)
            m_biblePanel->triggerGoLive();
        else if (m_contentStack->currentWidget() == m_playlistPage)
            m_playlistDetailPanel->triggerGoLive();
        else if (m_contentStack->currentWidget() == m_videoPage)
            m_videoDetailPanel->triggerGoLive();
        else
            m_detailPanel->triggerGoLive();
    });
}

void MainWindow::refreshAll()
{
    m_sidebar->setCounts(m_repository.categoryCounts());

    const ContentType category = m_sidebar->selectedCategory();
    QString search = m_listPanel->searchText();
    const SortOrder order = m_listPanel->sortOrder();

    QList<ContentItem> items = m_repository.fetch(search, std::optional<ContentType>(category), order);
    if (m_listPanel->favoritesOnly()) {
        QList<ContentItem> filtered;
        for (const ContentItem &item : items)
            if (item.favorite)
                filtered << item;
        items = filtered;
    }

    m_listPanel->setItems(items);
}

void MainWindow::refreshPlaylists()
{
    const QString search = m_playlistListPanel->searchText();
    const SortOrder order = m_playlistListPanel->sortOrder();
    m_playlistDetailPanel->setLibraryItems(m_repository.fetch(QString(), std::nullopt, SortOrder::Alphabetical));
    m_playlistListPanel->setPlaylists(m_playlistRepository.fetch(search, order));
}

void MainWindow::toggleFavorite(int id)
{
    const auto item = m_repository.findById(id);
    if (!item)
        return;
    m_repository.setFavorite(id, !item->favorite);
    refreshAll();
}

void MainWindow::refreshVideos()
{
    QList<ContentItem> items = m_repository.fetch(m_videoListPanel->searchText(),
                                                    std::optional<ContentType>(ContentType::Video),
                                                    m_videoListPanel->sortOrder());
    if (m_videoListPanel->favoritesOnly()) {
        QList<ContentItem> filtered;
        for (const ContentItem &item : items)
            if (item.favorite)
                filtered << item;
        items = filtered;
    }
    m_videoListPanel->setItems(items);
}

void MainWindow::toggleVideoFavorite(int id)
{
    const auto item = m_repository.findById(id);
    if (!item)
        return;
    m_repository.setFavorite(id, !item->favorite);
    refreshVideos();
}

void MainWindow::onSongImport()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Выберите файлы песен"), QString(),
        tr("Песни и сборники (*.txt *.sps *.spb);;Текстовые файлы (*.txt);;Сборники (*.sps *.spb);;Все файлы (*)"));
    if (paths.isEmpty())
        return;

    int imported = 0;
    for (const QString &path : paths) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QStringLiteral("sps") || suffix == QStringLiteral("spb")) {
            for (ContentItem item : parseSongCollection(path)) {
                if (m_repository.add(item))
                    ++imported;
            }
            continue;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        if (text.isEmpty())
            continue;

        ContentItem item;
        item.type = ContentType::Song;
        item.title = QFileInfo(path).completeBaseName();
        item.text = text;
        if (m_repository.add(item))
            ++imported;
    }

    refreshAll();
    QMessageBox::information(this, tr("Импорт песен"),
        imported > 0 ? tr("Импортировано песен: %1.").arg(imported)
                     : tr("Не удалось импортировать ни одной песни."));
}

void MainWindow::onImportExport()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Импорт / Экспорт"));
    box.setText(tr("Резервное копирование базы данных и фотографий."));
    QPushButton *exportButton = box.addButton(tr("Экспорт..."), QMessageBox::ActionRole);
    QPushButton *importButton = box.addButton(tr("Импорт..."), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == exportButton) {
        const QString destRoot = QFileDialog::getExistingDirectory(this, tr("Выберите папку для резервной копии"));
        if (destRoot.isEmpty())
            return;
        const QString backupDir = destRoot + QStringLiteral("/gather-backup-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QDir().mkpath(backupDir);

        const bool ok = QFile::copy(Database::dataDir() + QStringLiteral("/gather.db"), backupDir + QStringLiteral("/gather.db"))
            && copyDirRecursively(Database::photosDir(), backupDir + QStringLiteral("/photos"));

        if (ok) {
            // Read back by SettingsPanel's "Последняя копия" row — see setInfo().
            QSettings().setValue(QStringLiteral("backup/lastExportedAt"), QDateTime::currentDateTime());
            QMessageBox::information(this, tr("Готово"), tr("Резервная копия сохранена в:\n%1").arg(backupDir));
        } else {
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось создать резервную копию."));
        }
    } else if (box.clickedButton() == importButton) {
        const QString backupDir = QFileDialog::getExistingDirectory(this, tr("Выберите папку с резервной копией"));
        if (backupDir.isEmpty())
            return;
        if (!QFile::exists(backupDir + QStringLiteral("/gather.db"))) {
            QMessageBox::warning(this, tr("Ошибка"), tr("В выбранной папке не найден файл gather.db."));
            return;
        }
        const auto reply = QMessageBox::question(this, tr("Импорт"),
            tr("Текущая база данных будет заменена содержимым резервной копии. Продолжить?"));
        if (reply != QMessageBox::Yes)
            return;

        QSqlDatabase::database().close();
        QFile::remove(Database::dataDir() + QStringLiteral("/gather.db"));
        const bool ok = QFile::copy(backupDir + QStringLiteral("/gather.db"), Database::dataDir() + QStringLiteral("/gather.db"))
            && (!QDir(backupDir + QStringLiteral("/photos")).exists()
                || copyDirRecursively(backupDir + QStringLiteral("/photos"), Database::photosDir()));

        if (!ok || !Database::open()) {
            QMessageBox::critical(this, tr("Ошибка"), tr("Не удалось восстановить резервную копию."));
            return;
        }

        refreshAll();
        QMessageBox::information(this, tr("Готово"), tr("База данных восстановлена."));
    }
}

void MainWindow::onSettings()
{
    const QString translation = m_bibleRepository.defaultTranslation();
    m_settingsPanel->setInfo(Database::dataDir(), m_presentation->server()->displayUrl(),
                              translation, m_bibleRepository.books(translation).size(),
                              m_bibleRepository.totalVerseCount(translation),
                              m_repository.categoryCounts());
    m_outerStack->setCurrentIndex(1);
}

