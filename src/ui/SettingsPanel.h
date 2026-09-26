#pragma once

#include "core/ContentItem.h"

#include <QMap>
#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QComboBox;

// "Настройки" (design.pen MYHVU … IvONG): a section nav on the left and one
// page per section. Every control edits a pending value; nothing is written
// until "Сохранить" (then settingsSaved() tells the app what changed),
// "Отмена" drops the pending edits and "Сбросить" puts the current page back
// to its defaults (still to be saved).
class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget *parent = nullptr);

    // Facts the pages show that live elsewhere in the app.
    struct Context {
        QString dataDir;
        QString obsUrl;
        QStringList translations;
        QStringList collections;
        QMap<ContentType, int> counts;
        // "Загруженные сборники" / "Загруженные переводы Библии".
        QList<QPair<QString, int>> collectionCounts; // "" = without a songbook
        struct Translation { QString name; int books = 0; int verses = 0; };
        QList<Translation> translationInfos;
    };
    void setContext(const Context &context);

    // OBS connection test result ("Проверить подключение").
    void setObsStatus(bool connected, const QString &text, const QStringList &scenes);
    // Refreshes the "Последняя копия" / "Последняя оптимизация" rows.
    void refreshStatusRows();

    bool hasPendingChanges() const { return !m_pending.isEmpty(); }
    void showPage(int index);

signals:
    void settingsSaved(const QStringList &keys);
    void importExportRequested();
    void songImportRequested();
    void bibleImportRequested();
    // Trash buttons in the loaded songbooks / translations lists.
    void removeCollectionRequested(const QString &collection);
    void removeTranslationRequested(const QString &translation);
    // ⤓ in the loaded songbooks list: save that songbook as a .sps file.
    void exportCollectionRequested(const QString &collection);
    void backupNowRequested();
    void restoreRequested();
    void optimizeRequested();
    void obsTestRequested(const QString &host, int port, const QString &password);

private:
    struct Binding {
        QString key;
        int page = 0;
        std::function<void(const QVariant &)> show;
    };

    void buildUi();
    QWidget *buildGeneralPage();
    QWidget *buildShowPage();
    QWidget *buildBiblePage();
    QWidget *buildSongsPage();
    QWidget *buildBibleLibraryPage();
    QWidget *buildSongsLibraryPage();
    QWidget *buildTextDisplayPage(ContentType type);
    QWidget *buildDatabasePage();
    QWidget *buildHotkeysPage();
    QWidget *buildObsPage();
    QWidget *buildBackupPage();
    QWidget *buildAppearancePage();

    // ---- pending-value plumbing ----
    QVariant current(const QString &key) const;
    int bind(const QString &key, const std::function<void(const QVariant &)> &show);
    void setPending(const QString &key, const QVariant &value, int sourceBinding = -1);
    void refreshBindings();
    void updateActions();
    void save();
    void cancel();
    void resetPage();

    // ---- bound controls ----
    QComboBox *combo(const QString &key, const QList<QPair<QString, QVariant>> &options, int minWidth = 180);
    QWidget *toggle(const QString &key);
    QWidget *toggle(const QString &key, const std::function<bool(const QVariant &)> &read,
                    const std::function<QVariant(bool)> &write);
    QWidget *keyField(const QString &key);

    QStackedWidget *m_stack = nullptr;
    class QButtonGroup *m_navGroup = nullptr;
    int m_buildingPage = 0;
    QList<Binding> m_bindings;
    QVariantMap m_pending;
    Context m_context;

    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    // Rows filled from the context / status.
    QLabel *m_dataDirLabel = nullptr;
    QLabel *m_dbSizeLabel = nullptr;
    QLabel *m_lastOptimizeLabel = nullptr;
    QLabel *m_countsLabel = nullptr;
    QLabel *m_obsUrlLabel = nullptr;
    QLabel *m_lastBackupLabel = nullptr;
    QLabel *m_backupStatusLabel = nullptr;
    QLabel *m_backupDirLabel = nullptr;
    QLabel *m_obsStatusLabel = nullptr;
    QComboBox *m_screenBox = nullptr;
    QComboBox *m_translationBox = nullptr;
    QComboBox *m_altTranslationBox = nullptr;
    QComboBox *m_collectionBox = nullptr;
    QComboBox *m_sceneBox = nullptr;
    QList<std::function<void()>> m_contextRefreshers;
    // design.pen "Загруженные сборники и переводы": one row per songbook or
    // translation with its size and a delete button.
    QWidget *loadedList(bool bible);
    QList<std::function<void()>> m_previewRefreshers;
};
