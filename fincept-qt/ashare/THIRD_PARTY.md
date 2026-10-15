# a-stock-data integration

Source: https://github.com/simonlin1212/a-stock-data

Reviewed revision: `29b07d2e37005b9fd568bca1262f6e572b5f9407` (v3.10.2).
License: Apache-2.0, reproduced in `third-party/a-stock-data-LICENSE.txt`.
The upstream repository has no NOTICE file at this revision.

The following examples from its SKILL.md were adapted into native C++/Qt:

- `eastmoney_reports`: latest 20 stock research reports, with original report links.
- `eastmoney_stock_news`: latest 20 keyword news results; missing article arrays are
  treated as provider errors instead of a claim that no news exists.
- `wallstreetcn_lives`: latest 20 entries in the A-stock channel; market-wide content.
- Tencent gateway list and STAR historical candle volume unit correction.

Modifications: asynchronous Qt requests, strict response validation, escaped display
text, HTTP(S)-only links, on-demand UI, one-minute memory cache/cooldown, serialized
research requests with a two-second interval, and stale-selection response guards.
The skill is not executed or dynamically downloaded at runtime. Python is not
required by the packaged application. Other upstream capabilities are not integrated.

Tencent's three gateways share a provider; they are not independent data sources.
Public endpoints offer no guaranteed availability or latency. Upstream code licensing
does not grant additional redistribution rights to data obtained from third parties.
The host application's existing LICENSE remains unchanged.
