#include "MainWindow.h"
#include "BiblePanel.h"
#include "DetailPanel.h"
#include "DisplayControlPanel.h"
#include "ItemEditDialog.h"
#include "LibraryListPanel.h"
#include "HelpPanel.h"
#include "HotkeyRouter.h"
#include "IconProvider.h"
#include "ImportExportPanel.h"
#include "AnnouncementsPanel.h"
#include "PlaylistsPanel.h"
#include "SettingsPanel.h"
#include "Sidebar.h"
#include "SlideStripPanel.h"
#include "Theme.h"
#include "TimersPanel.h"
#include "PhotosPanel.h"
#include "VideosPanel.h"

#include "core/AppSettings.h"
#include "core/BackupManager.h"
#include "core/Database.h"
#include "display/DisplayServer.h"
#include "display/ObsClient.h"
#include "display/PresentationController.h"
#include "display/SlideContentBuilder.h"
#include "display/SlideRenderWidget.h"

#include <QCloseEvent>
#include <QTimer>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QStringDecoder>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QCoreApplication>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlDatabase>
#include <QStackedWidget>
#include <QStandardPaths>
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
// `title`: the songbook's own name from its "##<name>" header line, if any.
QList<ContentItem> parseSongCollection(const QString &path, QString *title = nullptr)
{
    QList<ContentItem> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    const QByteArray raw = file.readAll();
    file.close();

    QString content = QString::fromUtf8(raw);
    if (raw != content.toUtf8()) {
        auto cp1251Decoder = QStringDecoder("windows-1251");
        content = cp1251Decoder(raw);
    }

    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("##"))) {
            // "##" alone, then "##<songbook name>", then "##<publisher notes>".
            const QString header = line.mid(2).trimmed();
            if (title && title->isEmpty() && !header.isEmpty() && !header.contains(QStringLiteral("@%")))
                *title = header;
            continue;
        }
        if (line.trimmed().isEmpty())
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

// ChordPro (.cho/.chordpro/.crd): {title: …} names the song, {soc}/{eoc}
// wrap the chorus (labelled "Припев" so it repeats like other songs'),
// {comment: …} becomes a label line; chords stay inline as [Am] so
// "Показывать аккорды" can draw or hide them.
ContentItem parseChordPro(const QString &path)
{
    ContentItem item;
    item.type = ContentType::Song;
    item.title = QFileInfo(path).completeBaseName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return item;
    QStringList lines;
    const auto blank = [&lines]() {
        if (!lines.isEmpty() && !lines.last().isEmpty())
            lines << QString();
    };
    static const QRegularExpression directive(QStringLiteral(R"(^\{\s*([a-z_]+)\s*(?::\s*(.*?))?\s*\}$)"),
                                              QRegularExpression::CaseInsensitiveOption);
    for (const QString &raw : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#')))
            continue;
        const QRegularExpressionMatch match = directive.match(line);
        if (!match.hasMatch()) {
            if (line.isEmpty())
                blank();
            else
                lines << line;
            continue;
        }
        const QString name = match.captured(1).toLower();
        const QString value = match.captured(2).trimmed();
        if ((name == QLatin1String("title") || name == QLatin1String("t")) && !value.isEmpty()) {
            item.title = value;
        } else if (name == QLatin1String("soc") || name == QLatin1String("start_of_chorus")) {
            blank();
            lines << QObject::tr("Припев:");
        } else if (name == QLatin1String("eoc") || name == QLatin1String("end_of_chorus")
                   || name == QLatin1String("eov") || name == QLatin1String("end_of_verse")) {
            blank();
        } else if (name == QLatin1String("sov") || name == QLatin1String("start_of_verse")) {
            blank();
        } else if ((name == QLatin1String("comment") || name == QLatin1String("c") || name == QLatin1String("ci")) && !value.isEmpty()) {
            blank();
            lines << value;
        }
    }
    item.text = lines.join(QLatin1Char('\n')).trimmed();
    return item;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_presentation = new PresentationController(this);
    m_presentation->setControlWindow(this);
    m_obs = new ObsClient(this);
    buildUi();
    setupShortcuts();
    refreshAll();
    restoreStartupState();

    // "Автоматическое резервное копирование": checked at start and hourly.
    QTimer::singleShot(15000, this, &MainWindow::runAutomaticBackup);
    auto *backupTimer = new QTimer(this);
    backupTimer->setInterval(60 * 60 * 1000);
    connect(backupTimer, &QTimer::timeout, this, &MainWindow::runAutomaticBackup);
    backupTimer->start();

    // OBS: switch to "Сцена показа" whenever something goes on screen after
    // nothing was.
    connect(m_presentation, &PresentationController::contentChanged, this, [this, wasHidden = true](const SlideContent &content) mutable {
        const bool hidden = content.kind == SlideKind::Empty;
        if (hidden == wasHidden)
            return;
        wasHidden = hidden;
        if (!hidden)
            m_obs->switchToShowScene();
        else
            m_obs->switchToPreviousScene();
    });
}

void MainWindow::openForSnapshot(const QString &what)
{
    if (what == QLatin1String("add-song")) {
        // The "Добавить" dialog for a song, saved next to the snapshot.
        auto *dialog = new ItemEditDialog(this);
        dialog->setDefaultType(ContentType::Song);
        dialog->setSongCollections(m_repository.songCollections(), defaultSongCollection());
        dialog->show();
        QTimer::singleShot(1500, dialog, [dialog]() {
            dialog->grab().save(qEnvironmentVariable("SERMON_SNAPSHOT") + QStringLiteral(".dialog.png"));
        });
    } else if (what.startsWith(QLatin1String("export:"))) {
        // "export:<songbook>|<file>" — save a songbook without the file dialog.
        const QString spec = what.mid(7);
        exportSongCollection(spec.section(QLatin1Char('|'), 0, 0), spec.section(QLatin1Char('|'), 1), nullptr);
    } else if (what.startsWith(QLatin1String("import:"))) {
        // "import:<file>" — run a song import without the file dialog.
        importSongFiles({what.mid(7)});
    } else if (what.startsWith(QLatin1String("live:"))) {
        // "live:<item id>:<slide>" — put an item on screen (checking slide layout).
        m_sidebar->openSection(QStringLiteral("songs"));
        if (const auto item = m_repository.findById(what.section(QLatin1Char(':'), 1, 1).toInt()))
            m_presentation->goLive(*item, what.section(QLatin1Char(':'), 2, 2).toInt());
    } else if (what.startsWith(QLatin1String("settings"))) {
        m_sidebar->openSection(QStringLiteral("settings"));
        m_settingsPanel->showPage(what.section(QLatin1Char(':'), 1).toInt());
    } else if (!what.isEmpty()) {
        m_sidebar->openSection(what);
    }
}

void MainWindow::runAutomaticBackup()
{
    if (!BackupManager::automaticBackupDue())
        return;
    BackupManager::createBackup(true, nullptr, nullptr);
    m_settingsPanel->refreshStatusRows();
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Sermon — управление показом"));

    auto *central = new QWidget;
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_sidebar = new Sidebar;

    m_listPanel = new LibraryListPanel;
    m_detailPanel = new DetailPanel;
    m_slideStrip = new SlideStripPanel;
    m_libraryPage = new QWidget;
    m_libraryPage->setAttribute(Qt::WA_StyledBackground, true);
    // Scoped to the page itself: a bare "background:" would cascade into
    // every child and paint over their own backgrounds.
    m_libraryPage->setObjectName(QStringLiteral("LibraryPage"));
    m_libraryPage->setStyleSheet(QStringLiteral("QWidget#LibraryPage { background: #ffffff; }"));
    auto *libraryLayout = new QVBoxLayout(m_libraryPage);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(0);
    auto *libraryTopRow = new QHBoxLayout;
    libraryTopRow->setContentsMargins(0, 0, 0, 0);
    libraryTopRow->setSpacing(0);
    // Same reasoning as categoryLayout's split below: a real stretch on both
    // sides so a deficit is shared instead of one side absorbing all of it.
    libraryTopRow->addWidget(m_listPanel, 1);
    libraryTopRow->addWidget(m_detailPanel, 2);
    libraryLayout->addLayout(libraryTopRow, 1);
    libraryLayout->addWidget(m_slideStrip);

    m_biblePanel = new BiblePanel;

    // design.pen flrNK: playlist list + the selected playlist's card.
    m_playlistsPanel = new PlaylistsPanel(&m_repository, &m_playlistRepository);
    // design.pen cB8xz: announcement cards + "Редактирование объявления".
    m_announcementsPanel = new AnnouncementsPanel(&m_repository);

    // design.pen "Sermon App - Video": one gallery screen (grid/list,
    // selection bar, "На экран ⌄").
    m_videosPanel = new VideosPanel(&m_repository);

    m_contentStack = new QStackedWidget;
    m_contentStack->addWidget(m_libraryPage);
    m_contentStack->addWidget(m_biblePanel);
    m_contentStack->addWidget(m_playlistsPanel);
    m_contentStack->addWidget(m_videosPanel);
    m_contentStack->addWidget(m_announcementsPanel);

    // Like the other category pages, the timers screen sits next to the
    // projector panel (design.pen node o2YXd) rather than replacing it.
    m_timersPanel = new TimersPanel;
    m_contentStack->addWidget(m_timersPanel);

    // design.pen EqavR: the photo gallery screen, replacing the generic
    // list + detail layout for this one category.
    m_photosPanel = new PhotosPanel(&m_repository);
    m_contentStack->addWidget(m_photosPanel);

    m_displayPanel = new DisplayControlPanel;
    connect(m_displayPanel, &DisplayControlPanel::addCurrentSongRequested, this, [this]() {
        std::optional<ContentItem> item;
        if (m_contentStack->currentWidget() == m_libraryPage)
            item = m_detailPanel->currentItem();
        else if (m_contentStack->currentWidget() == m_playlistsPanel)
            item = m_playlistsPanel->selectedItem();

        if (!item || item->type != ContentType::Song) {
            QMessageBox::information(this, tr("Быстрый список"),
                                      tr("Выберите песню в библиотеке или плейлисте, чтобы добавить её в быстрый список."));
            return;
        }
        m_displayPanel->addSongToQuickList(*item);
    });

    auto *categoryPage = new QWidget;
    auto *categoryLayout = new QHBoxLayout(categoryPage);
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(0);
    // Both sides need a real (nonzero) stretch factor, not just m_contentStack:
    // QBoxLayout hands a stretch=0 sibling *all* of any shrink deficit before
    // touching a stretch>0 one at all, rather than sharing it — so with
    // m_displayPanel at stretch 0, it was getting crushed down past its own
    // minimumWidth (see DisplayControlPanel) while m_contentStack sat at its
    // full preferred size. Weighted roughly by their design widths
    // (~1002 vs ~430) so a real deficit is shared proportionately instead.
    categoryLayout->addWidget(m_contentStack, 7);
    categoryLayout->addWidget(m_displayPanel, 3);

    m_settingsPanel = new SettingsPanel;
    m_importExportPanel = new ImportExportPanel;
    m_helpPanel = new HelpPanel;

    m_outerStack = new QStackedWidget;
    m_outerStack->addWidget(categoryPage);
    m_outerStack->addWidget(m_settingsPanel);
    m_outerStack->addWidget(m_importExportPanel);
    m_outerStack->addWidget(m_helpPanel);

    // Same reasoning as categoryLayout's split below: give m_sidebar a real
    // (if small) stretch too, so a genuine deficit is shared instead of
    // being dumped entirely on it before m_outerStack gives up anything.
    layout->addWidget(m_sidebar, 1);
    layout->addWidget(m_outerStack, 9);

    setCentralWidget(central);

    // Must come *after* setCentralWidget()/the layout exists, not before:
    // called earlier (before any child widget existed), Qt's own layout
    // activation on first show() was overriding it with the layout's
    // preferred size — every panel sitting at its own *maximum* width
    // (Sidebar+ListPanel+DetailPanel+DisplayControlPanel, each capped but
    // still "wanting" to be as wide as its cap) adds up to well over
    // 1600px, so the window opened far bigger than requested. design.pen's
    // own canvas is a static 1680×940 frame per screen, but opening the
    // window at that size by default is oversized for a lot of real
    // displays — open smaller and let panels genuinely adapt down from
    // there (see ReferenceField in BiblePanel.cpp for the pattern).
    setMinimumSize(1024, 576);
    resize(1150, 650);

    m_displayPanel->setController(m_presentation);
    m_timersPanel->setController(m_presentation);
    connect(m_contentStack, &QStackedWidget::currentChanged, this, [this]() {
        // The quick list is for songs only (not next to Bible, although
        // design.pen shows it there too).
        m_displayPanel->setQuickListVisible(m_contentStack->currentWidget() == m_libraryPage);
    });
    connect(m_photosPanel, &PhotosPanel::goLiveRequested, this, [this](const QList<ContentItem> &photos, int startIndex) {
        m_presentation->goLivePhotos(photos, startIndex);
    });
    connect(m_photosPanel, &PhotosPanel::libraryChanged, this, [this]() {
        m_sidebar->setCounts(m_repository.categoryCounts());
    });


    connect(m_sidebar, &Sidebar::categorySelected, this, [this](ContentType type) {
        m_outerStack->setCurrentIndex(0);
        if (type == ContentType::Photo) {
            // The Photos screen has its own gallery; don't rebuild the (hidden)
            // library list for it.
            m_contentStack->setCurrentWidget(m_photosPanel);
            m_photosPanel->reload();
            m_sidebar->setCounts(m_repository.categoryCounts());
            return;
        }
        if (type == ContentType::Announcement) {
            m_contentStack->setCurrentWidget(m_announcementsPanel);
            m_announcementsPanel->reload();
            m_sidebar->setCounts(m_repository.categoryCounts());
            return;
        }
        m_contentStack->setCurrentWidget(type == ContentType::BibleVerse ? static_cast<QWidget *>(m_biblePanel) : m_libraryPage);
        m_listPanel->setCategory(type);
        refreshAll();
    });

    connect(m_biblePanel, &BiblePanel::previewRequested, this, [this](const ContentItem &item, int slideIndex) {
        showPreviewDialog(item, slideIndex);
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
        if (m_contentStack->currentWidget() == m_photosPanel && m_outerStack->currentIndex() == 0) {
            m_photosPanel->importPhotos();
            return;
        }
        if (m_contentStack->currentWidget() == m_announcementsPanel && m_outerStack->currentIndex() == 0) {
            m_announcementsPanel->createNew();
            return;
        }
        if (m_contentStack->currentWidget() == m_playlistsPanel && m_outerStack->currentIndex() == 0) {
            m_playlistsPanel->createPlaylist();
            return;
        }
        ItemEditDialog dialog(this);
        dialog.setDefaultType(m_sidebar->selectedCategory());
        dialog.setSongCollections(m_repository.songCollections(), defaultSongCollection());
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
        m_contentStack->setCurrentWidget(m_playlistsPanel);
        refreshPlaylists();
    });
    connect(m_sidebar, &Sidebar::videoRequested, this, [this]() {
        m_outerStack->setCurrentIndex(0);
        m_contentStack->setCurrentWidget(m_videosPanel);
        m_videosPanel->reload();
    });

    connect(m_playlistsPanel, &PlaylistsPanel::goLivePlaylistRequested, this,
            [this](const QList<ContentItem> &items, int index, int playlistId) {
                m_presentation->goLivePlaylist(items, index, playlistId);
            });
    connect(m_playlistsPanel, &PlaylistsPanel::countChanged, m_sidebar, &Sidebar::setPlaylistCount);
    connect(m_playlistsPanel, &PlaylistsPanel::previewRequested, this, [this](const ContentItem &item) {
        showPreviewDialog(item);
    });
    connect(m_presentation, &PresentationController::playlistPositionChanged, this, [this]() {
        m_playlistsPanel->setLiveEntry(m_presentation->livePlaylistId(), m_presentation->livePlaylistIndex());
    });

    connect(m_announcementsPanel, &AnnouncementsPanel::goLiveRequested, this, [this](const ContentItem &item) {
        m_presentation->goLive(item, 0);
    });
    connect(m_announcementsPanel, &AnnouncementsPanel::previewRequested, this, [this](const ContentItem &item) {
        showPreviewDialog(item);
    });
    connect(m_announcementsPanel, &AnnouncementsPanel::announcementSaved, this, [this](const ContentItem &item) {
        // An edit to what's on screen shows up there right away.
        if (!m_presentation->isPlaylistLive() && m_presentation->liveItemId() == item.id)
            m_presentation->goLive(item, 0);
    });
    connect(m_announcementsPanel, &AnnouncementsPanel::libraryChanged, this, [this]() {
        m_sidebar->setCounts(m_repository.categoryCounts());
    });

    connect(m_videosPanel, &VideosPanel::goLiveRequested, this, [this](const ContentItem &item, bool loop) {
        m_presentation->goLive(item, 0, loop);
    });
    connect(m_videosPanel, &VideosPanel::libraryChanged, this, [this]() {
        m_sidebar->setCounts(m_repository.categoryCounts());
    });

    connect(m_sidebar, &Sidebar::importExportRequested, this, [this]() {
        m_importExportPanel->setCounts(m_repository.categoryCounts(), m_playlistRepository.fetch(QString(), SortOrder::Alphabetical).size());
        m_outerStack->setCurrentIndex(2);
    });
    connect(m_sidebar, &Sidebar::settingsRequested, this, &MainWindow::onSettings);
    connect(m_sidebar, &Sidebar::helpRequested, this, [this]() { m_outerStack->setCurrentIndex(3); });
    connect(m_sidebar, &Sidebar::timersRequested, this, [this]() {
        m_outerStack->setCurrentIndex(0);
        m_contentStack->setCurrentWidget(m_timersPanel);
    });
    connect(m_settingsPanel, &SettingsPanel::importExportRequested, this, &MainWindow::onImportExport);
    connect(m_settingsPanel, &SettingsPanel::songImportRequested, this, &MainWindow::onSongImport);
    connect(m_settingsPanel, &SettingsPanel::bibleImportRequested, this, &MainWindow::onBibleImport);
    connect(m_settingsPanel, &SettingsPanel::removeCollectionRequested, this, &MainWindow::removeSongCollection);
    connect(m_settingsPanel, &SettingsPanel::removeTranslationRequested, this, &MainWindow::removeBibleTranslation);
    connect(m_settingsPanel, &SettingsPanel::exportCollectionRequested, this, [this](const QString &collection) {
        const QString name = collection.isEmpty() ? tr("Без сборника") : collection;
        QString fileName = name;
        fileName.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
        const QString path = QFileDialog::getSaveFileName(this, tr("Сохранить сборник"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + fileName + QStringLiteral(".sps"),
            tr("Сборник (*.sps)"));
        if (path.isEmpty())
            return;
        QString error;
        if (exportSongCollection(collection, path, &error))
            QMessageBox::information(this, tr("Сохранить сборник"), tr("Сборник «%1» сохранён:\n%2").arg(name, QDir::toNativeSeparators(path)));
        else
            QMessageBox::warning(this, tr("Сохранить сборник"), tr("Не удалось сохранить сборник.\n%1").arg(error));
    });
    connect(m_settingsPanel, &SettingsPanel::settingsSaved, this, &MainWindow::applySettings);
    connect(m_settingsPanel, &SettingsPanel::obsTestRequested, this, [this](const QString &host, int port, const QString &password) {
        m_obs->test(host, port, password);
    });
    connect(m_obs, &ObsClient::testFinished, m_settingsPanel, &SettingsPanel::setObsStatus);
    connect(m_settingsPanel, &SettingsPanel::backupNowRequested, this, [this]() {
        QString path;
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = BackupManager::createBackup(false, &path, &error);
        QApplication::restoreOverrideCursor();
        m_settingsPanel->refreshStatusRows();
        if (ok)
            QMessageBox::information(this, tr("Резервная копия"), tr("Копия сохранена:\n%1").arg(QDir::toNativeSeparators(path)));
        else
            QMessageBox::warning(this, tr("Резервная копия"), tr("Не удалось создать копию.\n%1").arg(error));
    });
    connect(m_settingsPanel, &SettingsPanel::restoreRequested, this, [this]() {
        const QString db = QFileDialog::getOpenFileName(this, tr("Выберите sermon.db из папки резервной копии"), AppSettings::backupDir(),
                                                        tr("База Sermon (sermon.db *.db)"));
        if (db.isEmpty())
            return;
        if (QMessageBox::question(this, tr("Восстановление"),
                                  tr("Текущая библиотека будет заменена копией (текущая сохранится рядом на всякий случай). "
                                     "После этого Sermon перезапустится. Продолжить?")) != QMessageBox::Yes)
            return;
        QString error;
        if (!BackupManager::restore(db, &error)) {
            QMessageBox::critical(this, tr("Восстановление"), tr("Не удалось восстановить.\n%1").arg(error));
            return;
        }
        restartApp();
    });
    connect(m_settingsPanel, &SettingsPanel::optimizeRequested, this, [this]() {
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const qint64 saved = BackupManager::optimizeDatabase(&error);
        QApplication::restoreOverrideCursor();
        onSettings(); // refresh size / date
        if (saved < 0)
            QMessageBox::warning(this, tr("Оптимизация"), tr("Не удалось оптимизировать базу.\n%1").arg(error));
        else
            QMessageBox::information(this, tr("Оптимизация"), tr("Готово. Освобождено: %1 КБ.").arg(saved / 1024));
    });
    connect(m_importExportPanel, &ImportExportPanel::actionRequested, this, &MainWindow::onImportExport);

    connect(m_listPanel, &LibraryListPanel::filtersChanged, this, &MainWindow::refreshAll);
    connect(m_listPanel, &LibraryListPanel::itemSelected, this, [this](std::optional<int> id) {
        const auto item = id.has_value() ? m_repository.findById(*id) : std::nullopt;
        m_detailPanel->showItem(item);
        m_slideStrip->showItem(item);
    });
    connect(m_listPanel, &LibraryListPanel::favoriteToggled, this, &MainWindow::toggleFavorite);

    connect(m_detailPanel, &DetailPanel::editRequested, this, [this](int id) {
        const auto item = m_repository.findById(id);
        if (!item)
            return;
        ItemEditDialog dialog(*item, this);
        dialog.setSongCollections(m_repository.songCollections(), defaultSongCollection());
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

    connect(m_slideStrip, &SlideStripPanel::itemTextUpdated, this, [this](const ContentItem &item) {
        if (!m_repository.update(item))
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить изменения."));
        else
            refreshAll();
    });
    connect(m_slideStrip, &SlideStripPanel::previewRequested, this, [this](const ContentItem &item, int slideIndex) {
        showPreviewDialog(item, slideIndex);
    });
    connect(m_slideStrip, &SlideStripPanel::goLiveRequested, this, [this](const ContentItem &item, int slideIndex) {
        m_presentation->goLive(item, slideIndex);
    });
}

void MainWindow::showPreviewDialog(const ContentItem &item, int slideIndex)
{
    const QStringList slides = (item.type == ContentType::Song || item.type == ContentType::BibleVerse)
                                    ? item.presentationSlides()
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
}

void MainWindow::setupShortcuts()
{
    // Keys come from Настройки → Горячие клавиши (applyShortcutKeys).
    m_hotkeys = new HotkeyRouter(this);
    const auto add = [this](const QString &key, const std::function<void()> &action) {
        m_hotkeys->setAction(key, {}, action);
    };
    add(AppSettings::KeyPrev, [this]() { m_presentation->stepBack(); });
    add(AppSettings::KeyNext, [this]() { m_presentation->stepForward(); });
    add(AppSettings::KeyPause, [this]() { m_presentation->toggleFrozen(); });
    add(AppSettings::KeyEndShow, [this]() { m_presentation->endShow(); });
    add(AppSettings::KeyGoLive, [this]() {
        if (m_outerStack->currentIndex() != 0)
            return;
        QWidget *page = m_contentStack->currentWidget();
        if (page == m_biblePanel)
            m_biblePanel->triggerGoLive();
        else if (page == m_playlistsPanel)
            m_playlistsPanel->goLiveSelected();
        else if (page == m_announcementsPanel)
            m_announcementsPanel->goLiveSelected();
        else if (page == m_videosPanel)
            m_videosPanel->goLiveSelected();
        else if (page == m_photosPanel)
            m_photosPanel->showSelected();
        else if (page == m_timersPanel)
            m_timersPanel->goLiveSelected();
        else if (page == m_libraryPage)
            m_slideStrip->triggerGoLive();
    });
    add(AppSettings::KeySearch, [this]() {
        activateWindow();
        QWidget *page = m_outerStack->currentIndex() == 0 ? m_contentStack->currentWidget() : m_outerStack->currentWidget();
        for (QLineEdit *field : page->findChildren<QLineEdit *>(QStringLiteral("SearchField"))) {
            if (field->isVisible()) {
                field->setFocus(Qt::ShortcutFocusReason);
                field->selectAll();
                return;
            }
        }
    });
    add(AppSettings::KeyAdd, [this]() {
        if (m_outerStack->currentIndex() == 0)
            emit m_sidebar->addRequested();
    });
    applyShortcutKeys();
}

void MainWindow::applyShortcutKeys()
{
    static const QStringList actions{AppSettings::KeyPrev, AppSettings::KeyNext, AppSettings::KeyPause, AppSettings::KeyEndShow,
                                     AppSettings::KeyGoLive, AppSettings::KeySearch, AppSettings::KeyAdd};
    QHash<QString, QList<QKeyCombination>> keys;
    QList<QKeyCombination> used;
    for (const QString &action : actions) {
        const QKeySequence sequence = AppSettings::shortcut(action);
        if (!sequence.isEmpty()) {
            keys[action] << sequence[0];
            used << sequence[0];
        }
    }
    // Presentation clickers send PageDown / PageUp: always accepted for
    // next / previous, unless the operator gave those keys another job.
    const auto extra = [&](const QString &action, Qt::Key key) {
        if (!used.contains(QKeyCombination(key)))
            keys[action] << QKeyCombination(key);
    };
    extra(AppSettings::KeyNext, Qt::Key_PageDown);
    extra(AppSettings::KeyPrev, Qt::Key_PageUp);
    for (const QString &action : actions)
        m_hotkeys->setKeys(action, keys.value(action));

    const auto label = [](const QString &action) {
        return AppSettings::shortcut(action).toString(QKeySequence::PortableText); // "Esc", "Space" as in design.pen
    };
    m_displayPanel->setShortcutLabels(label(AppSettings::KeyEndShow), label(AppSettings::KeyPause));
}

QString MainWindow::currentSectionKey() const
{
    const QWidget *page = m_contentStack->currentWidget();
    if (page == m_biblePanel) return QStringLiteral("bible");
    if (page == m_announcementsPanel) return QStringLiteral("announcements");
    if (page == m_photosPanel) return QStringLiteral("photos");
    if (page == m_videosPanel) return QStringLiteral("videos");
    if (page == m_playlistsPanel) return QStringLiteral("playlists");
    if (page == m_timersPanel) return QStringLiteral("timers");
    return QStringLiteral("songs");
}

void MainWindow::restoreStartupState()
{
    QString section = AppSettings::value(AppSettings::StartupSection).toString();
    if (section == QLatin1String("last"))
        section = AppSettings::value(AppSettings::LastSection).toString();
    m_sidebar->openSection(section);

    if (AppSettings::value(AppSettings::RememberItem).toBool()) {
        const int id = QSettings().value(QStringLiteral("state/lastItem/") + section, -1).toInt();
        if (id >= 0) {
            if (section == QLatin1String("songs"))
                m_listPanel->selectItemById(id);
            else if (section == QLatin1String("announcements"))
                m_announcementsPanel->selectById(id);
            else if (section == QLatin1String("playlists"))
                m_playlistsPanel->selectById(id);
        }
    }

    if (AppSettings::value(AppSettings::RememberDisplayWindow).toBool()
        && AppSettings::value(AppSettings::DisplayWindowWasOpen).toBool())
        m_presentation->openDisplayWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    AppSettings::setValue(AppSettings::LastSection, currentSectionKey());
    AppSettings::setValue(AppSettings::DisplayWindowWasOpen, m_presentation->isDisplayWindowVisible());
    QSettings settings;
    if (const auto id = m_listPanel->selectedItemId())
        settings.setValue(QStringLiteral("state/lastItem/songs"), *id);
    if (m_announcementsPanel->selectedId() >= 0)
        settings.setValue(QStringLiteral("state/lastItem/announcements"), m_announcementsPanel->selectedId());
    if (m_playlistsPanel->selectedId() >= 0)
        settings.setValue(QStringLiteral("state/lastItem/playlists"), m_playlistsPanel->selectedId());
    QMainWindow::closeEvent(event);
    // The projector window is a separate top-level window: close it too.
    QCoreApplication::quit();
}

void MainWindow::refreshAll()
{
    m_sidebar->setCounts(m_repository.categoryCounts());
    m_sidebar->setPlaylistCount(m_playlistRepository.fetch(QString(), SortOrder::DateAddedDesc).size());

    const ContentType category = m_sidebar->selectedCategory();
    if (category == ContentType::Photo) {
        m_photosPanel->reload();
        return;
    }
    if (category == ContentType::Announcement) {
        m_announcementsPanel->reload();
        return;
    }
    QString search = m_listPanel->searchText();
    const SortOrder order = m_listPanel->sortOrder();

    QList<ContentItem> items = m_repository.fetch(search, std::optional<ContentType>(category), order);
    // "Активный сборник".
    const QString collection = AppSettings::value(AppSettings::ActiveCollection).toString();
    if (category == ContentType::Song && !collection.isEmpty()) {
        QList<ContentItem> inCollection;
        for (const ContentItem &item : std::as_const(items))
            if (item.refBook == collection)
                inCollection << item;
        items = inCollection;
    }
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
    m_playlistsPanel->reload();
}

void MainWindow::toggleFavorite(int id)
{
    const auto item = m_repository.findById(id);
    if (!item)
        return;
    const bool newFavorite = !item->favorite;
    m_repository.setFavorite(id, newFavorite);

    // Favorites-only view needs the row to actually appear/disappear, which
    // only a full re-fetch + rebuild can do.
    if (m_listPanel->favoritesOnly()) {
        refreshAll();
        return;
    }

    // Otherwise, don't tear down and rebuild every row in the list (up to
    // ~800 real widgets) just to flip one star — update this one row in
    // place. The full rebuild used to run on every single favorite click,
    // which was slow and, combined with the list's scroll handling, could
    // crash (QWidget::show() segfault) while hundreds of item widgets were
    // being replaced out from under the view.
    m_sidebar->setCounts(m_repository.categoryCounts());
    m_listPanel->setItemFavorite(id, newFavorite);
    if (m_listPanel->selectedItemId() == id) {
        const auto updated = m_repository.findById(id);
        m_detailPanel->showItem(updated);
        m_slideStrip->showItem(updated);
    }
}

void MainWindow::onSongImport()
{
    // Настройки → Песни и сборники: preferred format first, and the folder.
    const QString all = tr("Песни и сборники (*.sps *.spb *.cho *.chordpro *.crd *.txt)");
    const QString collections = tr("Сборники (*.sps *.spb)");
    const QString chordPro = tr("ChordPro (*.cho *.chordpro *.crd)");
    const QString plain = tr("Обычный текст (*.txt)");
    const QString format = AppSettings::value(AppSettings::ImportFormat).toString();
    const QString preferred = format == QLatin1String("cho") ? chordPro : format == QLatin1String("txt") ? plain : collections;
    QString folder = AppSettings::value(AppSettings::ImportDir).toString();
    if (folder.isEmpty())
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString selectedFilter = preferred;
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Выберите файлы песен"), folder,
        QStringList{preferred, all, collections, chordPro, plain, tr("Все файлы (*)")}.join(QStringLiteral(";;")), &selectedFilter);
    if (paths.isEmpty())
        return;
    importSongFiles(paths);
}

void MainWindow::importSongFiles(const QStringList &paths)
{
    // One transaction for the whole import: a commit per song made a big
    // songbook freeze the app for a long time.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QSqlDatabase::database().transaction();
    // Importing the same file twice used to double every song: skip what
    // the library already has (same title and text).
    QSet<QString> existing;
    for (const ContentItem &item : m_repository.fetch(QString(), ContentType::Song, SortOrder::Alphabetical))
        existing.insert(item.title + QLatin1Char('\n') + item.text);
    int imported = 0;
    int skipped = 0;
    const auto addSong = [&](ContentItem &item) {
        const QString key = item.title + QLatin1Char('\n') + item.text;
        if (existing.contains(key)) {
            ++skipped;
            return;
        }
        if (m_repository.add(item)) {
            existing.insert(key);
            ++imported;
        }
    };
    for (const QString &path : paths) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QStringLiteral("sps") || suffix == QStringLiteral("spb")) {
            QString collection;
            const QList<ContentItem> songs = parseSongCollection(path, &collection);
            if (collection.isEmpty())
                collection = QFileInfo(path).completeBaseName();
            for (ContentItem item : songs) {
                item.refBook = collection;
                addSong(item);
            }
            continue;
        }
        if (suffix == QStringLiteral("cho") || suffix == QStringLiteral("chordpro") || suffix == QStringLiteral("crd")) {
            ContentItem item = parseChordPro(path);
            if (!item.text.isEmpty())
                addSong(item);
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
        addSong(item);
    }
    QSqlDatabase::database().commit();
    QApplication::restoreOverrideCursor();

    refreshAll();
    refreshSettingsContext();
    QString message = imported > 0 ? tr("Импортировано песен: %1.").arg(imported)
        : skipped > 0 ? tr("Все песни из файла уже есть в библиотеке.")
                      : tr("Не удалось импортировать ни одной песни.");
    if (skipped > 0)
        message += QLatin1Char('\n') + tr("Уже были в библиотеке и пропущены: %1.").arg(skipped);
    QMessageBox::information(this, tr("Импорт песен"), message);
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
        const QString backupDir = destRoot + QStringLiteral("/sermon-backup-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QDir().mkpath(backupDir);

        const bool ok = QFile::copy(Database::dataDir() + QStringLiteral("/sermon.db"), backupDir + QStringLiteral("/sermon.db"))
            && copyDirRecursively(Database::photosDir(), backupDir + QStringLiteral("/photos"));

        if (ok)
            QMessageBox::information(this, tr("Готово"), tr("Резервная копия сохранена в:\n%1").arg(backupDir));
        else
            QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось создать резервную копию."));
    } else if (box.clickedButton() == importButton) {
        const QString backupDir = QFileDialog::getExistingDirectory(this, tr("Выберите папку с резервной копией"));
        if (backupDir.isEmpty())
            return;
        if (!QFile::exists(backupDir + QStringLiteral("/sermon.db"))) {
            QMessageBox::warning(this, tr("Ошибка"), tr("В выбранной папке не найден файл sermon.db."));
            return;
        }
        const auto reply = QMessageBox::question(this, tr("Импорт"),
            tr("Текущая база данных будет заменена содержимым резервной копии. Продолжить?"));
        if (reply != QMessageBox::Yes)
            return;

        QSqlDatabase::database().close();
        QFile::remove(Database::dataDir() + QStringLiteral("/sermon.db"));
        const bool ok = QFile::copy(backupDir + QStringLiteral("/sermon.db"), Database::dataDir() + QStringLiteral("/sermon.db"))
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

void MainWindow::applySettings(const QStringList &keys)
{
    if (keys == QStringList{QStringLiteral("__restart__")}) {
        restartApp();
        return;
    }
    // Everything that reads settings live picks the new values up here.
    m_presentation->applyDisplaySettings(keys.contains(AppSettings::DisplayScreen) || keys.contains(AppSettings::DisplayResolution)
                                         || keys.contains(AppSettings::AutoFullscreen));
    applyShortcutKeys();
    m_announcementsPanel->applySettings();
    m_biblePanel->reload();
    refreshAll();
}

void MainWindow::restartApp()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
    close();
    QCoreApplication::quit();
}

void MainWindow::onSettings()
{
    refreshSettingsContext();
    m_outerStack->setCurrentIndex(1);
}

void MainWindow::refreshSettingsContext()
{
    SettingsPanel::Context context;
    context.dataDir = Database::dataDir();
    context.obsUrl = m_presentation->server()->displayUrl();
    context.translations = m_bibleRepository.translations();
    context.collections = m_repository.songCollections();
    context.counts = m_repository.categoryCounts();
    context.collectionCounts = m_repository.songCollectionCounts();
    for (const auto &info : m_bibleRepository.translationInfos())
        context.translationInfos.append({info.name, info.books, info.verses});
    m_settingsPanel->setContext(context);
}

bool MainWindow::backupBeforeRemoving()
{
    QString path;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = BackupManager::createBackup(false, &path, &error);
    QApplication::restoreOverrideCursor();
    m_settingsPanel->refreshStatusRows();
    if (ok)
        return true;
    return QMessageBox::warning(this, tr("Резервная копия"),
                                tr("Не удалось сделать резервную копию.\n%1\n\nУдалить всё равно?").arg(error),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

QString MainWindow::defaultSongCollection() const
{
    const QString chosen = AppSettings::value(AppSettings::DefaultCollection).toString().trimmed();
    return chosen.isEmpty() ? tr("Мои песни") : chosen;
}

bool MainWindow::exportSongCollection(const QString &collection, const QString &path, QString *error)
{
    // The same "##" / "#$#" format parseSongCollection() reads, so the file
    // can be imported again (here or on another computer).
    const auto encode = [](QString text) {
        text.replace(QStringLiteral("\r"), QString());
        text.replace(QStringLiteral("\n\n"), QStringLiteral("@$"));
        text.replace(QLatin1Char('\n'), QStringLiteral("@%"));
        return text;
    };
    const auto clean = [](QString field) {
        return field.replace(QStringLiteral("#$#"), QStringLiteral("# $ #")).replace(QLatin1Char('\n'), QLatin1Char(' '));
    };
    QString out = QStringLiteral("##\n##%1\n").arg(collection.isEmpty() ? tr("Без сборника") : collection);
    for (const ContentItem &song : m_repository.songsInCollection(collection)) {
        QStringList fields(10);
        fields[0] = clean(song.refLocation);
        fields[1] = clean(song.title);
        fields[6] = encode(song.text).replace(QStringLiteral("#$#"), QStringLiteral("# $ #"));
        out += fields.join(QStringLiteral("#$#")) + QLatin1Char('\n');
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(out.toUtf8());
    return true;
}

void MainWindow::removeSongCollection(const QString &collection)
{
    int count = 0;
    for (const auto &[name, songs] : m_repository.songCollectionCounts()) {
        if (name == collection)
            count = songs;
    }
    const QString title = collection.isEmpty() ? tr("Без сборника") : collection;
    if (QMessageBox::question(this, tr("Удаление сборника"),
                              tr("Удалить сборник «%1» и все его песни (%2)?\nПеред удалением будет сделана резервная копия базы.")
                                  .arg(title).arg(count)) != QMessageBox::Yes
        || !backupBeforeRemoving())
        return;
    if (!m_repository.removeSongCollection(collection)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось удалить сборник."));
        return;
    }
    if (AppSettings::value(AppSettings::ActiveCollection).toString() == collection)
        AppSettings::setValue(AppSettings::ActiveCollection, QString());
    refreshAll();
    refreshSettingsContext();
}

void MainWindow::removeBibleTranslation(const QString &translation)
{
    if (QMessageBox::question(this, tr("Удаление перевода"),
                              tr("Удалить перевод «%1»?\nПеред удалением будет сделана резервная копия базы.").arg(translation))
            != QMessageBox::Yes
        || !backupBeforeRemoving())
        return;
    if (!m_bibleRepository.removeTranslation(translation)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось удалить перевод."));
        return;
    }
    for (const QString &key : {AppSettings::BibleTranslation, AppSettings::BibleAltTranslation}) {
        if (AppSettings::value(key).toString() == translation)
            AppSettings::setValue(key, QString());
    }
    m_biblePanel->reload();
    refreshSettingsContext();
}

void MainWindow::onBibleImport()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Выберите файл с переводом Библии"), QString(), tr("Библия (*.spb);;Все файлы (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось открыть файл."));
        return;
    }

    const QByteArray raw = file.readAll();
    file.close();

    QString content = QString::fromUtf8(raw);
    if (raw != content.toUtf8()) {
        auto cp1251Decoder = QStringDecoder("windows-1251");
        content = cp1251Decoder(raw);
    }

    QString title;
    struct BookInfo { QString name; int chapters; };
    QHash<int, BookInfo> books;
    struct VerseInfo { int book; int chapter; int verse; QString text; };
    QList<VerseInfo> verses;

    const QStringList lines = content.split(QLatin1Char('\n'));
    int i = 0;
    while (i < lines.size() && lines.at(i).startsWith(QStringLiteral("##"))) {
        if (lines.at(i).startsWith(QStringLiteral("##Title:"))) {
            title = lines.at(i).mid(8).trimmed();
        }
        i++;
    }

    while (i < lines.size() && !lines.at(i).startsWith(QStringLiteral("-----"))) {
        const QString line = lines.at(i).trimmed();
        if (!line.isEmpty()) {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.size() >= 3) {
                books.insert(parts.at(0).toInt(), {parts.at(1), parts.at(2).toInt()});
            }
        }
        i++;
    }
    i++; // skip "-----"

    QHash<QString, QString> merged;
    QList<QString> order;

    for (; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty()) continue;
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() >= 5) {
            const QString key = parts.at(1) + ":" + parts.at(2) + ":" + parts.at(3);
            if (merged.contains(key)) {
                merged[key] += " " + parts.at(4);
            } else {
                merged[key] = parts.at(4);
                order << key;
            }
        }
    }

    for (const QString &key : std::as_const(order)) {
        const QStringList p = key.split(QLatin1Char(':'));
        verses.append({p.at(0).toInt(), p.at(1).toInt(), p.at(2).toInt(), merged[key]});
    }

    if (title.isEmpty() || books.isEmpty() || verses.isEmpty()) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось распознать формат файла."));
        return;
    }

    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();

    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM bible_books WHERE translation = ?"));
    q.addBindValue(title);
    q.exec();

    q.prepare(QStringLiteral("DELETE FROM bible_verses WHERE translation = ?"));
    q.addBindValue(title);
    q.exec();

    q.prepare(QStringLiteral("INSERT INTO bible_books (translation, book_num, book_name, chapter_count) VALUES (?, ?, ?, ?)"));
    QVariantList bTrans, bNum, bName, bChap;
    for (auto it = books.constBegin(); it != books.constEnd(); ++it) {
        bTrans << title;
        bNum << it.key();
        bName << it.value().name;
        bChap << it.value().chapters;
    }
    q.addBindValue(bTrans);
    q.addBindValue(bNum);
    q.addBindValue(bName);
    q.addBindValue(bChap);
    if (!q.execBatch()) {
        db.rollback();
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить книги в базу данных."));
        return;
    }

    q.prepare(QStringLiteral("INSERT INTO bible_verses (translation, book_num, chapter, verse, text) VALUES (?, ?, ?, ?, ?)"));
    QVariantList vTrans, vNum, vChap, vVerse, vText;
    for (const auto &v : std::as_const(verses)) {
        vTrans << title;
        vNum << v.book;
        vChap << v.chapter;
        vVerse << v.verse;
        vText << v.text;
    }
    q.addBindValue(vTrans);
    q.addBindValue(vNum);
    q.addBindValue(vChap);
    q.addBindValue(vVerse);
    q.addBindValue(vText);
    if (!q.execBatch()) {
        db.rollback();
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить стихи в базу данных."));
        return;
    }

    db.commit();
    m_biblePanel->reload();
    refreshSettingsContext();
    QMessageBox::information(this, tr("Импорт Библии"), tr("Успешно импортирован перевод: %1\nСтихов: %2").arg(title).arg(verses.size()));
}

