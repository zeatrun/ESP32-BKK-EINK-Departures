import React from 'react';
import { ConfigData } from '../../types';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function GeneralPage({ config, setConfig, t, onNext, onPrev }: PageProps) {
  return (
    <div className="page active">
      <div className="form-group">
        <label>{t.timezone}</label>
        <select
          value={config.timezone}
          onChange={(e) => setConfig({ ...config, timezone: e.target.value })}
        >
          <option value="Europe/Budapest">Europe/Budapest (UTC+1/+2)</option>
          <option value="Europe/London">Europe/London (UTC+0/+1)</option>
          <option value="Europe/Paris">Europe/Paris (UTC+1/+2)</option>
          <option value="Europe/Berlin">Europe/Berlin (UTC+1/+2)</option>
          <option value="UTC">UTC</option>
        </select>
      </div>

      <div style={{ flex: 1 }} />

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev}>← {t.back}</button>
        <button className="btn-primary" onClick={onNext}>{t.next} →</button>
      </div>
    </div>
  );
}
