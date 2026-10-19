#pragma once
#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
#include <QUrl>

namespace fincept::services::ashare {
struct ResearchItem {
    QString title{}, time{}, source{}, summary{};
    QUrl url{};
};
struct ResearchFeed {
    QVector<ResearchItem> items{};
    QString source{};
    QDateTime fetched{};
};
bool parse_research(const QByteArray& body, const QString& kind, ResearchFeed& feed, QString& error);
// Native adaptation of a-stock-data examples; see ashare/THIRD_PARTY.md.
class AShareResearchService : public QObject {
    Q_OBJECT
  public:
    explicit AShareResearchService(QObject* parent = nullptr);
    void fetch(const QString& symbol, const QString& kind);
  signals:
    void ready(const QString& symbol, const QString& kind, const fincept::services::ashare::ResearchFeed& feed);
    void failed(const QString& symbol, const QString& kind, const QString& error);

  private:
    void pump();
    struct Job {
        QString symbol{}, kind{}, key{};
    };
    QNetworkAccessManager network_;
    QQueue<Job> queue_;
    QSet<QString> pending_;
    QHash<QString, ResearchFeed> cache_;
    QHash<QString, QDateTime> attempted_;
    bool busy_ = false;
};
} // namespace fincept::services::ashare
