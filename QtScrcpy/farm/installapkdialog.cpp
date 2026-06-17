#include "installapkdialog.h"

#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const char *kStyle = R"(
QDialog {
    background:#0b0f17;
    color:#e2e8f0;
    font-size:13px;
}
QLabel { background:transparent; color:#e2e8f0; }
#titleLabel { font-size:18px; font-weight:bold; color:#ffffff; }
#sectionLabel { font-size:13px; font-weight:bold; color:#e2e8f0; }
QLineEdit {
    background:#1a1f2e;
    border:1px solid #2a344a;
    border-radius:4px;
    padding:8px 12px;
    color:#e2e8f0;
    font-size:12px;
}
QLineEdit:focus { border-color:#4169e1; }
QPushButton {
    background:transparent;
    border:1px solid #2a344a;
    border-radius:5px;
    padding:6px 16px;
    color:#e2e8f0;
    font-size:12px;
}
QPushButton:hover { background:#26314a; }
QPushButton#primary { background:#4169e1; border:none; color:#ffffff; padding:8px 22px; }
QPushButton#primary:hover { background:#5a7df5; }
QPushButton#browse {
    border:1px dashed #4169e1;
    color:#4169e1;
    padding:8px 14px;
    font-size:16px;
}
QPushButton#browse:hover { background:rgba(65,105,225,0.12); }
QPushButton#close { border:none; color:#7c8aa0; font-size:18px; padding:0; }
QPushButton#close:hover { color:#e2e8f0; background:#1e2636; }
#deviceChip { background:transparent; color:#e2e8f0; }
)";
}

InstallApkDialog::InstallApkDialog(const QList<QPair<QString, int>> &targets, QWidget *parent)
    : QDialog(parent)
{
    setupUi(targets);
    setStyleSheet(QString::fromUtf8(kStyle));
}

InstallApkDialog::~InstallApkDialog() = default;

void InstallApkDialog::setupUi(const QList<QPair<QString, int>> &targets)
{
    setWindowTitle(tr("Instalar APK"));
    setMinimumWidth(660);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 22);
    root->setSpacing(16);

    // --- Header: title + close ---
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Instalar APK"));
    title->setObjectName("titleLabel");
    auto *closeBtn = new QPushButton("\xE2\x9C\x95");
    closeBtn->setObjectName("close");
    closeBtn->setFixedSize(28, 28);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(closeBtn);
    root->addLayout(header);

    // --- File picker section ---
    auto *sectionLabel = new QLabel(tr("O seleccionar manualmente"));
    sectionLabel->setObjectName("sectionLabel");
    root->addWidget(sectionLabel);

    auto *pickRow = new QHBoxLayout;
    pickRow->setSpacing(10);
    m_pathEdit = new QLineEdit;
    m_pathEdit->setPlaceholderText(tr("Archivo .apk local"));
    auto *browseBtn = new QPushButton("\xF0\x9F\x97\x81");
    browseBtn->setObjectName("browse");
    browseBtn->setToolTip(tr("Seleccionar archivo .apk"));
    connect(browseBtn, &QPushButton::clicked, this, &InstallApkDialog::browseForApk);
    pickRow->addWidget(m_pathEdit, 1);
    pickRow->addWidget(browseBtn, 0);
    root->addLayout(pickRow);

    // --- Target devices (numbered phone chips) ---
    auto *devRow = new QHBoxLayout;
    devRow->setSpacing(16);
    for (const auto &t : targets) {
        auto *chip = new QWidget;
        chip->setObjectName("deviceChip");
        auto *cv = new QVBoxLayout(chip);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(2);
        auto *icon = new QLabel("\xF0\x9F\x93\xB1");    // phone emoji
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet("font-size:26px;");
        auto *num = new QLabel(QString::number(t.second));
        num->setAlignment(Qt::AlignCenter);
        num->setStyleSheet("font-size:12px; color:#94a3b8;");
        cv->addWidget(icon);
        cv->addWidget(num);
        chip->setToolTip(t.first);
        devRow->addWidget(chip, 0, Qt::AlignTop);
    }
    devRow->addStretch();
    root->addLayout(devRow);

    root->addStretch();

    // --- Install button ---
    auto *footer = new QHBoxLayout;
    m_installButton = new QPushButton(tr("Instalar"));
    m_installButton->setObjectName("primary");
    connect(m_installButton, &QPushButton::clicked, this, &InstallApkDialog::onInstallClicked);
    footer->addWidget(m_installButton);
    footer->addStretch();
    root->addLayout(footer);
}

QString InstallApkDialog::apkPath() const
{
    return m_pathEdit->text().trimmed();
}

void InstallApkDialog::browseForApk()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Seleccionar archivo .apk"), QString(), tr("APK files (*.apk)"));
    if (!file.isEmpty()) {
        m_pathEdit->setText(file);
    }
}

void InstallApkDialog::onInstallClicked()
{
    if (apkPath().isEmpty()) {
        QMessageBox::warning(this, tr("Instalar APK"),
                             tr("Selecciona un archivo .apk primero."));
        return;
    }
    accept();
}
