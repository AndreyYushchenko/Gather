#pragma once

#include "core/ContentItem.h"

#include <QDialog>

class QComboBox;
class QStackedWidget;
class QLineEdit;
class QTextEdit;
class QCheckBox;
class QDateEdit;
class QLabel;
class QPushButton;

// Add/edit form. When constructed with an existing item, the category is
// locked; when adding a new item, the user picks the category first.
class ItemEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit ItemEditDialog(QWidget *parent = nullptr);
    explicit ItemEditDialog(const ContentItem &existing, QWidget *parent = nullptr);

    ContentItem item() const;
    void setDefaultType(ContentType type);
    // Song page "Сборник": the songbooks to offer and the one a new song gets.
    void setSongCollections(const QStringList &collections, const QString &defaultCollection);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onTypeChanged(int index);
    void browseForPhoto();
    void onExpiryToggled(bool enabled);
    void onBackgroundTypeChanged(int index);
    void browseForBackground();
    void downloadYoutubeBackground();

private:
    void buildUi();
    void applyExisting();
    void setPhotoPath(const QString &sourcePath);

    bool m_isNew = true;
    ContentItem m_original;
    QString m_pendingPhotoSource; // absolute source path selected by the user, copied on accept()

    QComboBox *m_typeCombo = nullptr;
    QStackedWidget *m_stack = nullptr;

    // Song page
    QLineEdit *m_songTitle = nullptr;
    QLineEdit *m_songNumber = nullptr;
    class QComboBox *m_songCollection = nullptr;
    QTextEdit *m_songText = nullptr;

    // Bible verse page
    QLineEdit *m_verseBook = nullptr;
    QLineEdit *m_verseLocation = nullptr;
    QTextEdit *m_verseText = nullptr;

    // Announcement page
    QLineEdit *m_announcementTitle = nullptr;
    QTextEdit *m_announcementText = nullptr;
    QCheckBox *m_announcementHasExpiry = nullptr;
    QDateEdit *m_announcementExpiry = nullptr;

    // Photo page
    QLabel *m_photoPreview = nullptr;
    QLineEdit *m_photoCaption = nullptr;

    // Slide background (shared by Song/Verse/Announcement pages; hidden for Photo)
    QWidget *m_backgroundGroup = nullptr;
    QComboBox *m_backgroundTypeCombo = nullptr;
    QLabel *m_backgroundPathLabel = nullptr;
    QPushButton *m_backgroundBrowseButton = nullptr;
    QLineEdit *m_backgroundYoutubeUrl = nullptr;
    QPushButton *m_backgroundYoutubeDownloadButton = nullptr;
    QString m_pendingBackgroundSource;      // local file selected by the user, copied on accept()
    bool m_pendingBackgroundIsDownload = false; // already sitting in Database::backgroundsDir(), no copy needed

    void accept() override;
};
