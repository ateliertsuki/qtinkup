#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSerialPortInfo>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("QTinkup"));

    auto *fileMenu = menuBar()->addMenu(tr("&Menu"));
    auto *aboutAction = fileMenu->addAction(tr("&About..."));
    aboutAction->setMenuRole(QAction::NoRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    auto *exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setMenuRole(QAction::NoRole);
    connect(exitAction, &QAction::triggered, this, [this] {
        cleanup();
        QApplication::quit();
    });

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(6);
    form->setVerticalSpacing(4);

    m_portCombo = new QComboBox(central);
    form->addRow(tr("Serial port:"), m_portCombo);

    auto *fwRow = new QHBoxLayout;
    fwRow->setContentsMargins(0, 0, 0, 0);
    fwRow->setSpacing(4);
    m_fwEdit = new QLineEdit(central);
    m_fwEdit->setPlaceholderText(tr("firmware.hex"));
    m_browseButton = new QPushButton(tr("Browse…"), central);
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::browseFirmware);
    fwRow->addWidget(m_fwEdit, 1);
    fwRow->addWidget(m_browseButton);
    form->addRow(tr("Firmware:"), fwRow);

    layout->addLayout(form);

    m_progressBar = new QProgressBar(central);
    // The native macOS style fails to paint QProgressBar entirely
    // then, render this widget with Fusion instead!
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        fusion->setParent(m_progressBar);
        m_progressBar->setStyle(fusion);
    }
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(tr("Select port and load firmware"), central);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    layout->addSpacing(16);

    m_startButton = new QPushButton(tr("Start"), central);
    m_startButton->setMinimumHeight(m_startButton->sizeHint().height() + 12);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startUpdate);
    layout->addWidget(m_startButton);

    setCentralWidget(central);

    connect(&m_tink, &Tink::progress, this, &MainWindow::updateProgress,
            Qt::QueuedConnection);
    connect(&m_tink, &Tink::writingStarted, this, &MainWindow::writingStarted,
            Qt::QueuedConnection);
    connect(&m_tink, &Tink::finished, this, &MainWindow::updateFinished);

    connect(m_portCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::refreshStatus);
    connect(m_fwEdit, &QLineEdit::textChanged, this, &MainWindow::refreshStatus);

    populatePorts();

    // Fixed-size window: no resizing and no maximize button
    setFixedSize(sizeHint().expandedTo(QSize(360, 0)));
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);

    // Start centered on the screen
    if (QScreen *screen = QGuiApplication::primaryScreen())
        setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                        size(), screen->availableGeometry()));
}

void MainWindow::populatePorts()
{
    m_portCombo->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        QString label = port.portName();
        if (!port.description().isEmpty())
            label += QStringLiteral(" — %1").arg(port.description());
        m_portCombo->addItem(label, port.portName());

        // tinkup.py targets FTDI devices, so preselect one when present
        if (port.manufacturer().contains(QStringLiteral("FTDI"), Qt::CaseInsensitive))
            m_portCombo->setCurrentIndex(m_portCombo->count() - 1);
    }

    if (m_portCombo->count() == 0)
        qInfo() << "No serial ports found";
}

void MainWindow::browseFirmware()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select firmware"), QString(),
        tr("Firmware HEX files (*.hex);;All files (*)"));
    if (!path.isEmpty())
        m_fwEdit->setText(path);
}

void MainWindow::startUpdate()
{
    if (m_tink.isRunning())
        return;

    if (m_portCombo->currentIndex() < 0) {
        qInfo() << "No serial port selected";
        return;
    }
    if (m_fwEdit->text().isEmpty()) {
        qInfo() << "No firmware file selected";
        return;
    }

    m_startButton->setEnabled(false);
    m_portCombo->setEnabled(false);
    m_fwEdit->setEnabled(false);
    m_browseButton->setEnabled(false);
    m_progressBar->setValue(0);

    m_tink.start(m_fwEdit->text(), m_portCombo->currentData().toString());
}

void MainWindow::refreshStatus()
{
    if (m_tink.isRunning())
        return;
    if (m_portCombo->currentIndex() >= 0 && !m_fwEdit->text().isEmpty())
        m_statusLabel->setText(tr("Ready!"));
    else
        m_statusLabel->setText(tr("Select port and load firmware"));
}

void MainWindow::writingStarted()
{
    // should be reached once per update
    m_statusLabel->setText(tr("Writing..."));
}

void MainWindow::updateProgress(int percent)
{
    m_progressBar->setValue(percent);
}

void MainWindow::updateFinished(bool success)
{
    if (success) {
        m_progressBar->setValue(100);
        m_statusLabel->setText(tr("Success!"));
    } else {
        m_statusLabel->setText(tr("Error occurred. Check logs."));
    }
    m_startButton->setEnabled(true);
    m_portCombo->setEnabled(true);
    m_fwEdit->setEnabled(true);
    m_browseButton->setEnabled(true);
}

void MainWindow::showAbout()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About QTinkup"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    auto *label = new QLabel(
        tr("<b>QTinkup %1</b><br><br>"
           "<a href=\"https://www.retrotink.com\">RetroTINK</a> firmware updater.<br><br>"
           "Original <b>tinkup.py script</b> by Ryan Mullen ( <a href=\"https://github.com/rmull/tinkup\">rmull Github</a> )<br>"
           "<b>Qt 6.x fork</b> by Roberto M. ( <a href=\"https://github.com/ateliertsuki\">ateliertsuki Github</a> )")
            .arg(QApplication::applicationVersion()),
        &dialog);
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    layout->addWidget(label);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::cleanup()
{
    qInfo() << "Quitting";
    m_tink.cleanup();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    cleanup();
    event->accept();
}
