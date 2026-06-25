import React from 'react';
import { ConfigData } from '../../types';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function WiFiPage({ config, setConfig, t, onNext, onPrev }: PageProps) {
  const canProceed = config.wifi_ssid.length > 0 && config.wifi_password.length >= 8;

  return (
    <div className="page active">
      <div className="form-group">
        <label>{t.ssid}</label>
        <input
          type="text"
          placeholder="Network name..."
          value={config.wifi_ssid}
          onChange={(e) => setConfig({ ...config, wifi_ssid: e.target.value })}
        />
      </div>

      <div className="form-group">
        <label>{t.password}</label>
        <input
          type="password"
          placeholder="At least 8 characters..."
          value={config.wifi_password}
          onChange={(e) => setConfig({ ...config, wifi_password: e.target.value })}
        />
        {config.wifi_password && config.wifi_password.length < 8 && (
          <small style={{ color: '#f44336', display: 'block', marginTop: '4px' }}>
            Password must be at least 8 characters
          </small>
        )}
      </div>

      <div style={{ flex: 1 }} />

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev}>← {t.back}</button>
        <button className="btn-primary" onClick={onNext} disabled={!canProceed}>
          {t.next} →
        </button>
      </div>
    </div>
  );
}
