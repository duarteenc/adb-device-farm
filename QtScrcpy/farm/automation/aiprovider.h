#ifndef FARM_AUTOMATION_AIPROVIDER_H
#define FARM_AUTOMATION_AIPROVIDER_H

#include <functional>

#include <QObject>
#include <QString>
#include <QStringList>

#include "workflowmodel.h"

class QNetworkAccessManager;

namespace farm {

/**
 * Optional natural-language → workflow planner. Providers:
 *   none    (default; the product never requires it)
 *   ollama  (local, http://localhost:11434, /api/chat)
 *   openai  (any OpenAI-compatible endpoint the operator points at, incl. local ones)
 *
 * The generated workflow is validated against the node catalog and every node
 * marked "risky" (uninstall, clear data, reboot, delete…) is listed so the UI
 * can ask for confirmation before saving/running.
 */
class AiProvider : public QObject
{
    Q_OBJECT
public:
    static AiProvider &instance();

    bool isConfigured() const;
    QString providerName() const;
    void checkAvailability(QObject *context, std::function<void(bool ok, QString info)> done);
    void generateWorkflow(const QString &request, QObject *context, std::function<void(Workflow workflow, QString error, QString rawText)> done);
    static QStringList riskyNodeTitles(const Workflow &workflow);
    static QString systemPrompt();
    /// Extracts the first JSON object from a model reply (handles ``` fences).
    static QString extractJson(const QString &text);

private:
    explicit AiProvider(QObject *parent = nullptr);
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace farm

#endif // FARM_AUTOMATION_AIPROVIDER_H
