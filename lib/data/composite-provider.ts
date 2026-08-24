import type {
  DailyMarketBar,
  MarketDataProvider,
  MarketSnapshot,
  MinuteMarketBar,
  StockInstrument,
} from './market-data-provider.ts';
import { TencentMarketDataProvider } from './tencent-provider.ts';
import { isTushareConfigured, TushareMarketDataProvider } from './tushare-provider.ts';

export type HistoryMode = 'tushare' | 'tencent-fallback';

export type DailyBarsWithSource = {
  bars: DailyMarketBar[];
  historyMode: HistoryMode;
  isFallback: boolean;
  warning?: string;
};

export class CompositeAStockDataProvider implements MarketDataProvider {
  readonly name = 'a-stock-data / 腾讯 + Tushare';
  private readonly realtime = new TencentMarketDataProvider();
  private readonly history = new TushareMarketDataProvider();

  /**
   * Returns the configured default only. A completed history request can
   * still fall back at runtime, so callers that need an accurate result
   * should use getDailyBarsWithSource() instead.
   */
  get historyMode(): HistoryMode {
    return isTushareConfigured() ? 'tushare' : 'tencent-fallback';
  }

  getUniverse(asOfDate: string): Promise<StockInstrument[]> {
    return this.realtime.getUniverse(asOfDate);
  }

  async getDailyBarsWithSource(
    codes: string[],
    startDate: string,
    endDate: string,
  ): Promise<DailyBarsWithSource> {
    if (!isTushareConfigured()) {
      return {
        bars: await this.realtime.getDailyBars(codes, startDate, endDate),
        historyMode: 'tencent-fallback',
        isFallback: true,
        warning: '未配置 Tushare，近30日涨停使用腾讯不复权日线按板块涨停幅度识别',
      };
    }

    try {
      return {
        bars: await this.history.getDailyBars(codes, startDate, endDate),
        historyMode: 'tushare',
        isFallback: false,
      };
    } catch (error) {
      const reason = error instanceof Error ? error.message : '未知错误';
      const warning = `Tushare 日线请求失败，已降级到腾讯日线：${reason}`;
      console.warn(warning);
      return {
        bars: await this.realtime.getDailyBars(codes, startDate, endDate),
        historyMode: 'tencent-fallback',
        isFallback: true,
        warning,
      };
    }
  }

  async getDailyBars(
    codes: string[],
    startDate: string,
    endDate: string,
  ): Promise<DailyMarketBar[]> {
    const result = await this.getDailyBarsWithSource(codes, startDate, endDate);
    return result.bars;
  }

  getMinuteBars(codes: string[], date: string): Promise<MinuteMarketBar[]> {
    return this.realtime.getMinuteBars(codes, date);
  }

  getSnapshots(codes: string[]): Promise<MarketSnapshot[]> {
    return this.realtime.getSnapshots(codes);
  }
}

export const marketDataProvider = new CompositeAStockDataProvider();

