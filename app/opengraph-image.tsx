import { ImageResponse } from 'next/og';

export const alt = 'Zhiheng Quant — A-share strategy screening and backtesting';
export const size = { width: 1200, height: 630 };
export const contentType = 'image/png';

const rules = ['3–5% CHANGE', '30D LIMIT-UP', '< 20B CAP', '> 1 VOL RATIO', '5–10% TURN', '14:30 HIGH'];

export default function OpenGraphImage() {
  return new ImageResponse(
    <div
      style={{
        width: '100%',
        height: '100%',
        display: 'flex',
        flexDirection: 'column',
        justifyContent: 'space-between',
        padding: '64px 72px',
        color: '#f5f0df',
        background: 'linear-gradient(135deg, #092f28 0%, #0b4a3a 58%, #082821 100%)',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 20 }}>
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              width: 62,
              height: 62,
              borderRadius: 18,
              color: '#092f28',
              background: '#d7b76f',
              fontSize: 28,
              fontWeight: 800,
            }}
          >
            ZQ
          </div>
          <div style={{ display: 'flex', fontSize: 30, fontWeight: 700, letterSpacing: 2 }}>
            ZHIHENG QUANT
          </div>
        </div>
        <div style={{ display: 'flex', color: '#d7b76f', fontSize: 20, letterSpacing: 2 }}>
          A-SHARE SIGNAL LAB
        </div>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 22 }}>
        <div style={{ display: 'flex', color: '#d7b76f', fontSize: 23, letterSpacing: 3 }}>
          YANG YONGXING · TAIL 14:30
        </div>
        <div style={{ display: 'flex', maxWidth: 880, fontSize: 58, fontWeight: 800, lineHeight: 1.08 }}>
          SCREEN THE CLOSE.
          <br />
          TEST THE SIGNAL.
        </div>
        <div style={{ display: 'flex', color: '#b9cec7', fontSize: 25 }}>
          Strategy screening, rule audits and forward-return backtesting.
        </div>
      </div>

      <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap' }}>
        {rules.map((rule) => (
          <div
            key={rule}
            style={{
              display: 'flex',
              padding: '12px 17px',
              border: '1px solid rgba(215,183,111,.55)',
              borderRadius: 999,
              color: '#e7d6a9',
              background: 'rgba(4,28,23,.38)',
              fontSize: 17,
              letterSpacing: 1,
            }}
          >
            {rule}
          </div>
        ))}
      </div>
    </div>,
    size,
  );
}
