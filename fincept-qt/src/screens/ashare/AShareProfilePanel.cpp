#include "screens/ashare/AShareProfilePanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {
using namespace services::ashare;
namespace {
QString esc(const QString& value) {
    return value.toHtmlEscaped();
}
QString yi(double value) {
    return std::isfinite(value) ? QString::number(value / 1e8, 'f', 2) : "—";
}
QString num(double value, int digits = 2, const QString& suffix = {}) {
    return std::isfinite(value) ? QString::number(value, 'f', digits) + suffix : "—";
}
} // namespace
AShareProfilePanel::AShareProfilePanel(QWidget* parent) : QWidget(parent), service_(new AShareProfileService(this)) {
    setObjectName("ashareProfilePanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* controls = new QHBoxLayout;
    status_ = new QLabel(QStringLiteral("东方财富 F10 · 打开本页时加载"));
    status_->setWordWrap(true);
    status_->setStyleSheet("color:#636a76;font-size:11px;");
    controls->addWidget(status_, 1);
    auto* refresh = new QPushButton(QStringLiteral("刷新资料"));
    refresh->setObjectName("ashareProfileRefresh");
    controls->addWidget(refresh);
    layout->addLayout(controls);
    text_ = new QTextBrowser;
    text_->setObjectName("ashareProfileText");
    text_->setOpenLinks(false);
    text_->setOpenExternalLinks(false);
    text_->setMinimumHeight(110);
    layout->addWidget(text_);
    set_dark(false);
    connect(refresh, &QPushButton::clicked, this, [this]() { load(true); });
    connect(service_, &AShareProfileService::ready, this, [this](const QString& symbol, const StockProfile& profile) {
        if (symbol != symbol_)
            return;
        profile_ = profile;
        has_profile_ = true;
        loaded_ = symbol;
        status_->setText(QStringLiteral("东方财富 F10 · 获取于 %1 · 30 分钟缓存%2")
                             .arg(profile.fetched.toTimeZone(QTimeZone("Asia/Shanghai")).toString("HH:mm:ss"),
                                  profile.errors.isEmpty()
                                      ? QString()
                                      : QStringLiteral(" · %1 项未取得").arg(profile.errors.size())));
        render();
    });
    connect(service_, &AShareProfileService::failed, this, [this](const QString& symbol, const QString& error) {
        if (symbol != symbol_)
            return;
        status_->setText(QStringLiteral("个股资料暂不可用"));
        if (!has_profile_ || loaded_ != symbol)
            text_->setPlainText(error);
    });
}
void AShareProfilePanel::set_symbol(const QString& symbol) {
    if (symbol_ == symbol)
        return;
    symbol_ = symbol;
    has_profile_ = false;
    text_->setPlainText(symbol.isEmpty() ? QStringLiteral("指数没有个股资料。")
                                         : QStringLiteral("切换到本页后加载个股资料。"));
    if (active_)
        load(false);
}
void AShareProfilePanel::set_active(bool active) {
    active_ = active;
    // Only claim room for the F10 tables while this tab is shown.
    text_->setMinimumHeight(active ? 320 : 110);
    if (active_ && (!has_profile_ || loaded_ != symbol_))
        load(false);
}
void AShareProfilePanel::set_dark(bool dark) {
    dark_ = dark;
    text_->setStyleSheet(
        dark ? "background:#0f1319;color:#e6eaf0;border:1px solid #232a35;border-radius:4px;padding:6px;"
             : "background:#ffffff;color:#14171c;border:1px solid #dadfe6;border-radius:4px;padding:6px;");
    status_->setStyleSheet(dark ? "color:#8b95a5;font-size:11px;" : "color:#5e6673;font-size:11px;");
    if (has_profile_)
        render();
}
void AShareProfilePanel::load(bool force) {
    if (symbol_.isEmpty())
        return;
    status_->setText(symbol_.toUpper() + QStringLiteral(" · 正在请求东方财富 F10（约 3 秒）…"));
    if (!has_profile_)
        text_->setPlainText(QStringLiteral("正在加载公司概况、主营构成、财务指标与十大股东…"));
    service_->fetch(symbol_, force);
}
void AShareProfilePanel::render() {
    text_->setHtml(render_html(profile_, dark_));
}

QString AShareProfilePanel::summary_for(const QString& symbol) const {
    if (!has_profile_ || loaded_ != symbol)
        return {};
    const auto& p = profile_;
    QStringList lines;
    if (!p.industry.isEmpty())
        lines << QStringLiteral("行业：%1；板块：%2").arg(p.industry, p.board);
    if (!p.concepts.isEmpty())
        lines << QStringLiteral("概念：%1").arg(p.concepts.mid(0, 12).join(QStringLiteral("、")));
    if (!p.business.isEmpty()) {
        QStringList items;
        for (int i = 0; i < std::min<int>(4, p.business.first().items.size()); ++i) {
            const auto& it = p.business.first().items[i];
            items << QStringLiteral("%1 %2%").arg(it.name).arg(it.ratio * 100, 0, 'f', 1);
        }
        lines << QStringLiteral("主营构成（%1，%2）：%3")
                     .arg(p.business.first().kind, p.business.first().report_date, items.join(QStringLiteral("；")));
    }
    for (int i = 0; i < std::min<int>(3, p.finance.size()); ++i) {
        const auto& f = p.finance[i];
        lines << QStringLiteral(
                     "%1：营收 %2 亿（同比 %3%），归母净利 %4 亿（同比 %5%），ROE %6%，毛利率 %7%，资产负债率 %8%")
                     .arg(f.period, num(f.revenue / 1e8), num(f.revenue_yoy), num(f.net_profit / 1e8),
                          num(f.net_profit_yoy), num(f.roe), num(f.gross_margin), num(f.debt_ratio));
    }
    if (!p.holder_counts.isEmpty())
        lines << QStringLiteral("股东户数（%1）：%2")
                     .arg(p.holder_counts.first().date)
                     .arg(p.holder_counts.first().holders, 0, 'f', 0);
    return lines.join(QChar('\n'));
}
QString AShareProfilePanel::render_html(const StockProfile& p, bool dark) {
    const QString muted = dark ? "#8b95a5" : "#5e6673", border = dark ? "#232a35" : "#dadfe6";
    const QString up = "#e8394f", down = "#14a37f"; // red up / green down, readable on both themes
    auto signed_pct = [&](double value) {
        if (!std::isfinite(value))
            return QStringLiteral("—");
        return QStringLiteral("<span style='color:%1'>%2%3%</span>")
            .arg(value >= 0 ? up : down, value >= 0 ? "+" : "")
            .arg(value, 0, 'f', 2);
    };
    const QString th = QStringLiteral("style='color:%1;font-weight:normal;padding:3px 8px;border-bottom:1px solid %2;'")
                           .arg(muted, border);
    const QString td = QStringLiteral("style='padding:3px 8px;'");
    const QString tdr = QStringLiteral("style='padding:3px 8px;' align='right'");
    QString html = "<div style='font-size:13px;'>";
    auto section = [&](const QString& title, const QString& note = {}) {
        html +=
            QStringLiteral("<p style='margin-top:12px;margin-bottom:4px;'><b>%1</b>%2</p>")
                .arg(esc(title),
                     note.isEmpty()
                         ? QString()
                         : QStringLiteral(" <span style='color:%1;font-size:11px;'>%2</span>").arg(muted, esc(note)));
    };

    if (!p.name.isEmpty()) {
        section(QStringLiteral("公司概况"));
        html += QStringLiteral("<table cellspacing='0'>");
        const QList<QPair<QString, QString>> rows{
            {QStringLiteral("公司全称"), p.full_name},       {QStringLiteral("东财行业"), p.industry},
            {QStringLiteral("证监会行业"), p.csrc_industry}, {QStringLiteral("上市板块"), p.board},
            {QStringLiteral("所在地区"), p.province},        {QStringLiteral("董事长"), p.chairman},
            {QStringLiteral("员工人数"), p.employees}};
        for (const auto& row : rows)
            if (!row.second.isEmpty())
                html += QStringLiteral("<tr><td style='color:%1;padding:2px 12px 2px 0;'>%2</td><td>%3</td></tr>")
                            .arg(muted, row.first, esc(row.second));
        html += "</table>";
        if (!p.scope.isEmpty())
            html += QStringLiteral("<p style='color:%1;'>经营范围：%2</p>")
                        .arg(muted, esc(p.scope.left(220) + (p.scope.size() > 220 ? "…" : "")));
    }
    if (!p.concepts.isEmpty()) {
        section(QStringLiteral("所属板块 / 概念"), QStringLiteral("%1 个").arg(p.concepts.size()));
        html += "<p>" + esc(p.concepts.mid(0, 30).join(QStringLiteral(" · "))) + "</p>";
    }
    for (const auto& group : p.business) {
        if (group.kind == QStringLiteral("按行业") && p.business.size() > 2)
            continue; // product + region are enough
        section(QStringLiteral("主营构成 · ") + group.kind,
                QStringLiteral("报告期 %1 · 收入单位亿元").arg(group.report_date));
        html += QStringLiteral("<table cellspacing='0'><tr><th %1 align='left'>项目</th><th %1 align='right'>收入</th>"
                               "<th %1 align='right'>占比</th><th %1 align='right'>毛利率</th></tr>")
                    .arg(th);
        for (int i = 0; i < std::min<int>(8, group.items.size()); ++i) {
            const auto& item = group.items[i];
            html += QStringLiteral("<tr><td %1>%2</td><td %3>%4</td><td %3>%5</td><td %3>%6</td></tr>")
                        .arg(td, esc(item.name), tdr, yi(item.income), num(item.ratio * 100, 1, "%"),
                             item.has_margin ? num(item.gross_margin * 100, 1, "%") : "—");
        }
        html += "</table>";
    }
    if (!p.finance.isEmpty()) {
        section(QStringLiteral("主要财务指标"), QStringLiteral("按报告期 · 金额单位亿元 · 同比红增绿减"));
        html += QStringLiteral(
                    "<table cellspacing='0'><tr><th %1 align='left'>报告期</th><th %1 align='right'>营业总收入</th>"
                    "<th %1 align='right'>同比</th><th %1 align='right'>归母净利润</th><th %1 align='right'>同比</th>"
                    "<th %1 align='right'>ROE</th><th %1 align='right'>毛利率</th><th %1 align='right'>负债率</th>"
                    "<th %1 align='right'>每股收益</th></tr>")
                    .arg(th);
        for (int i = 0; i < std::min<int>(8, p.finance.size()); ++i) {
            const auto& f = p.finance[i];
            html += QStringLiteral("<tr><td %1>%2</td><td %3>%4</td><td %3>%5</td><td %3>%6</td><td %3>%7</td>"
                                   "<td %3>%8</td><td %3>%9</td><td %3>%10</td><td %3>%11</td></tr>")
                        .arg(td, esc(f.period), tdr, yi(f.revenue), signed_pct(f.revenue_yoy), yi(f.net_profit),
                             signed_pct(f.net_profit_yoy), num(f.roe, 2, "%"), num(f.gross_margin, 2, "%"))
                        .arg(num(f.debt_ratio, 2, "%"), num(f.eps, 3));
        }
        html += "</table>";
    }
    if (!p.holders.isEmpty()) {
        section(QStringLiteral("十大股东"), QStringLiteral("截至 %1").arg(p.holders_date));
        html += QStringLiteral("<table cellspacing='0'><tr><th %1 align='left'>#</th><th %1 align='left'>股东名称</th>"
                               "<th %1 align='right'>持股（万股）</th><th %1 align='right'>占总股本</th><th %1 "
                               "align='right'>变动</th></tr>")
                    .arg(th);
        for (const auto& h : p.holders)
            html += QStringLiteral("<tr><td %1>%2</td><td %1>%3</td><td %4>%5</td><td %4>%6</td><td %4>%7</td></tr>")
                        .arg(td)
                        .arg(h.rank)
                        .arg(esc(h.name), tdr, num(h.shares / 1e4, 2), num(h.ratio, 2, "%"),
                             esc(h.change.isEmpty() ? "—" : h.change));
        html += "</table>";
    }
    if (!p.holder_counts.isEmpty()) {
        section(QStringLiteral("股东户数"), QStringLiteral("户数减少通常意味着筹码趋于集中，仅供参考"));
        html += QStringLiteral(
                    "<table cellspacing='0'><tr><th %1 align='left'>截止日</th><th %1 align='right'>股东户数</th>"
                    "<th %1 align='right'>较上期</th></tr>")
                    .arg(th);
        for (int i = 0; i < std::min<int>(6, p.holder_counts.size()); ++i) {
            const auto& c = p.holder_counts[i];
            html += QStringLiteral("<tr><td %1>%2</td><td %3>%4</td><td %3>%5</td></tr>")
                        .arg(td, esc(c.date), tdr, QString::number(c.holders, 'f', 0),
                             c.has_change ? signed_pct(c.change_ratio) : "—");
        }
        html += "</table>";
    }
    if (!p.errors.isEmpty())
        html +=
            QStringLiteral("<p style='color:%1;'>未取得：%2</p>").arg(muted, esc(p.errors.join(QStringLiteral("；"))));
    html += QStringLiteral("<p style='color:%1;font-size:11px;'>数据来源：东方财富 F10 "
                           "公开页面接口，非授权数据源，可能延迟或缺字段；不构成投资建议。</p></div>")
                .arg(muted);
    return html;
}
} // namespace fincept::screens
