import React, { useState } from 'react';
import { ConfigData } from '../../types';
import { saveConfiguration, factoryReset } from '../../api/espApi';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onPrev: () => void;
}

export default function SummaryPage({ config, t, onPrev }: PageProps) {
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const [success, setSuccess] = useState(false);

  const handleSave = async () => {
    setSaving(true);
    setError('');
    try {
      const result = await saveConfiguration(config);
      if (result.success) {
        setSuccess(true);
      } else {
        setError(result.message || 'Failed to save');
      }
    } catch (err) {
      setError('Connection error');
    } finally {
      setSaving(false);
    }
  };

  const handleReset = async () => {
    if (confirm('Are you sure? This will erase all configuration.')) {
      setSaving(true);
      try {
        await factoryReset();
        window.location.reload();
      } catch (err) {
        setError('Failed to reset');
      } finally {
        setSaving(false);
      }
    }
  };

  if (success) {
    return (
      <div className="page active">
        <div style={{
          flex: 1,
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          gap: '24px'
        }}>
          <div style={{
            width: '80px',
            height: '80px',
            background: '#e8f5e9',
            borderRadius: '50%',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            fontSize: '48px'
          }}>✓</div>
          <h2 style={{ color: '#4caf50', textAlign: 'center' }}>
            {t.confirm}!
          </h2>
          <p style={{ color: '#999', textAlign: 'center' }}>
            Configuration saved. Device will restart...
          </p>
        </div>
      </div>
    );
  }

  return (
    <div className="page active">
      <h2 style={{ marginBottom: '24px', color: '#333' }}>{t.summary}</h2>

      <div className="summary-list">
        <div className="summary-item">
          <span className="summary-item-label">Language:</span>
          <span className="summary-item-value">{config.language === 'hu' ? 'Magyar' : 'English'}</span>
        </div>
        <div className="summary-item">
          <span className="summary-item-label">SSID:</span>
          <span className="summary-item-value">{config.wifi_ssid}</span>
        </div>
        <div className="summary-item">
          <span className="summary-item-label">Timezone:</span>
          <span className="summary-item-value">{config.timezone}</span>
        </div>
        {config.location_name && (
          <div className="summary-item">
            <span className="summary-item-label">Location:</span>
            <span className="summary-item-value">{config.location_name}</span>
          </div>
        )}
      </div>

      {error && (
        <div style={{
          background: '#ffebee',
          color: '#f44336',
          padding: '12px',
          borderRadius: '8px',
          marginBottom: '16px',
          fontSize: '14px'
        }}>
          {error}
        </div>
      )}

      <div style={{ flex: 1 }} />

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev} disabled={saving}>
          ← {t.layout}
        </button>
        <button
          className="btn-primary"
          onClick={handleSave}
          disabled={saving}
          style={{ flex: 2 }}
        >
          {saving ? 'Saving...' : t.save}
        </button>
        <button
          className="btn-danger"
          onClick={handleReset}
          disabled={saving}
        >
          {t.reset}
        </button>
      </div>
    </div>
  );
}
