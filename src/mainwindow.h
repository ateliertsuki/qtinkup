#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "tink.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void browseFirmware();
    void startUpdate();
    void refreshStatus();
    void writingStarted();
    void updateProgress(int percent);
    void updateFinished(bool success);
    void showAbout();

private:
    void populatePorts();
    void cleanup();

    Tink m_tink;

    QComboBox *m_portCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_fwEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_startButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
};

#endif // MAINWINDOW_H
