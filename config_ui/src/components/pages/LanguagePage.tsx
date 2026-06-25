import React from 'react';
import { Language, ConfigData } from '../../types';

interface PageProps {
  config: ConfigData;
  setConfig: (config: ConfigData) => void;
  language: Language;
  t: any;
  onNext: () => void;
  onPrev: () => void;
}

export default function LanguagePage({ setConfig, t, onNext }: PageProps) {
  const handleSelectLanguage = (lang: Language) => {
    setConfig(prev => ({ ...prev, language: lang }));
    setTimeout(() => onNext(), 300);
  };

  return (
    <div className="page active">
      <div className="language-grid">
        <div
          className="language-box"
          onClick={() => handleSelectLanguage('en')}
        >
          🇬🇧 {t.english}
        </div>
        <div
          className="language-box"
          onClick={() => handleSelectLanguage('hu')}
        >
          🇭🇺 {t.hungarian}
        </div>
      </div>
      
      <div style={{ flex: 1 }} />
    </div>
  );
}
