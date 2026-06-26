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
          <option value="Etc/GMT+12">UTC-12:00 (Baker Island, Howland Island)</option>
          <option value="Pacific/Pago_Pago">UTC-11:00 (Pago Pago, Midway)</option>
          <option value="Pacific/Honolulu">UTC-10:00 (Honolulu, Papeete)</option>
          <option value="America/Anchorage">UTC-09:00 (Anchorage, Juneau)</option>
          <option value="America/Los_Angeles">UTC-08:00 (Los Angeles, Vancouver)</option>
          <option value="America/Denver">UTC-07:00 (Denver, Phoenix)</option>
          <option value="America/Chicago">UTC-06:00 (Chicago, Mexico City)</option>
          <option value="America/New_York">UTC-05:00 (New York, Toronto)</option>
          <option value="America/Halifax">UTC-04:00 (Halifax, Santiago)</option>
          <option value="America/Argentina/Buenos_Aires">UTC-03:00 (Buenos Aires, Montevideo)</option>
          <option value="Atlantic/South_Georgia">UTC-02:00 (South Georgia, Mid-Atlantic)</option>
          <option value="Atlantic/Azores">UTC-01:00 (Azores, Cape Verde)</option>
          <option value="UTC">UTC+00:00 (London, Reykjavik)</option>
          <option value="Europe/Budapest">UTC+01:00 (Budapest, Berlin)</option>
          <option value="Europe/Athens">UTC+02:00 (Athens, Cairo)</option>
          <option value="Europe/Moscow">UTC+03:00 (Moscow, Istanbul)</option>
          <option value="Asia/Dubai">UTC+04:00 (Dubai, Baku)</option>
          <option value="Asia/Karachi">UTC+05:00 (Karachi, Tashkent)</option>
          <option value="Asia/Dhaka">UTC+06:00 (Dhaka, Almaty)</option>
          <option value="Asia/Bangkok">UTC+07:00 (Bangkok, Jakarta)</option>
          <option value="Asia/Singapore">UTC+08:00 (Singapore, Beijing)</option>
          <option value="Asia/Tokyo">UTC+09:00 (Tokyo, Seoul)</option>
          <option value="Australia/Sydney">UTC+10:00 (Sydney, Port Moresby)</option>
          <option value="Pacific/Noumea">UTC+11:00 (Noumea, Honiara)</option>
          <option value="Pacific/Auckland">UTC+12:00 (Auckland, Suva)</option>
          <option value="Pacific/Apia">UTC+13:00 (Apia, Nuku'alofa)</option>
          <option value="Pacific/Kiritimati">UTC+14:00 (Kiritimati, Line Islands)</option>
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
