import React, { useEffect, useRef, useState } from 'react';
import { ConfigData } from '../../types';
import { scanWiFiNetworks, WiFiNetwork } from '../../api/espApi';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

function rssiToStrength(rssi: number): string {
  if (rssi >= -50) return '▂▄▆█';
  if (rssi >= -65) return '▂▄▆░';
  if (rssi >= -75) return '▂▄░░';
  return '▂░░░';
}

export default function WiFiPage({ config, setConfig, t, onNext, onPrev }: PageProps) {
  const [networks, setNetworks] = useState<WiFiNetwork[]>([]);
  const [scanning, setScanning] = useState(true);
  const [showManual, setShowManual] = useState(false);
  const [showPassword, setShowPassword] = useState(false);
  const ssidInputRef = useRef<HTMLInputElement | null>(null);
  const passwordInputRef = useRef<HTMLInputElement | null>(null);
  const scanRequestIdRef = useRef(0);

  const canProceed = config.wifi_ssid.length > 0 && config.wifi_password.length >= 8;

  const handleEnterProceed = (event: React.KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'Enter' && canProceed) {
      event.preventDefault();
      onNext();
    }
  };

  const runScan = async (forceRefresh: boolean) => {
    const requestId = ++scanRequestIdRef.current;
    setScanning(true);

    const maxPolls = 40;
    for (let poll = 0; poll < maxPolls; poll += 1) {
      const scanResponse = await scanWiFiNetworks(forceRefresh && poll === 0);

      if (requestId !== scanRequestIdRef.current) {
        return;
      }

      const found = scanResponse.networks ?? [];
      setNetworks(found);

      if (scanResponse.status === 'done') {
        setScanning(false);
        if (found.length === 0) setShowManual(true);
        return;
      }

      if (scanResponse.status === 'failed') {
        setScanning(false);
        if (found.length === 0) setShowManual(true);
        return;
      }

      await new Promise((resolve) => setTimeout(resolve, 350));
    }

    if (requestId === scanRequestIdRef.current) {
      setScanning(false);
      if (networks.length === 0) setShowManual(true);
    }
  };

  useEffect(() => {
    runScan(true);
    return () => {
      scanRequestIdRef.current += 1;
    };
  }, []);

  useEffect(() => {
    if (showManual) {
      ssidInputRef.current?.focus();
      return;
    }
    if (config.wifi_ssid.length > 0) {
      passwordInputRef.current?.focus();
    }
  }, [showManual, config.wifi_ssid]);

  const doRescan = async () => {
    await runScan(true);
  };

  return (
    <div className="page active">
      {!showManual && (
        <div className="form-group">
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
            <label style={{ margin: 0 }}>{t.ssid}</label>
            <button
              onClick={doRescan}
              disabled={scanning}
              style={{ background: 'none', border: 'none', cursor: 'pointer', color: '#667eea', fontSize: '13px', padding: 0 }}
            >
              {scanning ? '⏳' : '↻'} {t.scan ?? 'Scan'}
            </button>
          </div>

          {scanning ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: '10px', padding: '16px', color: '#999' }}>
              <div className="spinner" style={{ width: '20px', height: '20px', borderWidth: '3px' }} />
              {t.scanning ?? 'Scanning...'}
            </div>
          ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', maxHeight: '220px', overflowY: 'auto' }}>
              {networks.map((net) => (
                <div
                  key={net.ssid}
                  onClick={() => setConfig({ ...config, wifi_ssid: net.ssid })}
                  style={{
                    display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                    padding: '10px 12px',
                    border: `2px solid ${config.wifi_ssid === net.ssid ? '#667eea' : '#e0e0e0'}`,
                    borderRadius: '8px',
                    cursor: 'pointer',
                    background: config.wifi_ssid === net.ssid ? '#f0f0ff' : 'white',
                    transition: 'all 0.2s',
                  }}
                >
                  <span style={{ fontWeight: config.wifi_ssid === net.ssid ? 600 : 400 }}>
                    {net.open ? '🔓' : '🔒'} {net.ssid}
                  </span>
                  <span style={{ fontSize: '12px', color: '#999', fontFamily: 'monospace' }}>
                    {rssiToStrength(net.rssi)}
                  </span>
                </div>
              ))}
              {networks.length === 0 && (
                <div style={{ color: '#999', padding: '12px', textAlign: 'center' }}>
                  {t.noNetworks ?? 'No networks found'}
                </div>
              )}
            </div>
          )}

          <button
            onClick={() => setShowManual(true)}
            style={{ background: 'none', border: 'none', cursor: 'pointer', color: '#667eea', fontSize: '13px', marginTop: '8px', padding: 0 }}
          >
            + {t.manualEntry ?? 'Enter manually'}
          </button>
        </div>
      )}

      {showManual && (
        <div className="form-group">
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
            <label style={{ margin: 0 }}>{t.ssid}</label>
            {networks.length > 0 && (
              <button
                onClick={() => setShowManual(false)}
                style={{ background: 'none', border: 'none', cursor: 'pointer', color: '#667eea', fontSize: '13px', padding: 0 }}
              >
                ← {t.showList ?? 'Show list'}
              </button>
            )}
          </div>
          <input
            ref={ssidInputRef}
            type="text"
            placeholder="Network name..."
            value={config.wifi_ssid}
            onChange={(e) => setConfig({ ...config, wifi_ssid: e.target.value })}
            onKeyDown={handleEnterProceed}
          />
        </div>
      )}

      {(showManual || config.wifi_ssid.length > 0) && (
        <div className="form-group">
          <label>{t.password}</label>
          <div style={{ position: 'relative' }}>
            <input
              ref={passwordInputRef}
              type={showPassword ? 'text' : 'password'}
              placeholder={t.passwordHint ?? 'At least 8 characters...'}
              value={config.wifi_password}
              onChange={(e) => setConfig({ ...config, wifi_password: e.target.value })}
              onKeyDown={handleEnterProceed}
              style={{ paddingRight: '44px' }}
            />
            <button
              type="button"
              onClick={() => setShowPassword((prev) => !prev)}
              aria-label={showPassword ? 'Hide password' : 'Show password'}
              style={{
                position: 'absolute',
                right: '10px',
                top: '50%',
                transform: 'translateY(-50%)',
                width: '24px',
                height: '24px',
                minWidth: '24px',
                padding: 0,
                margin: 0,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                flex: '0 0 auto',
                border: 'none',
                background: 'transparent',
                cursor: 'pointer',
                fontSize: '16px',
                lineHeight: 1,
                color: '#666'
              }}
            >
              {showPassword ? '🙈' : '👁'}
            </button>
          </div>
          {config.wifi_password && config.wifi_password.length < 8 && (
            <small style={{ color: '#f44336', display: 'block', marginTop: '4px' }}>
              {t.passwordTooShort ?? 'Password must be at least 8 characters'}
            </small>
          )}
        </div>
      )}

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
