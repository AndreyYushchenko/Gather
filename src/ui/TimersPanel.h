#pragma once

#include "display/TimerScreen.h"

#include <QList>
#include <QWidget>
#include <functional>

class PresentationController;
class TimerEngine;
class TimerRenderWidget;
class ToggleSwitch;
class ClickFrame;
class ChoiceButton;
class SegmentedControl;
class DropdownField;
class CheckRow;
class BackgroundThumb;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimeEdit;
class QDateTimeEdit;
class QSlider;
class QVBoxLayout;

// "Таймеры и время" (design.pen "Sermon App - Timers v2 (конструктор)"):
// the "Мои экраны" list, the selected screen's preview + controls, and its
// settings in four sections — Что показывать / Текст / Оформление /
// Поведение. All logic lives in TimerEngine; this class only reflects the
// selected screen into controls and writes edits back.
class TimersPanel : public QWidget {
    Q_OBJECT
public:
    explicit TimersPanel(QWidget *parent = nullptr);

    void setController(PresentationController *controller);
    // The selected timer screen to the projector.
    void goLiveSelected();

private:
    // ---- building ----
    QWidget *buildHeader();
    QWidget *buildScreensHeader();
    QWidget *buildEditorCard();
    QWidget *buildContentSection();
    QWidget *buildTextSection();
    QWidget *buildStyleSection();
    QWidget *buildBehaviourSection();
    QFrame *makeSection(const QString &title, const QString &icon, QVBoxLayout **layout);
    QWidget *makeRow(const QString &label, QWidget *body);

    // ---- refreshing ----
    void rebuildCards();
    void refreshCards();
    void refreshEditor();
    void refreshState();

    // ---- actions ----
    TimerScreen current() const;
    void modify(const std::function<void(TimerScreen &)> &change);
    void select(const QString &id);
    void addScreen(TimerScreenType type);
    void duplicateScreen(const QString &id);
    void deleteScreen(const QString &id);
    void showPreview();
    void showOnScreenMenu(QWidget *anchor);
    void showCardMenu(const QString &id, const QPoint &pos);
    void chooseCustomBackground();
    void chooseWarnMinutes(bool yellow, QWidget *anchor);

    TimerEngine *m_engine = nullptr;
    PresentationController *m_controller = nullptr;
    QString m_selectedId;
    bool m_showNames = true;
    bool m_updating = false; // true while refreshEditor() fills controls

    // Screens
    QLabel *m_screensHeading = nullptr;
    QWidget *m_cardsRow = nullptr;
    struct Card {
        QString id;
        ClickFrame *frame;
        TimerRenderWidget *render;
        QLabel *caption;
        QLabel *number;
    };
    QList<Card> m_cards;

    // Editor card
    QLabel *m_typeBadgeIcon = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_summary = nullptr;
    ClickFrame *m_deleteButton = nullptr;
    TimerRenderWidget *m_preview = nullptr;
    QFrame *m_stateBox = nullptr;
    QLabel *m_stateDot = nullptr;
    QLabel *m_stateTitle = nullptr;
    QLabel *m_stateDetail = nullptr;
    QWidget *m_mainControls = nullptr;
    QWidget *m_adjustBlock = nullptr;

    // Что показывать
    QList<ChoiceButton *> m_typeButtons;
    QWidget *m_targetRow = nullptr;
    SegmentedControl *m_targetSeg = nullptr;
    QTimeEdit *m_durationEdit = nullptr;
    QTimeEdit *m_targetTimeEdit = nullptr;
    DropdownField *m_repeatField = nullptr;
    QDateTimeEdit *m_targetDateEdit = nullptr;
    QLabel *m_targetHint = nullptr;
    QWidget *m_formatRow = nullptr;
    SegmentedControl *m_formatSeg = nullptr;
    QLabel *m_formatHint = nullptr;
    QWidget *m_faceRow = nullptr;
    SegmentedControl *m_faceSeg = nullptr;
    QWidget *m_clockOptionsRow = nullptr;
    ToggleSwitch *m_use24hToggle = nullptr;
    ToggleSwitch *m_secondsToggle = nullptr;
    QWidget *m_timeZoneRow = nullptr;
    DropdownField *m_timeZoneField = nullptr;

    // Текст
    QWidget *m_messageBlock = nullptr;
    QPlainTextEdit *m_messageEdit = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_subtitleEdit = nullptr;
    QWidget *m_endMessageColumn = nullptr;
    QLineEdit *m_endMessageEdit = nullptr;

    // Оформление
    QList<BackgroundThumb *> m_backgroundThumbs;
    DropdownField *m_fontField = nullptr;
    DropdownField *m_colorField = nullptr;
    DropdownField *m_sizeField = nullptr;
    DropdownField *m_alignField = nullptr;
    QSlider *m_dimSlider = nullptr;
    QLabel *m_dimLabel = nullptr;
    ToggleSwitch *m_shadowToggle = nullptr;
    QWidget *m_progressRow = nullptr;
    ToggleSwitch *m_progressToggle = nullptr;

    // Поведение
    QWidget *m_endActionRow = nullptr;
    SegmentedControl *m_endActionSeg = nullptr;
    DropdownField *m_switchField = nullptr;
    QList<QString> m_switchTargets;
    QWidget *m_soundRow = nullptr;
    ToggleSwitch *m_soundToggle = nullptr;
    DropdownField *m_soundField = nullptr;
    QWidget *m_warnRow = nullptr;
    ToggleSwitch *m_warnToggle = nullptr;
    QLabel *m_warnYellowLabel = nullptr;
    QLabel *m_warnRedLabel = nullptr;
    QWidget *m_autoStartRow = nullptr;
    ToggleSwitch *m_autoStartToggle = nullptr;
    ToggleSwitch *m_scheduleToggle = nullptr;
    QTimeEdit *m_scheduleTimeEdit = nullptr;
    DropdownField *m_scheduleDayField = nullptr;
};
