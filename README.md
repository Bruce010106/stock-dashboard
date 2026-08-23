# 知衡 Quant

A 股量化选股与策略回测平台。首个内置策略为“杨永兴尾盘战法”。

## 在线访问

- Vercel：<https://zhiheng-quant.vercel.app>

## 已实现

- 指定策略的 A 股候选股筛选与逐项条件审计
- 指定股票、策略及持有交易日的信号表现回测
- “杨永兴尾盘战法”六项筛选条件
- 选股及回测 API
- `a-stock-data` 腾讯实时行情与分钟线适配器
- 东方财富沪深京股票池适配器（大页截断时使用新浪全市场清单降级）
- Tushare Pro 历史日线、点时市值、量比、换手率适配器
- Tushare 不可用时自动降级到腾讯不复权日线
- 全市场真实行情选股 API、连接状态 API 与 CSV 导出

首页“运行今日选股”会读取真实市场行情。回测页面现有样本仍用于展示交互口径；真实历史回测还需要开通 Tushare 历史分钟数据权限。

## 杨永兴尾盘战法

1. 当日涨幅为 3%–5%，包含边界。
2. 最近 30 个交易日至少出现一次明确涨停。
3. 总市值严格小于 200 亿元。
4. 当日量比严格大于 1。
5. 换手率为 5%–10%，包含边界。
6. 14:30 后首次突破此前日内最高价；随后发生回踩，但后续分钟最低价不低于突破位，最后一分钟收盘价仍站在突破位之上。

## 本地运行

```bash
npm install
copy .env.example .env.local
npm run dev
```

访问 <http://localhost:3000>。

`TUSHARE_TOKEN` 只允许配置在 `.env.local` 或 Vercel 环境变量中，不能使用 `NEXT_PUBLIC_` 前缀。留空时，实时扫描仍然可以运行，但近 30 日涨停会采用腾讯不复权日线按板块涨停幅度识别。

## 真实数据接口

- `GET /api/data-status`：检查腾讯行情与 Tushare 配置状态。
- `GET /api/screen/yang-yongxing`：扫描沪深京全市场；结果缓存 5 分钟。
- `GET /api/screen/yang-yongxing?codes=000001,600519`：只检查指定股票，适合调试。
- `POST /api/strategies/yang-yongxing`：对调用方提交的标准化候选数据执行策略审计。

## 验证

```bash
npm run test:strategy
npm run lint
npm run build
```
