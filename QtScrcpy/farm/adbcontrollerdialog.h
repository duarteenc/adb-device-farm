#ifndef FARM_ADBCONTROLLERDIALOG_H
#define FARM_ADBCONTROLLERDIALOG_H

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QPushButton;
class QListWidget;
class QTabWidget;
class QTextEdit;

/**
 * ADB Controller dialog — execute raw adb commands against one or more devices.
 *
 * Features:
 * - Command input field with "Ejecutar" button
 * - Three tabs: saved commands, history, command log
 * - Save frequently-used commands with custom names
 * - Persistent history and saved commands
 */
class AdbControllerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AdbControllerDialog(const QStringList &targetSerials, QWidget *parent = nullptr);
    ~AdbControllerDialog() override;

private slots:
    void executeCommand();
    void saveCurrentCommand();
    void deleteSelectedCommand();
    void loadCommand(int row);
    void clearHistory();
    void clearLog();

private:
    void loadSettings();
    void saveSettings();
    void addToHistory(const QString &command);
    void addToLog(const QString &text);
    void executeAdbCommand(const QString &command);

    QStringList m_targetSerials;
    QLineEdit *m_commandInput = nullptr;
    QPushButton *m_executeButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    // Tab 1: Saved commands
    QListWidget *m_savedCommandsList = nullptr;
    QPushButton *m_deleteCommandButton = nullptr;
    QHash<QString, QString> m_savedCommands;  // name -> command

    // Tab 2: History
    QListWidget *m_historyList = nullptr;
    QPushButton *m_clearHistoryButton = nullptr;
    QStringList m_history;

    // Tab 3: Log
    QTextEdit *m_logView = nullptr;
    QPushButton *m_clearLogButton = nullptr;
};

#endif // FARM_ADBCONTROLLERDIALOG_H
