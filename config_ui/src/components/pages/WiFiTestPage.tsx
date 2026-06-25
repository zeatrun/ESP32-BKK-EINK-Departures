import React, { useEffect, useState } from 'react';
import { ConfigData } from '../../types';
import { testWiFi } from '../../api/espApi';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function WiFiTestPage({ config, t, onNext, onPrev }: PageProps) {
  const [status, setStatus] = useState<'testing' | 'success' | 'failed'>('testing');
  const [message, setMessage] = useState('');

  useEffect(() => {
    const runTest = async () => {
      try {
        const result = await testWiFi(config.wifi_ssid, config.wifi_password);
        if (result.success) {
          setStatus('success');
          setMessage(t.testSuccess);
        } else {
          setStatus('failed');
          setMessage(result.message || t.testFailed);
        }
      } catch (error) {
        setStatus('failed');
        setMessage(t.testFailed);
      }
    };

    runTest();
  }, [config.wifi_ssid, config.wifi_password, t]);

  return (
    <div className="page active">
      <div className="wifi-test-container">
        {status === 'testing' && (
          <>
            <div className="spinner" />
            <div className="status-text">{t.testing}</div>
            <div className="status-subtext">{config.wifi_ssid}</div>
          </>
        )}

        {status === 'success' && (
          <>
            <div className="status-icon success">✓</div>
            <div className="status-text">{t.testSuccess}</div>
            <div className="status-subtext">{config.wifi_ssid}</div>
          </>
        )}

        {status === 'failed' && (
          <>
            <div className="status-icon error">✕</div>
            <div className="status-text">{t.testFailed}</div>
            <div className="status-subtext">{message}</div>
          </>
        )}
      </div>

      <div className="button-group" style={{ marginTop: '40px' }}>
        <button className="btn-secondary" onClick={onPrev}>
          ← {t.back}
        </button>

        {status === 'success' && (
          <button className="btn-primary" onClick={onNext}>
            {t.next} →
          </button>
        )}

        {status === 'failed' && (
          <button className="btn-secondary" onClick={onNext}>
            {t.skip} →
          </button>
        )}
      </div>
    </div>
  );
}
