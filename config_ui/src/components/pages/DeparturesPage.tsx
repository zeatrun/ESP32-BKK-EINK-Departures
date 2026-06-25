import React from 'react';
import { ConfigData } from '../../types';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function DeparturesPage({ config, setConfig, t, onNext, onPrev }: PageProps) {
  const departuresSourceIsMqtt = config.departures_data_source === '1';

  return (
    <div className="page active">
      <div className="form-group">
        <label>{t.departuresSource}</label>
        <select
          value={config.departures_data_source}
          onChange={(e) => setConfig({ ...config, departures_data_source: e.target.value })}
        >
          <option value="0">{t.directApi}</option>
          <option value="1">{t.mqtt}</option>
        </select>
      </div>

      {!departuresSourceIsMqtt && (
        <>
          <div className="form-group">
            <label>{t.departuresProvider}</label>
            <select
              value={config.departures_api_provider}
              onChange={(e) => setConfig({ ...config, departures_api_provider: e.target.value })}
            >
              <option value="0">BKK (Budapest)</option>
              <option value="1">Other</option>
            </select>
          </div>

          <div className="form-group">
            <label>{t.bkkApiKey}</label>
            <input
              type="text"
              placeholder="API key..."
              value={config.bkk_api_key}
              onChange={(e) => setConfig({ ...config, bkk_api_key: e.target.value })}
            />
          </div>

          <div className="form-group">
            <label>{t.busStopId}</label>
            <input
              type="text"
              placeholder="Stop ID..."
              value={config.bus_stop_id}
              onChange={(e) => setConfig({ ...config, bus_stop_id: e.target.value })}
            />
          </div>

          <div className="form-group">
            <label>{t.trainStopId}</label>
            <input
              type="text"
              placeholder="Stop ID..."
              value={config.train_stop_id}
              onChange={(e) => setConfig({ ...config, train_stop_id: e.target.value })}
            />
          </div>
        </>
      )}

      {departuresSourceIsMqtt && (
        <div className="form-group">
          <label>{t.mqttDepTopic}</label>
          <input
            type="text"
            placeholder="e.g., departures/data"
            value={config.mqtt_departures_topic}
            onChange={(e) => setConfig({ ...config, mqtt_departures_topic: e.target.value })}
          />
        </div>
      )}

      <div style={{ flex: 1 }} />

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev}>← {t.back}</button>
        <button className="btn-primary" onClick={onNext}>{t.next} →</button>
      </div>
    </div>
  );
}
