export type DailyBar = {
  date: string;
  close: number;
  previousClose?: number;
  limitUpPrice?: number;
  isLimitUp?: boolean;
};

export type MinuteBar = {
  time: string;
  high: number;
  low: number;
  close: number;
};

export type YangYongxingCandidate = {
  code: string;
  name: string;
  changePct: number;
  totalMarketCapYuan: number;
  volumeRatio: number;
  turnoverRatePct: number;
  recentDailyBars: DailyBar[];
  minuteBars: MinuteBar[];
};

export type RuleCheck = {
  key: string;
  label: string;
  passed: boolean;
  actual: string;
  expected: string;
};

export type IntradayPattern = {
  passed: boolean;
  breakoutTime?: string;
  breakoutLevel?: number;
  sessionHigh?: number;
  pullbackLow?: number;
  reason: string;
};

export type YangYongxingResult = {
  code: string;
  name: string;
  passed: boolean;
  score: number;
  checks: RuleCheck[];
  intraday: IntradayPattern;
};

export const YANG_YONGXING_RULES = {
  minChangePct: 3,
  maxChangePct: 5,
  lookbackTradingDays: 30,
  maxMarketCapYuan: 20_000_000_000,
  minVolumeRatioExclusive: 1,
  minTurnoverRatePct: 5,
  maxTurnoverRatePct: 10,
  breakoutStartTime: '14:30',
} as const;

function round(value: number, digits = 2): string {
  return value.toFixed(digits);
}

function isLimitUp(bar: DailyBar): boolean {
  if (typeof bar.isLimitUp === 'boolean') return bar.isLimitUp;
  if (bar.limitUpPrice && bar.limitUpPrice > 0) {
    return bar.close >= bar.limitUpPrice * 0.999;
  }
  return false;
}

export function evaluateTailPattern(minuteBars: MinuteBar[]): IntradayPattern {
  const bars = [...minuteBars]
    .filter((bar) => /^\d{2}:\d{2}$/.test(bar.time))
    .sort((a, b) => a.time.localeCompare(b.time));

  const beforeBreakoutWindow = bars.filter(
    (bar) => bar.time < YANG_YONGXING_RULES.breakoutStartTime,
  );
  const tailBars = bars.filter(
    (bar) => bar.time >= YANG_YONGXING_RULES.breakoutStartTime,
  );

  if (beforeBreakoutWindow.length === 0 || tailBars.length < 2) {
    return {
      passed: false,
      reason: '分钟数据不足，无法验证 14:30 后突破与回踩',
    };
  }

  const breakoutLevel = Math.max(...beforeBreakoutWindow.map((bar) => bar.high));
  const breakoutIndex = tailBars.findIndex((bar) => bar.high > breakoutLevel);

  if (breakoutIndex < 0) {
    return {
      passed: false,
      breakoutLevel,
      reason: '14:30 后未突破此前日内最高价',
    };
  }

  const breakoutBar = tailBars[breakoutIndex];
  const confirmationBars = tailBars.slice(breakoutIndex + 1);
  if (confirmationBars.length === 0) {
    return {
      passed: false,
      breakoutTime: breakoutBar.time,
      breakoutLevel,
      sessionHigh: breakoutBar.high,
      reason: '突破发生过晚，没有后续分钟线验证回踩承接',
    };
  }

  const sessionHigh = Math.max(
    breakoutBar.high,
    ...confirmationBars.map((bar) => bar.high),
  );
  const pullbackLow = Math.min(...confirmationBars.map((bar) => bar.low));

  // A pullback needs to follow a high that has actually been established.
  // Comparing lows with the final session high is not sufficient: in a
  // steadily rising sequence every earlier low is below that final high,
  // even though price never retreated from a local high.
  let peakHigh = breakoutBar.high;
  let hasPullback = false;
  for (const bar of confirmationBars) {
    if (bar.high > peakHigh) {
      peakHigh = bar.high;
      continue;
    }

    if (bar.low < peakHigh) {
      hasPullback = true;
      break;
    }
  }
  const heldBreakout = confirmationBars.every((bar) => bar.low >= breakoutLevel);
  const lastClose = confirmationBars.at(-1)?.close ?? breakoutBar.close;
  const closedAboveBreakout = lastClose >= breakoutLevel;

  if (!hasPullback) {
    return {
      passed: false,
      breakoutTime: breakoutBar.time,
      breakoutLevel,
      sessionHigh,
      pullbackLow,
      reason: '已创新高，但尚未形成可验证的回踩',
    };
  }

  if (!heldBreakout || !closedAboveBreakout) {
    return {
      passed: false,
      breakoutTime: breakoutBar.time,
      breakoutLevel,
      sessionHigh,
      pullbackLow,
      reason: '创新高后回踩跌破突破位',
    };
  }

  return {
    passed: true,
    breakoutTime: breakoutBar.time,
    breakoutLevel,
    sessionHigh,
    pullbackLow,
    reason: '14:30 后创新高，回踩未破突破位且尾盘仍站稳',
  };
}

export function evaluateYangYongxing(
  candidate: YangYongxingCandidate,
): YangYongxingResult {
  const historicalBars = candidate.recentDailyBars.slice(
    -YANG_YONGXING_RULES.lookbackTradingDays,
  );
  const limitUpCount = historicalBars.filter(isLimitUp).length;
  const intraday = evaluateTailPattern(candidate.minuteBars);

  const checks: RuleCheck[] = [
    {
      key: 'change_pct',
      label: '当日涨幅',
      passed:
        candidate.changePct >= YANG_YONGXING_RULES.minChangePct &&
        candidate.changePct <= YANG_YONGXING_RULES.maxChangePct,
      actual: `${round(candidate.changePct)}%`,
      expected: '3.00%—5.00%（含边界）',
    },
    {
      key: 'recent_limit_up',
      label: '近 30 个交易日涨停',
      passed: limitUpCount >= 1,
      actual: `${limitUpCount} 次`,
      expected: '至少 1 次',
    },
    {
      key: 'market_cap',
      label: '总市值',
      passed:
        candidate.totalMarketCapYuan <
        YANG_YONGXING_RULES.maxMarketCapYuan,
      actual: `${round(candidate.totalMarketCapYuan / 100_000_000, 1)} 亿元`,
      expected: '严格小于 200 亿元',
    },
    {
      key: 'volume_ratio',
      label: '当日量比',
      passed:
        candidate.volumeRatio >
        YANG_YONGXING_RULES.minVolumeRatioExclusive,
      actual: round(candidate.volumeRatio),
      expected: '严格大于 1.00',
    },
    {
      key: 'turnover_rate',
      label: '换手率',
      passed:
        candidate.turnoverRatePct >=
          YANG_YONGXING_RULES.minTurnoverRatePct &&
        candidate.turnoverRatePct <=
          YANG_YONGXING_RULES.maxTurnoverRatePct,
      actual: `${round(candidate.turnoverRatePct)}%`,
      expected: '5.00%—10.00%（含边界）',
    },
    {
      key: 'tail_pattern',
      label: '14:30 后分时走势',
      passed: intraday.passed,
      actual: intraday.reason,
      expected: '创新高后回踩不破突破位',
    },
  ];

  const passedCount = checks.filter((check) => check.passed).length;

  return {
    code: candidate.code,
    name: candidate.name,
    passed: passedCount === checks.length,
    score: Math.round((passedCount / checks.length) * 100),
    checks,
    intraday,
  };
}

export function screenYangYongxing(
  candidates: YangYongxingCandidate[],
): YangYongxingResult[] {
  return candidates
    .map(evaluateYangYongxing)
    .filter((result) => result.passed)
    .sort((a, b) => b.score - a.score || a.code.localeCompare(b.code));
}
