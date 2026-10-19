# A 股观察台（个人自用二开）

基于 FinceptTerminal 的 Qt 桌面项目增加的 A 股看板。上游基线：
`cfd007a8439d74f2ab2675d80cc4cd0fba4ba21d`。

源码仍位于原项目的 `src/screens/ashare` 和 `src/services/ashare`。
原终端中通过 Markets 菜单的「A 股工作台」、组件目录或命令栏 `ashare` 打开。
Launchpad 增加「A 股工作台」模板；新会话没有已有布局时默认打开 A 股。
布局恢复沿用原版机制，保存所选证券和图表周期（旧布局中的分时／日 K 标记仍可恢复）；自选列表和日夜主题沿用原有本地设置。
本目录另提供独立桌面启动目标，使用**同一套 C++ 看板代码**，便于在不编译其余终端模块时运行和验证。
独立入口不是上游完整终端，未包含其其他研究、交易、AI 模块。

## 首版功能

- 界面为专业暗色终端风格（2026-10-15 改版，规范见 `docs/TERMINAL_DESIGN.md`）：默认夜间，可切换日间并保存；图表、盘口、表格随主题切换，行情数字为等宽字体。切换主题不改变当前自选、周期和数据。
- 快捷键：Ctrl+K 聚焦股票搜索，Ctrl+M 切换个股 / 大盘，Ctrl+1–8 切换分时 / 日 / 周 / 月 / 5 / 15 / 30 / 60 分钟，F5 立即刷新（大盘视图下刷新大盘数据）。

- 沪深京 A 股代码添加、名称/拼音首字母搜索、删除、自选股本地持久化（最多 30 只）。
- 股价、涨跌幅、当日开高低、成交额、行情时间，红涨绿跌。
- 当天分时价格、成交均价、分钟成交量，午休时间轴压缩。
- 多周期 K 线：日 K、周 K、月 K（前复权 / 不复权可切换），5 / 15 / 30 / 60 分钟 K（数据源仅提供不复权）。
- 主图指标：MA5/10/20/60 或 BOLL(20,2)；副图指标：MACD(12,26,9)、KDJ(9,3,3)、RSI14，可关闭；成交量叠加 MAVOL5/10。鼠标十字线，滚轮缩放。
- 交易时间每 15 秒请求自选行情，所选周期 K 线最多每 60 秒自动请求一次；支持暂停和手动刷新。
- 网络失败保留旧数据并提示；返回非今日行情时显示交易日期，不伪装成当天实时行情。
- a-stock-data 适配：个股研报、个股关键词新闻、华尔街见闻 A 股频道快讯，标题可打开原文。
- 自选表扩展列：量比、换手率、振幅、成交额、总市值、PE(TTM)、PB。点击表头排序（降序 → 升序 → 原顺序），无数据排在最后；右键表头选择显示列。
- 自选分组：默认「持仓 / 观察 / 短线」，可新建（最多 10 个）或删除；右键行移入或移出分组；在某分组下添加股票会直接归入该组。分组、列显示和图表偏好均本地保存。
- 五档盘口：买卖五档价格与挂单量（手）、委比、外盘 / 内盘；涨停 / 跌停价显示在行情明细行。
- 分笔成交：东方财富公开接口，最近 40 条，标注主动买 B / 主动卖 S。
- 个股资料（F10）：公司概况、所属板块 / 概念、主营构成（按产品 / 地区）、近 8 期主要财务指标、十大股东、股东户数。仅在打开该页签时请求。
- 顶部指数自选卡片：默认上证指数、深证成指、创业板指；支持通过下拉框添加和移除，点击卡片切换分时/日 K，自动保存。

指数目录包括上证指数、深证成指、创业板指、沪深300、上证50、中证500、中证1000、科创50、中小100。
顶部卡片展示点位、涨跌幅，旧行情及刷新失败会标记；指数与个股自选分别保存。
指数使用 `day` 原始点位序列，不适用个股前复权，也不将成交额/成交量伪造为指数均价。
选中指数时隐藏个股资讯、五档盘口和个股资料。顶部“移除指数”作用于下拉框当前选项，点击卡片会同步此选项。

当前为行情与技术指标展示，不含 AI 文字诊断、交易下单或提醒推送。指标数值仅为计算结果，不构成投资建议。

## 行情来源与口径

使用腾讯公开行情接口：`appstock/app/minute/query` 和 `appstock/app/fqkline/get`。
接口不是有 SLA 的授权商业行情源，可能限流、变更或延迟。每 15 秒请求不等于数据每 15 秒更新。
服务类独立于界面，后续可以加入 AKShare / Tushare 等适配器。

- 快照成交量多数板块为手、科创板（688/689）为股；程序用成交额 / (成交量 × 现价) 校验单位后统一换算为股。
  分时累计成交量使用同一单位。2026-10-15 前的版本对科创板一律按手乘 100，导致科创板分时均价约为实际的 1/100，已修正。
- 分时第三列是累计成交量，换算为股后取差得到分钟成交量。
- 分时第四列是累计成交额（元）；均价 = 累计成交额 / 累计成交股数。没有金额时不伪造均价。
- 分时曲线画数据源实际返回的分钟点，最新价单独展示，不捏造缺失分钟。
- 日 K 使用源返回的 `qfqday`；不拼接未复权快照，也不静默切换复权口径。
- 日 K 成交量统一为股：科创板 688/689 使用源值，其余股票由手乘 100。分时与快照维持各自单位转换。
- 周 / 月 K 使用 `qfqweek` / `qfqmonth`（前复权）或 `week` / `month`（不复权）；分钟 K 使用 `kline/mkline` 的 `m5`～`m60`，最多 320 根，为不复权。
- 腾讯对部分无除权记录的股票（如 2026-10-15 实测 688981）只返回 `day` 而不返回 `qfqday`。程序不会把它冒充为前复权，会提示切换「不复权」查看。
- 未提供后复权：实测腾讯后复权序列最新一根（盘中实时行）使用的复权因子与历史行不一致。
- 指标均以当前下载窗口计算：MA 为简单平均；EMA 以首个收盘价起算（与通达信 / 同花顺做法一致）；MACD 前 25 根、各 MA 不足窗口时显示 —。
  BOLL 标准差使用样本标准差（n-1）。KDJ 的 K、D 初值为 50。RSI14 使用 Wilder 平滑。由于历史窗口不同，数值可能与其他软件存在小差异。
- 五档挂单量各板块均为手（已与新浪逐档股数核对）；科创板挂单不足 1 手时显示「<1」。
- 上午及下午自动刷新按北京时间判断；节假日由返回日期提示，尚无完整交易日历，因此工作日节假日仍可能发起请求。
- 午休、收盘、停牌、盘前无数据均不会填充模拟行情。数据源不返回分时或前复权数据时显示不可用。

## 市场全景（大盘）

顶部「个股 / 大盘」切换（Ctrl+M），大盘视图仅在打开时请求，交易时段每 60 秒刷新，非交易时段只加载一次。

- 涨跌家数与涨跌分布：东方财富 push2ex 涨跌分布接口，±11 档为涨跌停，k 档表示涨跌幅约在 (k−1, k]%；统计范围以接口为准。
- 涨停 / 跌停 / 炸板与炸板率：东方财富涨停池、跌停池、炸板池；炸板率 = 炸板 /（涨停 + 炸板）。
- 全 A 成交额：腾讯申万一级行业成交额求和，近似全 A，并在卡片上注明口径。
- 板块热力图：腾讯板块排行，可选申万一级、申万二级、概念（最强 / 最弱各 40 个）；颜色深浅在 ±3% 封顶；悬停显示上涨家数、主力净流入、成交额和领涨股；点击板块在右侧列出涨幅前 40 只成分股。
- 板块资金流：同一板块数据按主力净流入排序（腾讯口径的“主力”）。
- 涨停池（连板、首次 / 最后封板、炸板次数、封单）、炸板池、跌停池（连续跌停、开板次数）。
- 龙虎榜：东方财富数据中心，只显示最近一个已公布交易日（通常收盘后数小时公布），同一股票多条上榜原因合并显示。
- 任一表格双击个股：加入自选（自选满 30 只时提示，不加入）并切换到个股视图。

未提供的数据及原因：

- 北向资金实时净买入：沪深港通自 2024-08-19 起不再盘中披露。接口仍返回 0 值序列，看板不展示 0 或估算值，只在卡片中说明。
- 个股主力资金流：2026-10-15 测试时东方财富 push2 对非浏览器 User-Agent 返回 HTTP 403，腾讯原个股资金流接口已下线。
  本程序不伪装浏览器绕过对方的限制，因此暂未提供；板块层面的主力净流入可用。同一限制也可能导致个股页的分笔成交暂时不可用，界面会显示 HTTP 状态。

大盘联网冒烟验证（夜间截图后切换日间截图，输出 `market-smoke.json`）：

```powershell
./ashare-dist/AShareBoard.exe -platform offscreen --market-smoke-output ./ashare-build/market-verified
```

2026-10-15 收盘后实测：涨跌分布 5368 只（涨 417 / 平 26 / 跌 4925），申万一级 31 个板块，点击板块取到 40 只成分股，涨停池 36、炸板池 7，龙虎榜 2026-10-14 共 68 只。

## 从 Fincept 接入的模块：AI 解读、持仓、笔记

顶部视图切换为「个股 / 大盘 / 持仓 / 笔记」。三个模块都不依赖 Python，也不需要 Fincept 账号或其云服务。

**AI 解读**（个股页签）
- 复用 Fincept 的 `services/llm/ProviderCatalog`（服务商列表、默认地址、端点拼接、AtlasCloud 屏蔽）和 `ModelCatalog`（各模型输出上限）；
  `LlmHttpHelpers.cpp` 中的错误信息解析与 `<think>` 过滤两个函数复制进 `AShareAiService.cpp`（原文件依赖 LlmService / MCP，未链接）。
  未接入 Fincept 的 MCP 工具调用、账号、DataHub 和数据库配置。
- 支持 OpenAI 兼容服务商（DeepSeek、Kimi、OpenAI、OpenRouter、Groq、xAI、MiniMax、AIHubMix、本地 Ollama 等）和 Anthropic；
  暂不支持 Gemini 原生接口与 Fincept 自家模型。通义千问等可选 OpenAI 并填写其兼容接口地址。
- API Key 用 Windows DPAPI 按当前系统用户加密后存入本地设置；其他平台拒绝保存。
- 每次对话首轮发送的数据快照：该股实时行情与扩展字段、五档摘要、日 K 指标、最近 30 根前复权日 K、已加载的 F10 摘要、已加载的大盘涨跌家数。
  勾选「附带我的持仓」时才附带该股持仓数量与成本。模型输出按 Markdown 渲染并禁用原始 HTML。推理模型的思考内容不显示。
- AI 输出可能有误，不构成投资建议；系统提示要求模型只依据所给数据回答、缺失数据明确说明。

**持仓**
- 交易记录：买入、卖出、分红，费用按券商交割单手工填写；加权平均成本法计算持仓成本与已实现盈亏；卖出超过当日持仓会被拒绝。
- 行情：独立请求腾讯公开快照，交易时段每 30 秒刷新；当日盈亏按“当前持仓 × 今日涨跌”估算，不区分当日买卖。
- 持仓分布图（矩形面积 = 市值，颜色可切换浮动盈亏 / 当日涨跌 / 仓位）参考 Fincept `PortfolioHeatmap` 的三种模式，用本看板配色重新绘制；
  CSV 导出直接复用 Fincept `screens/portfolio/PortfolioCsv.h`（含公式注入防护）。
- 数据保存在本地 `portfolio.json`（Windows 用户 AppData 下的 AShareBoard 目录），原子写入；文件损坏时不会被覆盖。

**笔记**
- 数据模型沿用 Fincept `FinancialNote` 的分类、观点（看多 / 看空 / 中性）、关联股票、标签、收藏；存为本地 `notes.json`，不使用 Fincept 的 SQLite。
- 支持搜索、分类与收藏筛选、按股票筛选、Markdown 预览（禁用原始 HTML）、导出 Markdown、Ctrl+S 保存。
- 个股页的「本股笔记」页签列出关联该股的笔记，可一键新建或跳转。

联网冒烟验证（写入两笔示例持仓与一条笔记到输出目录，不触碰个人数据）：

```powershell
./ashare-dist/AShareBoard.exe -platform offscreen --modules-smoke-output ./ashare-build/modules-verified
```

## 盘口与个股资料来源

| 能力 | 接口 | 说明 |
| --- | --- | --- |
| 五档、外盘 / 内盘、量比、换手、市值、PE、PB、涨跌停 | 腾讯 `minute/query` 同一快照的扩展字段 | 与价格同次刷新，无额外请求 |
| 分笔成交 | 东方财富 `push2.eastmoney.com/api/qt/stock/details/get` | 约 3 秒聚合的快照，**不是交易所逐笔**；成交量各板块均为手（已与腾讯分钟增量核对） |
| 个股资料 | 东方财富 F10 `emweb.securities.eastmoney.com/PC_HSF10/…` 五个页面接口 | 串行请求、间隔 0.4 秒，缓存 30 分钟，强制刷新每分钟最多一次；单项失败不影响其他项，并在页内列出 |

分笔仅为当前选中个股请求，随行情每 15 秒刷新；失败时保留上次数据并提示。
以上接口均为公开网页接口，无 SLA，可能限流、改版或缺字段（例如银行股无毛利率）。这些适配为本项目自行编写，不来自 a-stock-data。

## a-stock-data 接入范围

参考 [simonlin1212/a-stock-data](https://github.com/simonlin1212/a-stock-data) v3.10.2，
将部分 Python 示例改写为原生 Qt 适配器，运行时不需要 Python 或 API Key。
完整来源、固定版本与 Apache-2.0 许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。

| 能力 | 当前接入 |
| --- | --- |
| 个股研报 | 东方财富，最多 20 条，包含日期、机构、评级及原文链接 |
| 个股新闻 | 东方财富关键词搜索，最多 20 条；可能含相关市场新闻，不保证每条只涉及该股票 |
| A 股快讯 | 华尔街见闻 A 股频道，最多 20 条，全市场范围 |
| 行情备用入口 | 腾讯三个同源网关，网络或解析失败后依次尝试，单次超时 12 秒 |

图表下方选择资讯类型按需加载；切换自选会加载对应股票，快讯保持全市场口径。
资讯请求串行执行，间隔至少 2 秒；同一接口/股票每分钟最多刷新一次，成功结果缓存 60 秒。
资讯与行情分别刷新，不会每 15 秒批量请求全部新闻。每条展示发布时间，并标注数据获取时间。
接口缺字段、限流或网络失败明确显示错误；不会当作“没有相关新闻”。
2026-10-13 初测个股新闻出现仅 `passportWeb` 的异常响应，已覆盖此异常处理；
修正原生请求的 JSON 查询参数编码后，最终桌面联网验证新闻、研报、快讯各返回 20 条。
该项目其余财务、宏观、资金流等接口尚未接入。腾讯多网关不等于多个独立行情供应商。

扩展功能联网冒烟验证（隔离设置，夜间主题下依次截图分时 + 五档 + 分笔、周 K + MACD、15 分钟 K + KDJ、日 K + BOLL + RSI、个股资料、科创板分时，最后截日间主题，输出 `feature-smoke.json`）：

```powershell
./ashare-dist/AShareBoard.exe -platform offscreen --feature-smoke-output ./ashare-build/feature-verified
```

全部检查通过时退出码为 0，有检查未通过为 5，超过 4 分钟未完成为 3。科创板检查要求分时均价与现价之比在 0.9～1.1 之间。

联网资讯冒烟验证（隔离自选设置，输出各面板截图与 JSON 结果）：

```powershell
./ashare-dist/AShareBoard.exe -platform offscreen --research-smoke-output ./ashare-build/research-verified
```

## Windows 构建

独立目标需要 Qt 6.8+ Widgets / Network / Test、C++20 编译器、CMake 3.27+ 和 Ninja。
已使用 Qt 6.8.3 + MinGW 13.1 的便携工具链进行本地构建；完整上游的 MSVC 构建要求见 `docs/GETTING_STARTED.md`。

工具链可安装在仓库 `.tools`（不入 Git）：

```powershell
python -m venv .tools/venv
./.tools/venv/Scripts/python.exe -m pip install aqtinstall cmake==3.27.7 ninja==1.11.1.1
./.tools/venv/Scripts/python.exe -m aqt install-qt windows desktop 6.8.3 win64_mingw --archives qtbase -O .tools/Qt
./.tools/venv/Scripts/python.exe -m aqt install-tool windows desktop tools_mingw1310 -O .tools/Qt
./fincept-qt/ashare/build.ps1 -Package
./ashare-dist/AShareBoard.exe
```

`build.ps1` 也支持传入 `-QtPrefix` 和 `-CompilerBin`。便携构建目录为 `ashare-build/native`，运行目录为 `ashare-dist`。
中文源码目录会使用 `%LOCALAPPDATA%/AShareBoardSource` 目录联接构建（源码不移动）。
若当前环境无法经由该目录联接写入（2026-10-15 在 Claude 桌面端会话中出现，`%LOCALAPPDATA%` 写入被重定向），
可临时映射一个 ASCII 盘符后直接运行同一脚本，完成后取消映射：

```powershell
subst Q: "D:\USYD\工作\诺亚\fincept-ashare"
powershell -File Q:\fincept-qt\ashare\build.ps1 -Package
subst Q: /d
```

在 Windows PowerShell 5.1 中请勿把 `build.ps1 -Package` 的输出管道到其他命令：windeployqt 的警告写入 stderr，
在脚本 `$ErrorActionPreference = 'Stop'` 下会被当作错误中止。直接运行或用 `powershell -File` 运行即可。

### 完整工作台源码构建

`build-workbench.ps1` 编译上游完整 `FinceptTerminal` 目标，和独立目标分别使用
`ashare-build/workbench`、`ashare-build/native`，不会自动覆盖已安装程序。
完整目标还需 Qt Charts、Multimedia、WebSockets、LinguistTools 及匹配 MinGW 的 OpenSSL 开发包。
脚本接受 `-QtPrefix`、`-CompilerBin`、`-OpenSSLRoot`，默认单任务编译以限制内存占用。
无 WebEngine 时使用上游原有降级路径；Python 分析、账号、付费服务和交易连接仍按上游要求配置，
A 股看板接入并不代表其他模块的数据源已全部适配 A 股。

```powershell
./fincept-qt/ashare/build-workbench.ps1
# 完整终端构造检查，可按模块缩小范围；沿用上游 PIN 锁定检查。
$env:FINCEPT_SMOKE_SCREENS = 'ashare,notes'
$env:FINCEPT_SMOKE_OUTPUT = "$PWD/ashare-build/workbench-smoke"
./ashare-build/workbench/FinceptTerminal.exe --profile ashare-validation --smoke-test -platform offscreen
```
本机同时准备了 `%LOCALAPPDATA%/AShareBoardToolchain/mingw1310_64` 编译工具，脚本会优先使用它。
脚本传入 `-no-canonical-prefixes`，避免 MinGW 将英文目录联接还原为不能链接的中文路径。
本机个人自选存储在 Windows 用户配置 `AShareBoard/Personal`，不向上游账号同步。

## 代码规范与 CI

- A 股目标默认开启与上游 CI 相同的告警策略：GCC/Clang 为 `-Wall -Wextra -Wpedantic -Werror`，MSVC 为 `/W4 /permissive- /WX`
  （CMake 选项 `ASHARE_WERROR`，默认 ON）。2026-10-16 曾因结构体聚合初始化缺字段导致 Linux/macOS 构建失败，
  现在所有 A 股数据结构成员都带默认初始化，本地 MinGW 也会按错误处理同类告警。
- 格式：`.tools/venv` 中安装 `clang-format==18.1.3`，按 `fincept-qt/.clang-format` 格式化：

```powershell
./.tools/venv/Scripts/clang-format.exe -i --style=file (Get-ChildItem fincept-qt/src/screens/ashare,fincept-qt/src/services/ashare -File)
```

- CI：`.github/workflows/ashare.yml` 在 Windows（MinGW）、Linux（GCC）、macOS（Clang）上构建独立看板并运行 `AShareTests`，
  另检查 A 股源码格式；并在 Linux 上以 AddressSanitizer + UndefinedBehaviorSanitizer 编译看板与测试后运行 `AShareTests`
  （只插桩 A 股代码，Qt 为普通构建，关闭泄漏检测），检查越界、释放后使用、整数溢出等问题。
  上游 Fincept 的工作流已移到 `.github/workflows-upstream/`，不再执行。
- `AShareScreen` 按职责拆分为 `AShareScreen.cpp`（框架、视图、偏好、热键）、`AShareScreen_Watchlist.cpp`（自选、分组、指数条）、
  `AShareScreen_Stock.cpp`（个股页、图表控制、AI 数据快照），共用辅助函数在 `AShareScreen_internal.h`。

## 验证

`AShareTests`（43 项）覆盖代码识别、交易时段、分时排序和单位换算、跨交易日隔离、前复权约束、空数据与横盘绘图，
以及扩展快照字段与科创板单位、多周期与复权口径、分笔解析、指标参考值、F10 解析与 HTML 转义、
副图绘制、盘口计数（含分笔先于行情到达）、周期状态恢复、自选排序 / 列显示 / 分组持久化，
以及涨跌分布、板块、成分股、涨跌停池、龙虎榜解析，大盘面板渲染与双击打开个股、个股 / 大盘视图切换与状态恢复。
还覆盖 AI 请求体与端点、SSE 流解析与思考内容隐藏、DPAPI 密钥加密存储、本地模拟服务端的流式问答（默认不发送持仓），
持仓加权平均成本 / 已实现盈亏 / 超卖拒绝 / 损坏文件不覆盖、矩形树图面积、笔记筛选与持久化、四个视图切换与 AI 数据快照。

2026-10-15 验证记录：29 项通过，2 项联网用例按设计跳过；交易时段内对打包后的 `ashare-dist/AShareBoard.exe` 运行扩展功能联网冒烟，
各项检查通过（分笔 40 条、周 K 320 根、15 分钟 K 320 根、日 K 401 根、F10 财务 9 期）。
真实网络烟雾测试（请从含 Qt 运行库的环境启动）：

```powershell
./ashare-dist/AShareBoard.exe -platform offscreen --smoke-output ./ashare-build/live-check
```

成功会生成 `smoke.json`、`intraday.png` 和 `daily.png`；30 秒无完整行情返回则退出码为 3。
烟雾测试使用独立设置目录，不覆盖个人自选。

## 上游与许可

上游：https://github.com/Fincept-Corporation/FinceptTerminal

保留上游 LICENSE 和版权声明。本分支用于个人自用；并非 Fincept 官方产品。
上游 LICENSE 含 AGPL 标记及附加条款，对外发布、公司使用或分发前应另行核对授权。
本地运行包用于个人验证，不能据此视为已完成第三方软件分发合规审查。

## 名称搜索

自选输入框支持名称片段（例如“茅台”“平安”）、拼音首字母（例如 `gzmt`）和代码。输入后停顿 350 毫秒查询腾讯候选接口，点击候选直接加入自选；候选显示股票名称和交易所代码，过滤非 A 股。完整六位代码仍可直接添加。候选由公开接口返回，可能不完整，找不到时可补全名称或直接输入代码；断网时保留代码添加能力。
