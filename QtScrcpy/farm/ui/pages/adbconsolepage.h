#ifndef FARM_UI_ADBCONSOLEPAGE_H
#define FARM_UI_ADBCONSOLEPAGE_H

#include <QStringList>
#include <QWidget>

#include "storage/repositories.h"

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QLabel;
class QPushButton;

namespace farm {

/**
 * Integrated ADB console: run a command (`shell …`, `reboot`, `pull …`) on one
 * device, the selection, a group or every online device; per-device stdout /
 * stderr; history; saved presets with categories.
 */
class AdbConsolePage : public QWidget
{
    Q_OBJECT
public:
    explicit AdbConsolePage(QWidget *parent = nullptr);
    void setTargets(const QStringList &ids);

private:
    QStringList resolveTargets() const;
    void execute();
    void reloadSaved();
    void savePreset();
    void deletePreset();
    void appendOutput(const QString &text, const QColor &color);
    void rebuildTargetCombo();

    QStringList m_selection;
    QComboBox *m_mode = nullptr;
    QComboBox *m_single = nullptr;
    QComboBox *m_group = nullptr;
    QLineEdit *m_command = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QListWidget *m_history = nullptr;
    QListWidget *m_saved = nullptr;
    QComboBox *m_savedCategory = nullptr;
    QLineEdit *m_presetName = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_runBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QList<SavedCommand> m_presets;
    QStringList m_historyItems;
};

} // namespace farm

#endif // FARM_UI_ADBCONSOLEPAGE_H
