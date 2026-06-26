import React, { useState } from 'react';
import { ConfigData } from '../../types';
import { testDepartures } from '../../api/espApi';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function DeparturesPage({ config, setConfig, t, onNext, onPrev }: PageProps) {
  const departuresSourceIsMqtt = config.departures_data_source === '1';
  const [testingDepartures, setTestingDepartures] = useState(false);
  const [departuresTestStatus, setDeparturesTestStatus] = useState<'success' | 'failed' | null>(null);
  const [departuresTestMessage, setDeparturesTestMessage] = useState('');
  const [testedSuccessfully, setTestedSuccessfully] = useState(false);

  const handleTestDepartures = async () => {
    setTestingDepartures(true);
    setDeparturesTestStatus(null);
    const result = await testDepartures(config.bkk_api_key, config.bus_stop_id, config.train_stop_id);
    setTestingDepartures(false);
    setDeparturesTestStatus(result.success ? 'success' : 'failed');
    setDeparturesTestMessage(result.message);
    if (result.success) {
      setTestedSuccessfully(true);
    }
  };

  const departuresInputsComplete = departuresSourceIsMqtt
    ? config.mqtt_departures_topic.trim().length > 0
    : !!config.bkk_api_key && !!config.bus_stop_id && !!config.train_stop_id;

  const departuresNeedsTest = !departuresSourceIsMqtt;
  const departuresPrimaryDisabled = testingDepartures || !departuresInputsComplete;
  const departuresPrimaryIsNext = !departuresNeedsTest || testedSuccessfully;

  const handleDeparturesPrimaryAction = () => {
    if (departuresPrimaryDisabled) {
      return;
    }

    if (departuresPrimaryIsNext) {
      onNext();
      return;
    }

    handleTestDepartures();
  };

  return (
    <div className="page active">
      <div className="form-group">
        <label>{t.departuresSource}</label>
        <select
          value={config.departures_data_source}
          onChange={(e) => {
            setConfig({ ...config, departures_data_source: e.target.value });
            setTestedSuccessfully(false);
            setDeparturesTestStatus(null);
            setDeparturesTestMessage('');
          }}
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
              onChange={(e) => {
                setConfig({ ...config, bkk_api_key: e.target.value });
                setTestedSuccessfully(false);
                setDeparturesTestStatus(null);
                setDeparturesTestMessage('');
              }}
            />
          </div>

          <div className="form-group">
            <label>{t.busStopId}</label>
            <input
              type="text"
              placeholder="Stop ID..."
              value={config.bus_stop_id}
              onChange={(e) => {
                setConfig({ ...config, bus_stop_id: e.target.value });
                setTestedSuccessfully(false);
                setDeparturesTestStatus(null);
                setDeparturesTestMessage('');
              }}
            />
          </div>

          <div className="form-group">
            <label>{t.trainStopId}</label>
            <input
              type="text"
              placeholder="Stop ID..."
              value={config.train_stop_id}
              onChange={(e) => {
                setConfig({ ...config, train_stop_id: e.target.value });
                setTestedSuccessfully(false);
                setDeparturesTestStatus(null);
                setDeparturesTestMessage('');
              }}
            />
          </div>

          {departuresTestStatus === 'failed' && (
            <div style={{ marginTop: '12px' }}>
              <small style={{ color: '#f44', display: 'block' }}>✕ {departuresTestMessage || t.testFailed}</small>
            </div>
          )}

          {config.bkk_api_key && config.bus_stop_id && config.train_stop_id && testedSuccessfully && (
            <div style={{ marginTop: '12px', textAlign: 'center' }}>
              <div style={{ color: '#0a0', fontSize: '1.2em', marginBottom: '8px' }}>✓ {t.testSuccess}</div>
            </div>
          )}
        </>
      )}

      {departuresSourceIsMqtt && (
        <div className="form-group">
          <label>{t.mqttDepTopic}</label>
          <input
            type="text"
            placeholder="e.g., departures/data"
            value={config.mqtt_departures_topic}
            onChange={(e) => {
              setConfig({ ...config, mqtt_departures_topic: e.target.value });
              setTestedSuccessfully(false);
              setDeparturesTestStatus(null);
              setDeparturesTestMessage('');
            }}
          />
        </div>
      )}

      <div style={{ flex: 1 }} />

      <div className="button-group">
        <button className="btn-secondary" onClick={onPrev}>← {t.back}</button>
        <button
          className="btn-primary"
          onClick={handleDeparturesPrimaryAction}
          disabled={departuresPrimaryDisabled}
          style={{
            backgroundColor: departuresPrimaryDisabled ? '#9e9e9e' : departuresPrimaryIsNext ? '#5cb85c' : '#ff9800',
            opacity: departuresPrimaryDisabled ? 0.7 : 1,
            cursor: departuresPrimaryDisabled ? 'not-allowed' : 'pointer'
          }}
        >
          {testingDepartures ? t.testing : departuresPrimaryIsNext ? `${t.next} →` : t.testButton}
        </button>
      </div>
    </div>
  );
}
