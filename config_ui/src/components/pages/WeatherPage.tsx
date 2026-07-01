import React, { useState } from 'react';
import { ConfigData, GeoLocation } from '../../types';
import { searchLocations, testWeather } from '../../api/espApi';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function WeatherPage({ config, setConfig, t, onNext, onPrev }: PageProps) {
  const weatherSourceIsMqtt = config.weather_data_source === '1';
  const [searchQuery, setSearchQuery] = useState(config.location_name || '');
  const [searchResults, setSearchResults] = useState<GeoLocation[]>([]);
  const [searching, setSearching] = useState(false);
  const [showManualCoords, setShowManualCoords] = useState(false);
  const [latError, setLatError] = useState('');
  const [lonError, setLonError] = useState('');
  const [testingWeather, setTestingWeather] = useState(false);
  const [weatherTestStatus, setWeatherTestStatus] = useState<'success' | 'failed' | null>(null);
  const [weatherTestMessage, setWeatherTestMessage] = useState('');
  const [testedSuccessfully, setTestedSuccessfully] = useState(false);

  const validateCoordinate = (lat: string, lon: string) => {
    let latErr = '';
    let lonErr = '';

    if (lat) {
      const latNum = parseFloat(lat);
      if (isNaN(latNum)) {
        latErr = t.invalidCoordinates;
      } else if (latNum < -90 || latNum > 90) {
        latErr = t.latitudeMustBeBetween;
      }
    }

    if (lon) {
      const lonNum = parseFloat(lon);
      if (isNaN(lonNum)) {
        lonErr = t.invalidCoordinates;
      } else if (lonNum < -180 || lonNum > 180) {
        lonErr = t.longitudeMustBeBetween;
      }
    }

    setLatError(latErr);
    setLonError(lonErr);
    return !latErr && !lonErr;
  };

  const handleLocationSearch = async (query: string) => {
    setSearchQuery(query);
    
    if (query.length < 2) {
      setSearchResults([]);
      return;
    }

    setSearching(true);
    const results = await searchLocations(query);
    setSearchResults(results);
    setSearching(false);
  };

  const handleLocationSelect = (location: GeoLocation) => {
    setConfig({
      ...config,
      location_name: location.name,
      location_lat: location.latitude.toString(),
      location_lon: location.longitude.toString()
    });
    setSearchQuery(location.name);
    setSearchResults([]);
    setShowManualCoords(false);
    setLatError('');
    setLonError('');
    setTestedSuccessfully(false);
    setWeatherTestStatus(null);
    setWeatherTestMessage('');
  };

  const handleLatitudeChange = (lat: string) => {
    setConfig({ ...config, location_lat: lat });
    validateCoordinate(lat, config.location_lon);
    setTestedSuccessfully(false);
    setWeatherTestStatus(null);
    setWeatherTestMessage('');
  };

  const handleLongitudeChange = (lon: string) => {
    setConfig({ ...config, location_lon: lon });
    validateCoordinate(config.location_lat, lon);
    setTestedSuccessfully(false);
    setWeatherTestStatus(null);
    setWeatherTestMessage('');
  };

  const handleConfirmCoordinates = () => {
    if (validateCoordinate(config.location_lat, config.location_lon)) {
      setShowManualCoords(false);
    }
  };

  const handleTestWeather = async () => {
    setTestingWeather(true);
    setWeatherTestStatus(null);
    const result = await testWeather(config.location_lat, config.location_lon);
    setTestingWeather(false);
    setWeatherTestStatus(result.success ? 'success' : 'failed');
    setWeatherTestMessage(result.message);
    if (result.success) {
      setTestedSuccessfully(true);
    }
  };

  const weatherInputsComplete = weatherSourceIsMqtt
    ? config.mqtt_weather_topic.trim().length > 0
    : !!config.location_lat && !!config.location_lon && !latError && !lonError && !showManualCoords;

  const weatherNeedsTest = !weatherSourceIsMqtt;
  const weatherPrimaryDisabled = testingWeather || !weatherInputsComplete;
  const weatherPrimaryIsNext = !weatherNeedsTest || testedSuccessfully;

  const handleWeatherPrimaryAction = () => {
    if (weatherPrimaryDisabled) {
      return;
    }

    if (weatherPrimaryIsNext) {
      onNext();
      return;
    }

    handleTestWeather();
  };

  return (
    <div className="page active">
      <div className="form-group">
        <label>{t.weatherSource}</label>
        <select
          value={config.weather_data_source}
          onChange={(e) => {
            setConfig({ ...config, weather_data_source: e.target.value });
            setTestedSuccessfully(false);
            setWeatherTestStatus(null);
            setWeatherTestMessage('');
          }}
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

          {!showManualCoords ? (
            <div className="form-group">
              <label>{t.location}</label>
              <input
                type="text"
                placeholder="e.g., Budapest"
                value={searchQuery}
                onChange={(e) => handleLocationSearch(e.target.value)}
              />
              {searching && <small style={{ color: '#666' }}>{t.searchingLocations}</small>}
              {searchResults.length > 0 && (
                <div style={{
                  border: '1px solid #ddd',
                  borderRadius: '4px',
                  marginTop: '8px',
                  maxHeight: '200px',
                  overflowY: 'auto',
                  backgroundColor: '#fff'
                }}>
                  {searchResults.map((result, idx) => (
                    <div
                      key={idx}
                      onClick={() => handleLocationSelect(result)}
                      style={{
                        padding: '10px',
                        borderBottom: idx < searchResults.length - 1 ? '1px solid #eee' : 'none',
                        cursor: 'pointer',
                        hoverBackgroundColor: '#f5f5f5'
                      }}
                    >
                      <strong>{result.name}</strong>
                      {result.admin1 && <span> ({result.admin1})</span>}
                      {result.country && <span> - {result.country}</span>}
                      <div style={{ fontSize: '0.85em', color: '#999' }}>
                        {result.latitude.toFixed(4)}, {result.longitude.toFixed(4)}
                      </div>
                    </div>
                  ))}
                </div>
              )}
              {searchQuery.length >= 2 && searchResults.length === 0 && !searching && (
                <div>
                  <small style={{ color: '#f44' }}>{t.noLocationsFound}</small>
                  <div style={{ textAlign: 'center', marginTop: '10px' }}>
                    <button
                      type="button"
                      onClick={() => setShowManualCoords(true)}
                      className="btn-primary"
                      style={{ display: 'inline-block' }}
                    >
                      {t.manualCoordinates}
                    </button>
                  </div>
                </div>
              )}
              {config.location_name && (
                <small style={{ color: '#666', display: 'block', marginTop: '8px' }}>
                  ✓ {config.location_name} ({config.location_lat}, {config.location_lon})
                </small>
              )}
            </div>
          ) : (
            <div className="form-group">
              <label>{t.manualCoordinates}</label>
              <div style={{ display: 'flex', gap: '8px', marginBottom: '8px' }}>
                <input
                  type="number"
                  placeholder={t.latitude}
                  step="0.0001"
                  min="-90"
                  max="90"
                  value={config.location_lat}
                  onChange={(e) => handleLatitudeChange(e.target.value)}
                  style={{
                    flex: 1,
                    borderColor: latError ? '#f44' : '#ccc',
                    backgroundColor: latError ? '#ffe6e6' : '#fff'
                  }}
                />
                <input
                  type="number"
                  placeholder={t.longitude}
                  step="0.0001"
                  min="-180"
                  max="180"
                  value={config.location_lon}
                  onChange={(e) => handleLongitudeChange(e.target.value)}
                  style={{
                    flex: 1,
                    borderColor: lonError ? '#f44' : '#ccc',
                    backgroundColor: lonError ? '#ffe6e6' : '#fff'
                  }}
                />
              </div>
              {latError && <small style={{ color: '#f44', display: 'block', marginBottom: '4px' }}>{latError}</small>}
              {lonError && <small style={{ color: '#f44', display: 'block', marginBottom: '4px' }}>{lonError}</small>}
              <div style={{ display: 'flex', gap: '8px', marginTop: '8px' }}>
                <button
                  type="button"
                  onClick={handleConfirmCoordinates}
                  className="btn-primary"
                  style={{ flex: 1, padding: '8px', opacity: latError || lonError ? 0.5 : 1, cursor: latError || lonError ? 'not-allowed' : 'pointer' }}
                  disabled={!!latError || !!lonError}
                >
                  {t.confirm}
                </button>
                <button
                  type="button"
                  onClick={() => setShowManualCoords(false)}
                  className="btn-secondary"
                  style={{ flex: 1, padding: '8px' }}
                >
                  {t.back}
                </button>
              </div>
            </div>
          )}

          {!showManualCoords && weatherTestStatus === 'failed' && (
            <div style={{ marginTop: '12px' }}>
              <small style={{ color: '#f44', display: 'block' }}>✕ {weatherTestMessage || t.testFailed}</small>
            </div>
          )}

          {!showManualCoords && testedSuccessfully && (
            <div style={{ marginTop: '12px', textAlign: 'center' }}>
              <div style={{ color: '#0a0', fontSize: '1.2em', marginBottom: '8px' }}>✓ {t.testSuccess}</div>
            </div>
          )}
        </>
      )}

      {weatherSourceIsMqtt && (
        <div className="form-group">
          <label>{t.mqttWeatherTopic}</label>
          <input
            type="text"
            placeholder="e.g., weather/data"
            value={config.mqtt_weather_topic}
            onChange={(e) => {
              setConfig({ ...config, mqtt_weather_topic: e.target.value });
              setTestedSuccessfully(false);
              setWeatherTestStatus(null);
              setWeatherTestMessage('');
            }}
          />
        </div>
      )}

      <div style={{ flex: 1 }} />

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev}>← {t.back}</button>
        <button
          className="btn-primary"
          onClick={handleWeatherPrimaryAction}
          disabled={weatherPrimaryDisabled}
          style={{
            backgroundColor: weatherPrimaryDisabled ? '#9e9e9e' : weatherPrimaryIsNext ? '#5cb85c' : '#ff9800',
            opacity: weatherPrimaryDisabled ? 0.7 : 1,
            cursor: weatherPrimaryDisabled ? 'not-allowed' : 'pointer'
          }}
        >
          {testingWeather ? t.testing : weatherPrimaryIsNext ? `${t.next} →` : t.testButton}
        </button>
      </div>
    </div>
  );
}
