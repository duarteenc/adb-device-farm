#include "adbcontrollerdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLabel>
#include <QInputDialog>
#include <QSettings>
#include <QProcess>
#include <QDateTime>
#include <QMessageBox>
#include <QIcon>

AdbControllerDialog::AdbControllerDialog(const QStringList &targetSerials, QWidget *parent)
    : QDialog(parent)
    , m_targetSerials(targetSerials)
{
    setWindowTitle(tr("Comando ADB"));
    setMinimumSize(600, 500);
    resize(700, 600);

    // Main layout
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Top section: device info
    QString devicesText = targetSerials.size() == 1
                              ? targetSerials.first()
                              : tr("%1 dispositivos seleccionados").arg(targetSerials.size());
    auto *deviceLabel = new QLabel(devicesText, this);
    deviceLabel->setStyleSheet("QLabel { font-weight: bold; padding: 4px; }");
    mainLayout->addWidget(deviceLabel);

    // Command input section
    auto *inputLayout = new QHBoxLayout();
    m_commandInput = new QLineEdit(this);
    m_commandInput->setPlaceholderText(tr("Command (e.g. adb reboot)"));
    m_commandInput->setStyleSheet("QLineEdit { padding: 6px; font-size: 13px; }");
    connect(m_commandInput, &QLineEdit::returnPressed, this, &AdbControllerDialog::executeCommand);

    m_executeButton = new QPushButton(tr("Ejecutar"), this);
    m_executeButton->setStyleSheet(
        "QPushButton { background-color: #5865F2; color: white; padding: 6px 16px; "
        "font-weight: bold; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4752C4; }"
        "QPushButton:pressed { background-color: #3C45A5; }");
    connect(m_executeButton, &QPushButton::clicked, this, &AdbControllerDialog::executeCommand);

    inputLayout->addWidget(m_commandInput, 1);
    inputLayout->addWidget(m_executeButton);
    mainLayout->addLayout(inputLayout);

    // Name input + Save button
    auto *saveLayout = new QHBoxLayout();
    auto *nameLabel = new QLabel(tr("Name"), this);
    auto *nameInput = new QLineEdit(this);
    nameInput->setPlaceholderText(tr("Nombre para guardar"));
    nameInput->setObjectName("nameInput");

    m_saveButton = new QPushButton(tr("Guardar"), this);
    m_saveButton->setStyleSheet(
        "QPushButton { background-color: #57F287; color: black; padding: 6px 16px; "
        "border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3BA55D; color: white; }");
    connect(m_saveButton, &QPushButton::clicked, this, &AdbControllerDialog::saveCurrentCommand);

    saveLayout->addWidget(nameLabel);
    saveLayout->addWidget(nameInput, 1);
    saveLayout->addWidget(m_saveButton);
    mainLayout->addLayout(saveLayout);

    // Tab widget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #2b2d31; background: #1e1f22; }"
        "QTabBar::tab { background: #2b2d31; color: #b5bac1; padding: 8px 16px; "
        "border: none; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #404249; color: white; }"
        "QTabBar::tab:hover:!selected { background: #35373c; }");

    // Tab 1: Lista de comandos
    auto *savedTab = new QWidget();
    auto *savedLayout = new QVBoxLayout(savedTab);
    savedLayout->setContentsMargins(0, 0, 0, 0);

    m_savedCommandsList = new QListWidget(this);
    m_savedCommandsList->setStyleSheet(
        "QListWidget { background: #1e1f22; color: #dbdee1; border: 1px solid #2b2d31; "
        "padding: 4px; }"
        "QListWidget::item { padding: 6px; border-radius: 3px; }"
        "QListWidget::item:selected { background: #404249; }"
        "QListWidget::item:hover { background: #2b2d31; }");
    connect(m_savedCommandsList, &QListWidget::currentRowChanged, this,
            &AdbControllerDialog::loadCommand);

    auto *savedButtonLayout = new QHBoxLayout();
    m_deleteCommandButton = new QPushButton(tr("🗑"), this);
    m_deleteCommandButton->setFixedSize(40, 32);
    m_deleteCommandButton->setStyleSheet(
        "QPushButton { background: #2b2d31; color: #ed4245; border: none; "
        "border-radius: 4px; font-size: 16px; }"
        "QPushButton:hover { background: #ed4245; color: white; }");
    connect(m_deleteCommandButton, &QPushButton::clicked, this,
            &AdbControllerDialog::deleteSelectedCommand);

    savedButtonLayout->addStretch();
    savedButtonLayout->addWidget(m_deleteCommandButton);

    savedLayout->addWidget(m_savedCommandsList);
    savedLayout->addLayout(savedButtonLayout);
    m_tabWidget->addTab(savedTab, tr("Lista de comandos"));

    // Tab 2: Historial
    auto *historyTab = new QWidget();
    auto *historyLayout = new QVBoxLayout(historyTab);
    historyLayout->setContentsMargins(0, 0, 0, 0);

    m_historyList = new QListWidget(this);
    m_historyList->setStyleSheet(m_savedCommandsList->styleSheet());
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) {
            m_commandInput->setText(item->text());
        }
    });

    m_clearHistoryButton = new QPushButton(tr("Clear"), this);
    m_clearHistoryButton->setStyleSheet(
        "QPushButton { background: #2b2d31; color: #b5bac1; padding: 4px 12px; "
        "border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #ed4245; color: white; }");
    connect(m_clearHistoryButton, &QPushButton::clicked, this,
            &AdbControllerDialog::clearHistory);

    auto *historyButtonLayout = new QHBoxLayout();
    historyButtonLayout->addStretch();
    historyButtonLayout->addWidget(m_clearHistoryButton);

    historyLayout->addWidget(m_historyList);
    historyLayout->addLayout(historyButtonLayout);
    m_tabWidget->addTab(historyTab, tr("Historial"));

    // Tab 3: Registro de comandos
    auto *logTab = new QWidget();
    auto *logLayout = new QVBoxLayout(logTab);
    logLayout->setContentsMargins(0, 0, 0, 0);

    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(
        "QTextEdit { background: #1e1f22; color: #dbdee1; border: 1px solid #2b2d31; "
        "font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; padding: 8px; }");

    m_clearLogButton = new QPushButton(tr("Clear"), this);
    m_clearLogButton->setStyleSheet(m_clearHistoryButton->styleSheet());
    connect(m_clearLogButton, &QPushButton::clicked, this, &AdbControllerDialog::clearLog);

    auto *logButtonLayout = new QHBoxLayout();
    logButtonLayout->addStretch();
    logButtonLayout->addWidget(m_clearLogButton);

    logLayout->addWidget(m_logView);
    logLayout->addLayout(logButtonLayout);
    m_tabWidget->addTab(logTab, tr("Registro de comandos"));

    mainLayout->addWidget(m_tabWidget, 1);

    // Load saved settings
    loadSettings();
}

AdbControllerDialog::~AdbControllerDialog()
{
    saveSettings();
}

void AdbControllerDialog::executeCommand()
{
    QString cmd = m_commandInput->text().trimmed();
    if (cmd.isEmpty()) {
        return;
    }

    addToHistory(cmd);
    executeAdbCommand(cmd);
}

void AdbControllerDialog::saveCurrentCommand()
{
    QString cmd = m_commandInput->text().trimmed();
    if (cmd.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Ingrese un comando primero."));
        return;
    }

    auto *nameInput = findChild<QLineEdit *>("nameInput");
    QString name = nameInput ? nameInput->text().trimmed() : QString();

    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Ingrese un nombre para el comando."));
        return;
    }

    m_savedCommands.insert(name, cmd);
    saveSettings();

    // Rebuild saved commands list
    m_savedCommandsList->clear();
    const QStringList keys = m_savedCommands.keys();
    for (const QString &key : keys) {
        auto *item = new QListWidgetItem(key, m_savedCommandsList);
        item->setData(Qt::UserRole, m_savedCommands.value(key));
    }

    if (nameInput) {
        nameInput->clear();
    }
    m_commandInput->clear();

    addToLog(tr("[Guardado] %1: %2").arg(name, cmd));
}

void AdbControllerDialog::deleteSelectedCommand()
{
    QListWidgetItem *item = m_savedCommandsList->currentItem();
    if (!item) {
        return;
    }

    const QString name = item->text();
    m_savedCommands.remove(name);
    delete item;
    saveSettings();

    addToLog(tr("[Eliminado] %1").arg(name));
}

void AdbControllerDialog::loadCommand(int row)
{
    if (row < 0 || row >= m_savedCommandsList->count()) {
        return;
    }

    QListWidgetItem *item = m_savedCommandsList->item(row);
    if (item) {
        const QString cmd = item->data(Qt::UserRole).toString();
        m_commandInput->setText(cmd);
    }
}

void AdbControllerDialog::clearHistory()
{
    m_history.clear();
    m_historyList->clear();
    saveSettings();
    addToLog(tr("[Historial limpiado]"));
}

void AdbControllerDialog::clearLog()
{
    m_logView->clear();
}

void AdbControllerDialog::loadSettings()
{
    QSettings settings("AdbDeviceFarm", "AdbController");

    // Load saved commands
    const int size = settings.beginReadArray("savedCommands");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString name = settings.value("name").toString();
        const QString cmd = settings.value("command").toString();
        if (!name.isEmpty() && !cmd.isEmpty()) {
            m_savedCommands.insert(name, cmd);
        }
    }
    settings.endArray();

    // Populate saved commands list
    const QStringList keys = m_savedCommands.keys();
    for (const QString &key : keys) {
        auto *item = new QListWidgetItem(key, m_savedCommandsList);
        item->setData(Qt::UserRole, m_savedCommands.value(key));
    }

    // Load history
    m_history = settings.value("history").toStringList();
    for (const QString &cmd : m_history) {
        m_historyList->addItem(cmd);
    }
}

void AdbControllerDialog::saveSettings()
{
    QSettings settings("AdbDeviceFarm", "AdbController");

    // Save commands
    settings.beginWriteArray("savedCommands");
    int i = 0;
    QHashIterator<QString, QString> it(m_savedCommands);
    while (it.hasNext()) {
        it.next();
        settings.setArrayIndex(i++);
        settings.setValue("name", it.key());
        settings.setValue("command", it.value());
    }
    settings.endArray();

    // Save history (limit to last 100)
    if (m_history.size() > 100) {
        m_history = m_history.mid(m_history.size() - 100);
    }
    settings.setValue("history", m_history);
}

void AdbControllerDialog::addToHistory(const QString &command)
{
    if (!m_history.contains(command)) {
        m_history.append(command);
        m_historyList->addItem(command);
    }
}

void AdbControllerDialog::addToLog(const QString &text)
{
    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logView->append(QString("[%1] %2").arg(timestamp, text));
}

void AdbControllerDialog::executeAdbCommand(const QString &command)
{
    // Strip "adb" prefix if user typed it
    QString cmd = command;
    if (cmd.startsWith("adb ", Qt::CaseInsensitive)) {
        cmd = cmd.mid(4).trimmed();
    }

    addToLog(tr("► Ejecutando: adb %1").arg(cmd));

    for (const QString &serial : m_targetSerials) {
        QProcess *process = new QProcess(this);
        process->setProgram("adb");

        QStringList args;
        args << "-s" << serial;
        args.append(cmd.split(' ', Qt::SkipEmptyParts));
        process->setArguments(args);

        connect(process, &QProcess::finished, this,
                [this, serial, cmd, process](int exitCode, QProcess::ExitStatus status) {
                    const QString output = QString::fromUtf8(process->readAllStandardOutput());
                    const QString error = QString::fromUtf8(process->readAllStandardError());

                    if (status == QProcess::NormalExit && exitCode == 0) {
                        addToLog(tr("✓ [%1] OK").arg(serial));
                        if (!output.isEmpty()) {
                            addToLog(tr("  Output: %1").arg(output.trimmed()));
                        }
                    } else {
                        addToLog(tr("✗ [%1] Error (exit %2)").arg(serial).arg(exitCode));
                        if (!error.isEmpty()) {
                            addToLog(tr("  %1").arg(error.trimmed()));
                        }
                    }

                    process->deleteLater();
                });

        process->start();
    }

    // Switch to log tab to show output
    m_tabWidget->setCurrentIndex(2);
}
