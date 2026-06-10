#pragma once

#include <QIcon>
#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;

class TrayController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool quitRequested READ quitRequested NOTIFY quitRequestedChanged)

public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;

    bool available() const;
    bool quitRequested() const;

    Q_INVOKABLE void notifyHiddenToTray();
    Q_INVOKABLE void requestQuit();

signals:
    void showMainWindowRequested();
    void quitRequestedChanged();

private:
    QIcon createFallbackIcon() const;
    void showMainWindow();

    QSystemTrayIcon m_trayIcon;
    QMenu *m_menu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_quitAction = nullptr;
    bool m_quitRequested = false;
    bool m_hiddenMessageShown = false;
};
