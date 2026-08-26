# 知衡 Quant

A 股量化选股与策略回测平台。首个内置策略为“杨永兴尾盘战法”。

## 在线访问

- Vercel：<https://zhiheng-quant.vercel.app>

## 已实现

- 指定策略的 A 股候选股筛选与逐项条件审计
- 指定股票、日期区间及持有交易日的真实历史信号表现回测
- 回测累计收益、回撤、区间切换与绩效摘要（不生成缺失的基准数据）
- 选股结果迷你 K 线、二次筛选、排序、分页与筛选后导出
- 股票详情页：真实日 K、成交量、MA5/10/20、涨停与当前策略信号标记
- 邮箱注册、登录、找回密码与 Cookie 会话
- 自选股和持仓：未登录时本地保存，登录后通过 Supabase 跨设备同步
- 首次登录可将浏览器数据幂等合并到云端，按真实最新价计算市值与盈亏
- “杨永兴尾盘战法”六项筛选条件
- 选股及回测 API
- `a-stock-data` 腾讯实时行情与分钟线适配器
- 东方财富沪深京股票池适配器（大页截断时使用新浪全市场清单降级）
- Tushare Pro 历史日线、点时市值、量比、换手率适配器
- Tushare 不可用时自动降级到腾讯不复权日线
- 新浪财经免费历史 K 线适配器，作为策略回测在未配置 Tushare 时的近似数据源
- 全市场真实行情选股 API、连接状态 API 与 CSV 导出

首页“运行今日选股”会读取真实市场行情。回测页面自动选择数据源：配置了 `TUSHARE_TOKEN`（并开通相应权限）时，使用 Tushare 日线、`daily_basic` 点时指标与 `stk_mins` 历史分钟线重放信号，为 1 分钟点时精确口径；未配置时，自动改用新浪财经免费 K 线数据源，以 5 分钟线近似重放信号，历史总市值/换手率/量比为静态估算值，非交易所口径，单次区间最长 30 个自然日（Tushare 精确模式为 90 个自然日）。两种模式的返回结果都会标注口径（`accuracyMode`/`isApproximate`/`source`）与详细警告，近似结果不会被标注为精确数据。

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

`TUSHARE_TOKEN` 只允许配置在 `.env.local` 或 Vercel 环境变量中，不能使用 `NEXT_PUBLIC_` 前缀。兼容服务可以通过服务端变量 `TUSHARE_API_URL` 指定；留空时使用官方 `https://api.tushare.pro`。未配置 Token 时，实时扫描仍然可以运行，但近 30 日涨停会采用腾讯不复权日线按板块涨停幅度识别；策略回测同样可以直接运行，自动切换到新浪财经免费近似数据源，不需要任何密钥。

登录与云同步需要 `NEXT_PUBLIC_SUPABASE_URL` 和 `NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY`。Vercel 原生 Supabase 集成会自动配置这些变量；首次初始化数据库时运行 `npm run db:migrate`，迁移记录保存在数据库的私有 schema 中。

真实回测单次支持最多 5 只股票。系统先用日线和点时指标完成前五项粗筛，只为候选日期读取分钟线；这适合交互式核验，不替代全市场多年离线回测任务。

- **Tushare 精确模式**（配置 `TUSHARE_TOKEN` 且具备相应权限）：单次区间最长 90 个自然日，1 分钟点时精确数据。
- **新浪免费近似模式**（未配置 Tushare 时的默认模式）：单次区间最长 30 个自然日，5 分钟近似数据；历史总市值、换手率、量比按当前快照隐含股本静态外推估算，仅供近似参考，不代表交易所口径。超出区间上限的请求会被拒绝（400/422），不会被静默截断。

## 真实数据接口

- `GET /api/data-status`：检查腾讯行情与 Tushare 配置状态。
- `GET /api/screen/yang-yongxing`：扫描沪深京全市场；结果缓存 5 分钟。
- `GET /api/screen/yang-yongxing?codes=000001,600519`：只检查指定股票，适合调试。
- `GET /api/backtest/yang-yongxing?codes=002892&startDate=2026-08-01&endDate=2026-08-24&holdingTradingDays=5`：使用真实历史数据执行受控区间回测（区间 ≤ 30 天，未配置 Tushare 时也可直接调用；配置 Tushare 后区间上限为 90 天）。
- `GET /api/stocks/600519`：读取单只股票的真实日线、实时快照和当前策略核验结果。
- `GET /api/portfolio/quotes?codes=000001,600519`：读取自选股与持仓使用的公开实时快照字段。
- `GET/POST/PUT/DELETE /api/portfolio/cloud`：读取或修改当前登录用户的云端组合数据。
- `POST /api/portfolio/cloud/merge`：将首次登录前的浏览器数据幂等合并到当前账号。
- `POST /api/strategies/yang-yongxing`：对调用方提交的标准化候选数据执行策略审计。

## 验证

```bash
npm run test:strategy
npm run lint
npm run build
```
