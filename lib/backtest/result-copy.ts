/**
 * Pure copy helpers for the backtest page. A completed run's accuracy is
 * decided by the server response's isApproximate flag, never the client's
 * own Tushare-detection guess, so a free (Sina) result is never mislabeled
 * as a strict/point-in-time one.
 */
export type BacktestSourceMode = 'tushare' | 'free' | 'detecting';

export function backtestSourceMode(
  hasResult: boolean,
  resultIsApproximate: boolean,
  tushareConfigured: boolean | null,
): BacktestSourceMode {
  if (hasResult) return resultIsApproximate ? 'free' : 'tushare';
  if (tushareConfigured === null) return 'detecting';
  return tushareConfigured ? 'tushare' : 'free';
}

export function signalPriceBasisCopy(mode: BacktestSourceMode): string {
  if (mode === 'tushare') return '信号日最后 1 分钟收盘价';
  if (mode === 'free') return '信号日最后一个 5 分钟 K 线收盘价';
  return '信号日最后 1 分钟收盘价（Tushare 模式）或最后一个 5 分钟 K 线收盘价（新浪免费模式，检测完成后确定）';
}

export function resultSignalWord(isApproximate: boolean): '近似' | '严格' {
  return isApproximate ? '近似' : '严格';
}
