import type { Metadata } from 'next';
import { normalizeTicker } from '../../../lib/data/provider-utils.ts';
import { StockDetailClient } from '../../../components/stocks/StockDetailClient.tsx';

type Props = { params: Promise<{ code: string }> };

export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const rawCode = (await params).code;
  let code = rawCode;
  try {
    code = normalizeTicker(rawCode);
  } catch {
    // The client page displays the validation error returned by the API.
  }
  return {
    title: `${code} 股票详情｜知衡 Quant`,
    description: `${code} 的真实日K、成交量与杨永兴尾盘策略核验结果。`,
  };
}

export default async function StockDetailPage({ params }: Props) {
  const { code } = await params;
  return <StockDetailClient code={code} />;
}
