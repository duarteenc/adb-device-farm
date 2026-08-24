#include "textsenddialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/farmtheme.h"

namespace farm {

TextSendDialog::TextSendDialog(const QStringList &targets, QWidget *parent)
    : QDialog(parent)
    , m_targets(targets)
{
    setWindowTitle(tr("Send text"));
    resize(720, 440);
    theme::apply(this);

    auto *root = new QHBoxLayout(this);

    // ---- left: text ----
    auto *left = new QVBoxLayout();
    m_targetLabel = new QLabel(tr("%n target device(s)", nullptr, static_cast<int>(targets.size())), this);
    m_targetLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    m_text = new QPlainTextEdit(this);
    m_text->setPlaceholderText(tr("Text to type on the devices (Unicode works on mirrored devices)…"));
    auto *row = new QHBoxLayout();
    auto *pasteBtn = theme::button(tr("Paste from PC clipboard"), this);
    auto *clipBtn = theme::button(tr("Set device clipboard"), this);
    auto *sendBtn = theme::button(tr("Send"), this, QStringLiteral("primary"));
    row->addWidget(pasteBtn);
    row->addWidget(clipBtn);
    row->addStretch(1);
    row->addWidget(sendBtn);
    left->addWidget(m_targetLabel);
    left->addWidget(m_text, 1);
    left->addWidget(theme::hint(tr("Special characters and emoji require an active mirror; plain `input text` only supports ASCII."), this));
    left->addLayout(row);

    // ---- right: templates ----
    auto *right = new QVBoxLayout();
    right->addWidget(theme::sectionTitle(tr("Saved phrases"), this));
    m_templates = new QListWidget(this);
    right->addWidget(m_templates, 1);
    auto *useBtn = theme::button(tr("Use"), this);
    auto *delBtn = theme::button(tr("Delete"), this, QStringLiteral("danger"));
    auto *useRow = new QHBoxLayout();
    useRow->addWidget(useBtn);
    useRow->addWidget(delBtn);
    right->addLayout(useRow);
    right->addWidget(theme::sectionTitle(tr("Save current text as"), this));
    m_tplName = new QLineEdit(this);
    m_tplName->setPlaceholderText(tr("Name"));
    m_tplCategory = new QComboBox(this);
    m_tplCategory->setEditable(true);
    m_tplCategory->addItems({ tr("General"), tr("Login"), tr("Search"), tr("Messages") });
    m_tplShortcut = new QLineEdit(this);
    m_tplShortcut->setPlaceholderText(tr("Shortcut (e.g. Ctrl+1)"));
    auto *saveBtn = theme::button(tr("Save phrase"), this);
    right->addWidget(m_tplName);
    right->addWidget(m_tplCategory);
    right->addWidget(m_tplShortcut);
    right->addWidget(saveBtn);

    auto *rightW = new QWidget(this);
    rightW->setLayout(right);
    rightW->setFixedWidth(260);
    root->addLayout(left, 1);
    root->addWidget(rightW);

    connect(pasteBtn, &QPushButton::clicked, this, [this]() { m_text->setPlainText(QApplication::clipboard()->text()); });
    connect(clipBtn, &QPushButton::clicked, this, [this]() { emit clipboardRequested(m_targets, text()); });
    connect(sendBtn, &QPushButton::clicked, this, [this]() {
        if (!text().isEmpty()) {
            emit sendRequested(m_targets, text());
            accept();
        }
    });
    connect(useBtn, &QPushButton::clicked, this, &TextSendDialog::useTemplate);
    connect(m_templates, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { useTemplate(); });
    connect(delBtn, &QPushButton::clicked, this, &TextSendDialog::deleteTemplate);
    connect(saveBtn, &QPushButton::clicked, this, &TextSendDialog::saveTemplate);
    reloadTemplates();
}

QString TextSendDialog::text() const
{
    return m_text->toPlainText();
}

void TextSendDialog::reloadTemplates()
{
    m_rows = TemplateRepository::loadAll();
    m_templates->clear();
    for (const TextTemplate &t : m_rows) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  ·  %2%3").arg(t.name, t.category, t.shortcut.isEmpty() ? QString() : QStringLiteral("  [%1]").arg(t.shortcut)), m_templates);
        item->setToolTip(t.content);
        item->setData(Qt::UserRole, static_cast<qlonglong>(t.id));
    }
}

void TextSendDialog::useTemplate()
{
    const int row = m_templates->currentRow();
    if (row >= 0 && row < m_rows.size()) {
        m_text->setPlainText(m_rows.at(row).content);
    }
}

void TextSendDialog::saveTemplate()
{
    if (text().isEmpty() || m_tplName->text().trimmed().isEmpty()) {
        return;
    }
    TextTemplate t;
    t.name = m_tplName->text().trimmed();
    t.category = m_tplCategory->currentText().trimmed();
    t.content = text();
    t.shortcut = m_tplShortcut->text().trimmed();
    TemplateRepository::save(t);
    m_tplName->clear();
    reloadTemplates();
}

void TextSendDialog::deleteTemplate()
{
    const int row = m_templates->currentRow();
    if (row >= 0 && row < m_rows.size()) {
        TemplateRepository::remove(m_rows.at(row).id);
        reloadTemplates();
    }
}

} // namespace farm
