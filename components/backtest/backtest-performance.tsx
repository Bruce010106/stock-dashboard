'use client';

import { useMemo, useState } from 'react';
import {
  buildPerformanceSeries,
  filterPerformanceSignals,
  summarizePerformance,
  type PerformancePoint,
  type PerformanceRange,
  type PerformanceSignal,
} from '../../lib/backtest/chart-utils.ts';
import styles from './backtest-performance.module.css';

type BacktestPerformanceProps = {
  signals: readonly PerformanceSignal[];
  backtestEndDate?: string;
};

type ChartMetric = 'cumulativeReturnPct' | 'drawdownPct';

const RANGE_OPTIONS: ReadonlyArray<{ value: PerformanceRange; label: string }> = [
  { value: 'all', label: '全部' },
  { value: '30d', label: '近 30 日' },
  { value: '60d', label: '近 60 日' },
  { value: '90d', label: '近 90 日' },
];

const CHART_WIDTH = 760;
const CHART_HEIGHT = 230;
const CHART_PADDING = { top: 18, right: 14, bottom: 28, left: 42 };

function signedPercent(value: number): string {
  return `${value >= 0 ? '+' : ''}${value.toFixed(2)}%`;
}

function axisPercent(value: number): string {
  return `${value >= 0 ? '+' : ''}${value.toFixed(1)}%`;
}

function formatDate(date: string | null): string {
  return date ?? '暂无';
}

function chartDomain(values: readonly number[]): { min: number; max: number } {
  if (values.length === 0) return { min: -1, max: 1 };
  const minValue = Math.min(0, ...values);
  const maxValue = Math.max(0, ...values);
  if (minValue === maxValue) {
    const padding = Math.max(1, Math.abs(minValue) * 0.2);
    return { min: minValue - padding, max: maxValue + padding };
  }
  const padding = (maxValue - minValue) * 0.12;
  return { min: minValue - padding, max: maxValue + padding };
}

function chartY(value: number, domain: { min: number; max: number }): number {
  const plotHeight = CHART_HEIGHT - CHART_PADDING.top - CHART_PADDING.bottom;
  return CHART_PADDING.top + (domain.max - value) / (domain.max - domain.min) * plotHeight;
}

function chartX(index: number, count: number): number {
  const plotWidth = CHART_WIDTH - CHART_PADDING.left - CHART_PADDING.right;
  return CHART_PADDING.left + (count <= 1 ? plotWidth / 2 : index / (count - 1) * plotWidth);
}

function pointString(point: PerformancePoint, index: number, count: number, metric: ChartMetric, domain: { min: number; max: number }): string {
  return `${chartX(index, count)},${chartY(point[metric], domain)}`;
}

function PerformanceChart({ series, metric, color, label }: { series: readonly PerformancePoint[]; metric: ChartMetric; color: string; label: string }) {
  const values = series.map((point) => point[metric]);
  const domain = chartDomain(values);
  const zeroY = chartY(0, domain);
  const linePoints = series.map((point, index) => pointString(point, index, series.length, metric, domain)).join(' ');
  const first = series[0];
  const last = series.at(-1);
  const midIndex = Math.floor((series.length - 1) / 2);

  return (
    <div className={styles.chartFrame}>
      <div className={styles.axisLabels} aria-hidden="true">
        <span>{axisPercent(domain.max)}</span>
        <span>{axisPercent(0)}</span>
        <span>{axisPercent(domain.min)}</span>
      </div>
      <svg className={styles.chart} viewBox={`0 0 ${CHART_WIDTH} ${CHART_HEIGHT}`} role="img" aria-label={label} preserveAspectRatio="none">
        <line className={styles.gridLine} x1={CHART_PADDING.left} x2={CHART_WIDTH - CHART_PADDING.right} y1={chartY(domain.max, domain)} y2={chartY(domain.max, domain)} />
        <line className={styles.gridLine} x1={CHART_PADDING.left} x2={CHART_WIDTH - CHART_PADDING.right} y1={zeroY} y2={zeroY} />
        <line className={styles.gridLine} x1={CHART_PADDING.left} x2={CHART_WIDTH - CHART_PADDING.right} y1={chartY(domain.min, domain)} y2={chartY(domain.min, domain)} />
        <polyline className={styles.chartLine} points={linePoints} stroke={color} />
        {series.map((point, index) => (
          <circle key={`${point.date}-${index}`} cx={chartX(index, series.length)} cy={chartY(point[metric], domain)} r={index === 0 || index === series.length - 1 ? 4 : 2.5} fill={color}>
            <title>{`${point.date} · ${signedPercent(point[metric])} · ${point.signalCount} 个信号`}</title>
          </circle>
        ))}
      </svg>
      <div className={styles.xAxis} aria-hidden="true">
        <span>{first?.date ?? '暂无数据'}</span>
        {series.length > 2 ? <span>{series[midIndex].date}</span> : <span />}
        <span>{last?.date ?? '—'}</span>
      </div>
    </div>
  );
}

function SummaryMetric({ label, value, detail, tone }: { label: string; value: string; detail: string; tone?: 'positive' | 'negative' }) {
  return (
    <div className={styles.summaryMetric}>
      <span>{label}</span>
      <strong className={tone ? styles[tone] : undefined}>{value}</strong>
      <small>{detail}</small>
    </div>
  );
}

export default function BacktestPerformance({ signals, backtestEndDate }: BacktestPerformanceProps) {
  const [range, setRange] = useState<PerformanceRange>('all');
  const filteredSignals = useMemo(
    () => filterPerformanceSignals(signals, range, backtestEndDate),
    [backtestEndDate, range, signals],
  );
  const series = useMemo(() => buildPerformanceSeries(filteredSignals), [filteredSignals]);
  const summary = useMemo(() => summarizePerformance(filteredSignals, series), [filteredSignals, series]);
  const hasData = filteredSignals.length > 0 && series.length > 0;
  const cumulativeTone = summary.cumulativeReturnPct >= 0 ? 'positive' : 'negative';

  return (
    <section className={styles.card} aria-labelledby="backtest-performance-title">
      <div className={styles.header}>
        <div>
          <p className="eyebrow">Performance / completed signals</p>
          <h2 id="backtest-performance-title">累计收益与回撤</h2>
          <p className={styles.copy}>仅根据接口返回的完整信号收益按信号日顺序复合，指数基准未随本次回测返回。</p>
        </div>
        <div className={styles.rangeTabs} role="tablist" aria-label="回测曲线区间">
          {RANGE_OPTIONS.map((option) => (
            <button
              className={range === option.value ? styles.rangeActive : styles.rangeButton}
              key={option.value}
              type="button"
              role="tab"
              aria-selected={range === option.value}
              onClick={() => setRange(option.value)}
            >
              {option.label}
            </button>
          ))}
        </div>
      </div>

      {!hasData ? (
        <div className={styles.empty} role="status">
          <strong>当前区间没有可计算的完整信号</strong>
          <span>真实回测未返回完成收益，因此不绘制累计收益、回撤或替代基准数据。</span>
        </div>
      ) : (
        <>
          <div className={styles.summaryGrid}>
            <SummaryMetric
              label="信号复合收益"
              value={signedPercent(summary.cumulativeReturnPct)}
              detail={`${formatDate(summary.startDate)} — ${formatDate(summary.endDate)}`}
              tone={cumulativeTone}
            />
            <SummaryMetric
              label="最大回撤"
              value={signedPercent(summary.maxDrawdownPct)}
              detail={`样本 ${summary.signalCount} 个完成信号`}
              tone={summary.maxDrawdownPct < 0 ? 'negative' : undefined}
            />
            <SummaryMetric
              label="区间胜率"
              value={`${summary.winRatePct.toFixed(1)}%`}
              detail={`盈利 ${summary.winningSignalCount} / ${summary.signalCount}`}
            />
            <SummaryMetric
              label="平均单信号"
              value={signedPercent(summary.averageReturnPct)}
              detail={`最好 ${signedPercent(summary.bestReturnPct)} · 最差 ${signedPercent(summary.worstReturnPct)}`}
              tone={summary.averageReturnPct >= 0 ? 'positive' : 'negative'}
            />
          </div>

          <div className={styles.chartGrid}>
            <article className={styles.chartCard}>
              <div className={styles.chartHeader}>
                <div><strong>累计收益曲线</strong><span>指数 = 100</span></div>
                <b className={cumulativeTone === 'positive' ? styles.positive : styles.negative}>{signedPercent(summary.cumulativeReturnPct)}</b>
              </div>
              <PerformanceChart series={series} metric="cumulativeReturnPct" color="#176b47" label="策略累计收益曲线" />
            </article>
            <article className={styles.chartCard}>
              <div className={styles.chartHeader}>
                <div><strong>回撤曲线</strong><span>相对历史峰值</span></div>
                <b className={styles.negative}>{signedPercent(summary.maxDrawdownPct)}</b>
              </div>
              <PerformanceChart series={series} metric="drawdownPct" color="#c54135" label="策略回撤曲线" />
            </article>
          </div>

          <div className={styles.benchmark}>
            <div className={styles.benchmarkTitle}><span className={styles.benchmarkDot} />基准对照</div>
            <strong>暂无</strong>
            <p>当前真实回测响应没有返回沪深 300 或其他指数的同期净值，页面不生成替代基准曲线。</p>
          </div>
          <p className={styles.disclaimer}>口径：曲线以 100 为起点，将同一信号日的已完成收益复合后按信号日排序；未包含资金权重、交易费用、持仓重叠和每日净值。</p>
        </>
      )}
    </section>
  );
}
