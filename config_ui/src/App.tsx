import React, { useState } from 'react';
import './styles/App.css';
import { Language, ConfigData, translations } from './types';

import LanguagePage from './components/pages/LanguagePage';
import GeneralPage from './components/pages/GeneralPage';
import WiFiPage from './components/pages/WiFiPage';
import WiFiTestPage from './components/pages/WiFiTestPage';
import WeatherPage from './components/pages/WeatherPage';
import DeparturesPage from './components/pages/DeparturesPage';
import LayoutPage from './components/pages/LayoutPage';
import SummaryPage from './components/pages/SummaryPage';

type PageComponent = React.FC<{
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  language: Language;
  t: typeof translations.hu;
  onNext: () => void;
  onPrev: () => void;
}>;

const pages: { component: PageComponent; key: string }[] = [
  { component: LanguagePage, key: 'language' },
  { component: GeneralPage, key: 'general' },
  { component: WiFiPage, key: 'wifi' },
  { component: WiFiTestPage, key: 'wifi-test' },
  { component: WeatherPage, key: 'weather' },
  { component: DeparturesPage, key: 'departures' },
  { component: LayoutPage, key: 'layout' },
  { component: SummaryPage, key: 'summary' },
];

const initialConfig: ConfigData = {
  language: 'hu',
  wifi_ssid: '',
  wifi_password: '',
  mqtt_server: '',
  mqtt_port: '1883',
  mqtt_departures_topic: '',
  mqtt_weather_topic: '',
  timezone: 'Europe/Budapest',
  weather_data_source: '0',
  departures_data_source: '0',
  weather_api_provider: '0',
  departures_api_provider: '0',
  location_name: '',
  location_lat: '',
  location_lon: '',
  bkk_api_key: '',
  bus_stop_id: '',
  train_stop_id: '',
};

const pageHeaders: Record<string, { hu: string; en: string }> = {
  language:   { hu: 'Nyelvválasztó',        en: 'Language' },
  general:    { hu: 'Általános',             en: 'General' },
  wifi:       { hu: 'WiFi beállítás',        en: 'WiFi Settings' },
  'wifi-test':{ hu: 'Csatlakozás...',        en: 'Connecting...' },
  weather:    { hu: 'Időjárás',              en: 'Weather' },
  departures: { hu: 'Menetrend',             en: 'Departures' },
  layout:     { hu: 'Elrendezés',            en: 'Layout' },
  summary:    { hu: 'Összegzés',             en: 'Summary' },
};

function App() {
  const [currentPage, setCurrentPage] = useState(0);
  const [config, setConfig] = useState<ConfigData>(initialConfig);
  const [isAnimating, setIsAnimating] = useState(false);

  const language = (config.language as Language) || 'hu';
  const t = translations[language];
  const CurrentPage = pages[currentPage].component;

  const handleNext = () => {
    if (currentPage < pages.length - 1) {
      setIsAnimating(true);
      setTimeout(() => {
        setCurrentPage(currentPage + 1);
        setIsAnimating(false);
      }, 400);
    }
  };

  const handlePrev = () => {
    if (currentPage > 0) {
      setIsAnimating(true);
      setTimeout(() => {
        setCurrentPage(currentPage - 1);
        setIsAnimating(false);
      }, 400);
    }
  };

  return (
    <div className="config-container">
      <div className="config-header">
        {pageHeaders[pages[currentPage].key]?.[language] ?? pages[currentPage].key}
      </div>
      
      <div className="config-content">
        <CurrentPage
          config={config}
          setConfig={setConfig}
          language={language}
          t={t}
          onNext={handleNext}
          onPrev={handlePrev}
        />
      </div>
    </div>
  );
}

export default App;
