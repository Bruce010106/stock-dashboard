import type {
  DailyMarketBar,
  MarketDataProvider,
  MarketSnapshot,
  MinuteMarketBar,
  StockInstrument,
} from './market-data-provider.ts';
import { TencentMarketDataProvider } from './tencent-provider.ts';
import { isTushareConfigured, TushareMarketDataProvider } from './tushare-provider.ts';

export class CompositeAStockDataProvider implements MarketDataProvider {
  readonly name = 'a-stock-data / 腾讯 + Tushare';
  private readonly realtime = new TencentMarketDataProvider();
  private readonly history = new TushareMarketDataProvider();

  get historyMode(): 'tushare' | 'tencent-fallback' {
    return isTushareConfigured() ? 'tushare' : 'tencent-fallback';
  }

  getUniverse(asOfDate: string): Promise<StockInstrument[]> {
    return this.realtime.getUniverse(asOfDate);
  }

  async getDailyBars(
    codes: string[],
    startDate: string,
    endDate: string,
  ): Promise<DailyMarketBar[]> {
    if (isTushareConfigured()) {
      try {
        return await this.history.getDailyBars(codes, startDate, endDate);
      } catch (error) {
        console.warn('Tushare 日线不可用，降级到腾讯日线', error);
      }
    }
    return this.realtime.getDailyBars(codes, startDate, endDate);
  }

  getMinuteBars(codes: string[], date: string): Promise<MinuteMarketBar[]> {
    return this.realtime.getMinuteBars(codes, date);
  }

  getSnapshots(codes: string[]): Promise<MarketSnapshot[]> {
    return this.realtime.getSnapshots(codes);
  }
}

export const marketDataProvider = new CompositeAStockDataProvider();

