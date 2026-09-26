#pragma once

#include <QWidget>

// "Справка" screen: quick-start category cards + an FAQ accordion with real
// answers about Sermon's actual features (hotkeys, OBS, bible search, backup).
class HelpPanel : public QWidget {
    Q_OBJECT
public:
    explicit HelpPanel(QWidget *parent = nullptr);

private:
    void buildUi();
};
