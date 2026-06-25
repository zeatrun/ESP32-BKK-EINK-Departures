import React from 'react';
import { ConfigData } from '../../types';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function WeatherPage({ config, setConfig, t, onNext, onPrev }: PageProps) {
  const weatherSourceIsMqtt = config.weather_data_source === '1';

  return (
    <div className="page active">
      <h2 style={{ marginBottom: '24px', color: '#333' }}>{t.weatherSettings}</h2>

      <div className="form-group">
        <label>{t.weatherSource}</label>
        <select
          value={config.weather_data_source}
          onChange={(e) => setConfig({ ...config, weather_data_source: e.target.value })}
        >
          <option value="0">{t.directApi}</option>
          <option value="1">{t.mqtt}</option>
        </select>
      </div>

      {!weatherSourceIsMqtt && (
        <>
          <div className="form-group">
            <label>{t.weatherProvider}</label>
            <select
              value={config.weather_api_provider}
              onChange={(e) => setConfig({ ...config, weather_api_provider: e.target.value })}
            >
              <option value="0">{t.openmeteo}</option>
            </select>
          </div>

          <div className="form-group">
            <label>{t.location}</label>
            <input
              type="text"
              placeholder="e.g., Budapest"
              value={config.location_name}
              onChange={(e) => setConfig({ ...config, location_name: e.target.value })}
            />
            <small style={{ color: '#999', display: 'block', marginTop: '4px' }}>
              Known cities: Budapest, Pilisvorosvar, Esztergom, Gyor, Szeged, Debrecen
            </small>
          </div>
        </>
      )}

      {weatherSourceIsMqtt && (
        <div className="form-group">
          <label>{t.mqttWeatherTopic}</label>
          <input
            type="text"
            placeholder="e.g., weather/data"
            value={config.mqtt_weather_topic}
            onChange={(e) => setConfig({ ...config, mqtt_weather_topic: e.target.value })}
          />
        </div>
      )}

      <div style={{ flex: 1 }} />

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev}>← {t.wifiTest}</button>
        <button className="btn-primary" onClick={onNext}>{t.departures} →</button>
      </div>
    </div>
  );
}
