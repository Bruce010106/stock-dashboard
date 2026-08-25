import styles from './Screener.module.css';
import type { MiniBar } from './types';

type MiniCandlestickProps = {
  name: string;
  bars?: MiniBar[];
  changePct: number;
};

const WIDTH = 116;
const HEIGHT = 42;
const PADDING_X = 4;
const PADDING_Y = 4;

function finiteBars(bars: MiniBar[] | undefined): MiniBar[] {
  return (bars ?? []).filter((bar) =>
    [bar.open, bar.high, bar.low, bar.close].every((value) => Number.isFinite(value)) &&
    bar.high >= bar.low && bar.open >= bar.low && bar.open <= bar.high &&
    bar.close >= bar.low && bar.close <= bar.high,
  );
}

function formatChange(value: number): string {
  return `${value >= 0 ? '+' : ''}${value.toFixed(2)}%`;
}

export function MiniCandlestick({ name, bars, changePct }: MiniCandlestickProps) {
  const validBars = finiteBars(bars);
  if (validBars.length === 0) {
    return (
      <div
        className={styles.miniFallback}
        role="img"
        aria-label={`${name}缺少历史日线，仅显示当前涨跌幅 ${formatChange(changePct)}`}
        title="上游未返回日线序列；此处不生成走势"
      >
        <span className={changePct >= 0 ? styles.miniPositive : styles.miniNegative}>
          {formatChange(changePct)}
        </span>
        <small>日线未返回</small>
      </div>
    );
  }

  const minPrice = Math.min(...validBars.map((bar) => bar.low));
  const maxPrice = Math.max(...validBars.map((bar) => bar.high));
  const range = Math.max(maxPrice - minPrice, Math.abs(maxPrice) * 0.001, 0.01);
  const plotWidth = WIDTH - PADDING_X * 2;
  const plotHeight = HEIGHT - PADDING_Y * 2;
  const step = validBars.length === 1 ? 0 : plotWidth / (validBars.length - 1);
  const bodyWidth = Math.min(6, Math.max(3, plotWidth / validBars.length * 0.52));
  const y = (price: number) => PADDING_Y + ((maxPrice - price) / range) * plotHeight;

  return (
    <svg
      className={styles.miniChart}
      viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
      role="img"
      aria-label={`${name}近 ${validBars.length} 个交易日日线迷你K线，当前涨跌幅 ${formatChange(changePct)}`}
    >
      <title>{`${name}近${validBars.length}个交易日日线`}</title>
      {validBars.map((bar, index) => {
        const x = PADDING_X + step * index;
        const rising = bar.close >= bar.open;
        const bodyTop = y(Math.max(bar.open, bar.close));
        const bodyBottom = y(Math.min(bar.open, bar.close));
        return (
          <g key={`${bar.date}-${index}`}>
            <line
              x1={x}
              x2={x}
              y1={y(bar.high)}
              y2={y(bar.low)}
              className={rising ? styles.miniRise : styles.miniFall}
              strokeWidth="1"
            />
            <rect
              x={x - bodyWidth / 2}
              y={bodyTop}
              width={bodyWidth}
              height={Math.max(1.5, bodyBottom - bodyTop)}
              rx="0.8"
              className={rising ? styles.miniRise : styles.miniFall}
            />
          </g>
        );
      })}
    </svg>
  );
}
