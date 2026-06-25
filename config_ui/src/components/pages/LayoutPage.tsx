import React from 'react';
import { ConfigData } from '../../types';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function LayoutPage({ t, onNext, onPrev }: PageProps) {
  return (
    <div className="page active">
      <div style={{
        padding: '40px 20px',
        textAlign: 'center',
        color: '#999',
        flex: 1,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center'
      }}>
        Layout configuration coming soon...
      </div>

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev}>← {t.back}</button>
        <button className="btn-primary" onClick={onNext}>{t.next} →</button>
      </div>
    </div>
  );
}
