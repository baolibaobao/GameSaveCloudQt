#include "tray/TrayController.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

TrayController::TrayController(QObject *parent)
    : QObject(parent)
{
    m_menu = new QMenu();
    m_showAction = m_menu->addAction(QStringLiteral("显示主窗口"));
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QStringLiteral("退出"));

    connect(m_showAction, &QAction::triggered, this, &TrayController::showMainWindow);
    connect(m_quitAction, &QAction::triggered, this, &TrayController::requestQuit);
    connect(&m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    showMainWindow();
                }
            });

    m_trayIcon.setIcon(createFallbackIcon());
    m_trayIcon.setToolTip(QStringLiteral("GameSaveCloud-Qt 正在后台监控游戏存档"));
    m_trayIcon.setContextMenu(m_menu);

    if (available()) {
        m_trayIcon.show();
    }
}

TrayController::~TrayController()
{
    m_trayIcon.hide();
    delete m_menu;
}

bool TrayController::available() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

bool TrayController::quitRequested() const
{
    return m_quitRequested;
}

void TrayController::notifyHiddenToTray()
{
    if (!available() || !m_trayIcon.isVisible() || m_hiddenMessageShown) {
        return;
    }

    m_hiddenMessageShown = true;
    m_trayIcon.showMessage(
        QStringLiteral("GameSaveCloud-Qt 已在后台运行"),
        QStringLiteral("关闭窗口不会退出程序。自动同步和游戏进程监控会继续工作，可从托盘图标恢复窗口或退出。"),
        QSystemTrayIcon::Information,
        5000);
}

void TrayController::requestQuit()
{
    if (m_quitRequested) {
        return;
    }

    m_quitRequested = true;
    emit quitRequestedChanged();
    QApplication::quit();
}

QIcon TrayController::createFallbackIcon() const
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#0078D4")));
    painter.drawRoundedRect(QRectF(6, 6, 52, 52), 14, 14);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setFamily(QStringLiteral("Segoe UI"));
    font.setBold(true);
    font.setPixelSize(30);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("G"));
    painter.end();

    return QIcon(pixmap);
}

void TrayController::showMainWindow()
{
    emit showMainWindowRequested();
}
