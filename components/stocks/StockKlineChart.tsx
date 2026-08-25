'use client';

import { useMemo, useRef, useState } from 'react';
import styles from './stock-kline-chart.module.css';

export type KlineBar = {
  date: string;
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
  isLimitUp?: boolean;
};

type RangeKey = '1M' | '3M' | '6M' | '1Y';

const RANGE_BARS: Record<RangeKey, number> = { '1M': 22, '3M': 66, '6M': 132, '1Y': 250 };
const RANGE_OPTIONS = Object.keys(RANGE_BARS) as RangeKey[];

function movingAverage(bars: KlineBar[], window: number): Array<number | null> {
  let sum = 0;
  return bars.map((bar, index) => {
    sum += bar.close;
    if (index >= window) sum -= bars[index - window].close;
    return index >= window - 1 ? sum / window : null;
  });
}

function pathFor(values: Array<number | null>, x: (index: number) => number, y: (value: number) => number): string {
  let drawing = false;
  return values.map((value, index) => {
    if (value == null) {
      drawing = false;
      return '';
    }
    const command = drawing ? 'L' : 'M';
    drawing = true;
    return `${command}${x(index).toFixed(2)},${y(value).toFixed(2)}`;
  }).join(' ');
}

export function StockKlineChart({ bars, signalDate }: { bars: KlineBar[]; signalDate?: string }) {
  const [range, setRange] = useState<RangeKey>('3M');
  const [hovered, setHovered] = useState<number | null>(null);
  const svgRef = useRef<SVGSVGElement>(null);
  const visible = useMemo(() => bars.slice(-RANGE_BARS[range]), [bars, range]);

  const width = 960;
  const height = 440;
  const left = 54;
  const right = 18;
  const top = 24;
  const priceBottom = 326;
  const volumeTop = 350;
  const volumeBottom = 414;
  const plotWidth = width - left - right;
  const priceValues = visible.flatMap((bar) => [bar.low, bar.high]);
  const minPrice = Math.min(...priceValues);
  const maxPrice = Math.max(...priceValues);
  const pricePadding = Math.max((maxPrice - minPrice) * 0.08, maxPrice * 0.005, 0.01);
  const low = minPrice - pricePadding;
  const high = maxPrice + pricePadding;
  const maxVolume = Math.max(...visible.map((bar) => bar.volume), 1);
  const step = plotWidth / Math.max(visible.length, 1);
  const candleWidth = Math.max(2, Math.min(9, step * 0.62));
  const x = (index: number) => left + step * index + step / 2;
  const y = (value: number) => top + (high - value) / Math.max(high - low, 0.01) * (priceBottom - top);
  const volumeY = (value: number) => volumeBottom - value / maxVolume * (volumeBottom - volumeTop);
  const ma5 = movingAverage(visible, 5);
  const ma10 = movingAverage(visible, 10);
  const ma20 = movingAverage(visible, 20);
  const active = hovered == null ? visible.at(-1) : visible[hovered];

  const handlePointerMove = (event: React.PointerEvent<SVGSVGElement>) => {
    const box = svgRef.current?.getBoundingClientRect();
    if (!box || visible.length === 0) return;
    const chartX = (event.clientX - box.left) / box.width * width;
    const index = Math.max(0, Math.min(visible.length - 1, Math.floor((chartX - left) / step)));
    setHovered(index);
  };

  if (visible.length === 0) return <div className={styles.empty}>暂无K线数据</div>;

  return (
    <figure className={styles.figure} aria-label="股票日K图">
      <div className={styles.toolbar}>
        <div className={styles.legend}>
          <span className={styles.ma5}>MA5</span><span className={styles.ma10}>MA10</span><span className={styles.ma20}>MA20</span>
        </div>
        <div className={styles.ranges} aria-label="K线时间范围">
          {RANGE_OPTIONS.map((option) => (
            <button key={option} type="button" aria-pressed={range === option} onClick={() => setRange(option)}>{option}</button>
          ))}
        </div>
      </div>
      <div className={styles.quoteLine}>
        {active ? <><strong>{active.date}</strong><span>开 {active.open.toFixed(2)}</span><span>高 {active.high.toFixed(2)}</span><span>低 {active.low.toFixed(2)}</span><span>收 {active.close.toFixed(2)}</span><span>量 {Math.round(active.volume).toLocaleString('zh-CN')}</span></> : null}
      </div>
      <svg
        ref={svgRef}
        viewBox={`0 0 ${width} ${height}`}
        className={styles.chart}
        role="img"
        aria-label={`${visible[0]?.date} 至 ${visible.at(-1)?.date} 日K线`}
        onPointerMove={handlePointerMove}
        onPointerLeave={() => setHovered(null)}
      >
        {[0, 0.25, 0.5, 0.75, 1].map((ratio) => {
          const lineY = top + ratio * (priceBottom - top);
          const price = high - ratio * (high - low);
          return <g key={ratio}><line x1={left} x2={width - right} y1={lineY} y2={lineY} className={styles.grid} /><text x={left - 8} y={lineY + 4} className={styles.axis} textAnchor="end">{price.toFixed(2)}</text></g>;
        })}
        <line x1={left} x2={width - right} y1={volumeBottom} y2={volumeBottom} className={styles.grid} />
        {visible.map((bar, index) => {
          const rising = bar.close >= bar.open;
          const colorClass = rising ? styles.rising : styles.falling;
          const bodyTop = y(Math.max(bar.open, bar.close));
          const bodyHeight = Math.max(1.2, Math.abs(y(bar.open) - y(bar.close)));
          return (
            <g key={bar.date} className={colorClass}>
              <line x1={x(index)} x2={x(index)} y1={y(bar.high)} y2={y(bar.low)} />
              <rect x={x(index) - candleWidth / 2} y={bodyTop} width={candleWidth} height={bodyHeight} />
              <rect x={x(index) - candleWidth / 2} y={volumeY(bar.volume)} width={candleWidth} height={volumeBottom - volumeY(bar.volume)} opacity="0.46" />
              {bar.isLimitUp ? <circle cx={x(index)} cy={y(bar.high) - 7} r="3" className={styles.limitDot} /> : null}
              {signalDate === bar.date ? <path d={`M ${x(index)} ${y(bar.high) - 22} l -6 -9 h 12 z`} className={styles.signal} /> : null}
            </g>
          );
        })}
        <path d={pathFor(ma5, x, y)} className={`${styles.maLine} ${styles.ma5Line}`} />
        <path d={pathFor(ma10, x, y)} className={`${styles.maLine} ${styles.ma10Line}`} />
        <path d={pathFor(ma20, x, y)} className={`${styles.maLine} ${styles.ma20Line}`} />
        {hovered != null ? <line x1={x(hovered)} x2={x(hovered)} y1={top} y2={volumeBottom} className={styles.crosshair} /> : null}
        <text x={left} y={height - 7} className={styles.axis}>{visible[0]?.date}</text>
        <text x={width - right} y={height - 7} className={styles.axis} textAnchor="end">{visible.at(-1)?.date}</text>
      </svg>
      <figcaption className={styles.caption}><span><i className={styles.limitKey} />历史涨停</span><span><i className={styles.signalKey} />当前策略严格信号</span></figcaption>
    </figure>
  );
}
