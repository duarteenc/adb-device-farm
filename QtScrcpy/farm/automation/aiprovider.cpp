#include "aiprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "nodecatalog.h"

namespace farm {

AiProvider &AiProvider::instance()
{
    static AiProvider provider;
    return provider;
}

AiProvider::AiProvider(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

bool AiProvider::isConfigured() const
{
    return FarmSettings::instance().aiProvider() != QLatin1String("none");
}

QString AiProvider::providerName() const
{
    return FarmSettings::instance().aiProvider();
}

QString AiProvider::systemPrompt()
{
    return QStringLiteral(
               "You translate an operator's request into an ADB Device Farm workflow JSON.\n"
               "Reply with ONLY a JSON object of the form {\"name\":..., \"description\":..., \"variables\":{}, \"nodes\":[...], \"connections\":[...]}.\n"
               "Each node: {\"id\":\"n1\",\"type\":<type>,\"params\":{...},\"pos\":[x,y]}. Each connection: {\"from\":\"n1\",\"port\":\"out\",\"to\":\"n2\"}.\n"
               "Exactly one flow.start and at least one flow.end. Coordinates are screen fractions 0..1. Use ui.* nodes when an element "
               "has text, screen.waitForText/OCR when you must read the screen, time.wait between UI transitions. Never invent node types.\n"
               "Available node types (type [title] outputs params):\n")
        + NodeCatalog::describeForPrompt();
}

QString AiProvider::extractJson(const QString &text)
{
    QString t = text.trimmed();
    const int fence = t.indexOf(QLatin1String("```"));
    if (fence >= 0) {
        int start = t.indexOf(QLatin1Char('\n'), fence);
        const int end = t.indexOf(QLatin1String("```"), start + 1);
        if (start >= 0 && end > start) {
            t = t.mid(start + 1, end - start - 1);
        }
    }
    const int open = t.indexOf(QLatin1Char('{'));
    const int close = t.lastIndexOf(QLatin1Char('}'));
    if (open >= 0 && close > open) {
        return t.mid(open, close - open + 1);
    }
    return t;
}

QStringList AiProvider::riskyNodeTitles(const Workflow &workflow)
{
    QStringList list;
    for (const WorkflowNode &n : workflow.nodes) {
        const NodeSpec spec = NodeCatalog::spec(n.type);
        if (spec.risky) {
            list << QStringLiteral("%1 (%2)").arg(spec.title, n.params.value(QStringLiteral("package"), n.params.value(QStringLiteral("remote"), n.params.value(QStringLiteral("args")))).toString());
        }
    }
    return list;
}

void AiProvider::checkAvailability(QObject *context, std::function<void(bool, QString)> done)
{
    const FarmSettings &s = FarmSettings::instance();
    if (!isConfigured()) {
        QMetaObject::invokeMethod(context, [done]() { done(false, QStringLiteral("No AI provider configured (Settings > Automation). The app works fully without one.")); }, Qt::QueuedConnection);
        return;
    }
    const QString base = s.aiEndpoint();
    QNetworkRequest req(QUrl(base + (s.aiProvider() == QLatin1String("ollama") ? QStringLiteral("/api/tags") : QStringLiteral("/v1/models"))));
    const QString key = s.stringValue(QStringLiteral("ai/apiKey"), QString());
    if (!key.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
    }
    QNetworkReply *reply = m_nam->get(req);
    QPointer<QObject> ctx(context);
    connect(reply, &QNetworkReply::finished, this, [reply, ctx, done]() {
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString info = ok ? QString::fromUtf8(reply->readAll()).left(300) : reply->errorString();
        reply->deleteLater();
        if (ctx) {
            QMetaObject::invokeMethod(ctx.data(), [done, ok, info]() { done(ok, info); }, Qt::QueuedConnection);
        }
    });
}

void AiProvider::generateWorkflow(const QString &request, QObject *context, std::function<void(Workflow, QString, QString)> done)
{
    const FarmSettings &s = FarmSettings::instance();
    QPointer<QObject> ctx(context);
    auto finish = [ctx, done](const Workflow &w, const QString &err, const QString &raw) {
        if (ctx) {
            QMetaObject::invokeMethod(ctx.data(), [done, w, err, raw]() { done(w, err, raw); }, Qt::QueuedConnection);
        }
    };
    if (!isConfigured()) {
        finish(Workflow(), QStringLiteral("No AI provider configured. Choose Ollama (local) or an OpenAI-compatible endpoint in Settings > Automation."), QString());
        return;
    }
    const bool ollama = s.aiProvider() == QLatin1String("ollama");
    QJsonObject body;
    QJsonArray messages;
    messages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("system") }, { QStringLiteral("content"), systemPrompt() } });
    messages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("content"), request } });
    body[QStringLiteral("model")] = s.aiModel();
    body[QStringLiteral("messages")] = messages;
    QUrl url;
    if (ollama) {
        body[QStringLiteral("stream")] = false;
        body[QStringLiteral("format")] = QStringLiteral("json");
        url = QUrl(s.aiEndpoint() + QStringLiteral("/api/chat"));
    } else {
        body[QStringLiteral("temperature")] = 0.2;
        body[QStringLiteral("response_format")] = QJsonObject{ { QStringLiteral("type"), QStringLiteral("json_object") } };
        url = QUrl(s.aiEndpoint() + QStringLiteral("/v1/chat/completions"));
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QString key = s.stringValue(QStringLiteral("ai/apiKey"), QString());
    if (!key.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
    }
    req.setTransferTimeout(180000);
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, finish, ollama]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finish(Workflow(), QStringLiteral("AI request failed: %1").arg(reply->errorString()), QString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QString content;
        if (ollama) {
            content = root.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        } else {
            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                content = choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
            }
        }
        if (content.isEmpty()) {
            finish(Workflow(), QStringLiteral("empty reply from the model"), QString());
            return;
        }
        QString err;
        Workflow w = Workflow::fromJsonText(extractJson(content), &err);
        if (!err.isEmpty() && w.nodes.isEmpty()) {
            finish(Workflow(), QStringLiteral("model output is not a workflow: %1").arg(err), content);
            return;
        }
        if (w.name.isEmpty() || w.name == QLatin1String("Untitled")) {
            w.name = QStringLiteral("AI: ") + content.left(30);
        }
        QString firstError;
        if (!WorkflowValidator::isValid(w, &firstError)) {
            finish(w, QStringLiteral("generated workflow needs fixes: %1").arg(firstError), content);
            return;
        }
        FarmLog::instance().info(QStringLiteral("ai"), QStringLiteral("generated workflow '%1' with %2 nodes").arg(w.name).arg(w.nodes.size()));
        finish(w, QString(), content);
    });
}

} // namespace farm
