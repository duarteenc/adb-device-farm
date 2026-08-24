#ifndef FARM_UI_TEXTSENDDIALOG_H
#define FARM_UI_TEXTSENDDIALOG_H

#include <QDialog>
#include <QStringList>

#include "storage/repositories.h"

class QPlainTextEdit;
class QListWidget;
class QLineEdit;
class QComboBox;
class QLabel;

namespace farm {

/**
 * Send text to devices + the text-template manager (name / category / content /
 * shortcut). "Send" pushes the text through the scrcpy channel (Unicode) on
 * mirrored devices and `input text` (ASCII) elsewhere.
 */
class TextSendDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TextSendDialog(const QStringList &targets, QWidget *parent = nullptr);
    QString text() const;

signals:
    void sendRequested(const QStringList &targets, const QString &text);
    void clipboardRequested(const QStringList &targets, const QString &text);

private:
    void reloadTemplates();
    void saveTemplate();
    void deleteTemplate();
    void useTemplate();

    QStringList m_targets;
    QPlainTextEdit *m_text = nullptr;
    QListWidget *m_templates = nullptr;
    QLineEdit *m_tplName = nullptr;
    QComboBox *m_tplCategory = nullptr;
    QLineEdit *m_tplShortcut = nullptr;
    QLabel *m_targetLabel = nullptr;
    QList<TextTemplate> m_rows;
};

} // namespace farm

#endif // FARM_UI_TEXTSENDDIALOG_H
