import type { Metadata } from 'next';
import PortfolioDashboard from '../../components/portfolio/PortfolioDashboard';

export const metadata: Metadata = {
  title: '自选股与持仓｜知衡 Quant',
  description: '管理自选股和持仓，登录后可跨设备同步，并读取真实 A 股最新报价。',
};

export default function PortfolioPage() {
  return <PortfolioDashboard />;
}
