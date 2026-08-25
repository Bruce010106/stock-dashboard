import type { Metadata } from 'next';
import PortfolioDashboard from '../../components/portfolio/PortfolioDashboard';

export const metadata: Metadata = {
  title: '自选股与持仓｜知衡 Quant',
  description: '在浏览器本地管理自选股和持仓，并读取真实 A 股最新报价。',
};

export default function PortfolioPage() {
  return <PortfolioDashboard />;
}
