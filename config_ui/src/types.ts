export type Language = 'hu' | 'en';

export interface ConfigData {
  language: Language;
  wifi_ssid: string;
  wifi_password: string;
  mqtt_server: string;
  mqtt_port: string;
  mqtt_departures_topic: string;
  mqtt_weather_topic: string;
  timezone: string;
  weather_data_source: string;
  departures_data_source: string;
  weather_api_provider: string;
  departures_api_provider: string;
  location_name: string;
  location_lat: string;
  location_lon: string;
  bkk_api_key: string;
  bus_stop_id: string;
  train_stop_id: string;
}

export interface WiFiTestResult {
  success: boolean;
  ssid: string;
  message: string;
}

export interface GeoLocation {
  name: string;
  admin1?: string;
  country?: string;
  latitude: number;
  longitude: number;
}

export const translations = {
  hu: {
    language: 'Nyelvválasztó',
    selectLanguage: 'Válassz nyelvet',
    english: 'Angol',
    hungarian: 'Magyar',
    
    general: 'Általános',
    generalSettings: 'Általános beállítások',
    
    wifi: 'WiFi',
    wifiSettings: 'WiFi beállítások',
    ssid: 'Hálózat neve (SSID)',
    password: 'Jelszó',
    
    wifiTest: 'WiFi csatlakozás',
    testing: 'Tesztelés folyamatban...',
    testSuccess: 'Sikeresen csatlakozva!',
    testFailed: 'Sikertelen csatlakozás',
    back: 'Vissza',
    skip: 'Kihagyás',
    retry: 'Újra próbálni',
    next: 'Tovább',
    scan: 'Frissítés',
    scanning: 'Keresés...',
    noNetworks: 'Nem találhatók hálózatok',
    manualEntry: 'Kézi megadás',
    showList: 'Lista megjelenítése',
    passwordHint: 'Legalább 8 karakter...',
    passwordTooShort: 'A jelszónak legalább 8 karakternek kell lennie',
    
    weather: 'Időjárás',
    weatherSettings: 'Időjárás beállítások',
    location: 'Hely',
    weatherSource: 'Forrás',
    weatherProvider: 'Szolgáltató',
    directApi: 'Közvetlen API',
    mqtt: 'MQTT',
    openmeteo: 'Open-Meteo',
    
    departures: 'Menetrend',
    departuresSettings: 'Menetrend beállítások',
    departuresSource: 'Forrás',
    departuresProvider: 'Szolgáltató',
    bkkApiKey: 'BKK API kulcs',
    busStopId: 'Autóbusz megálló ID',
    trainStopId: 'Vonat megálló ID',
    
    layout: 'Elrendezés',
    layoutSettings: 'Elrendezés beállítások',
    
    summary: 'Összegzés',
    summarySettings: 'Összegzés és befejezés',
    save: 'Mentés és indítás',
    reset: 'Gyári beállítások',
    
    mqttServer: 'MQTT szerver',
    mqttPort: 'MQTT port',
    mqttDepTopic: 'Menetrend téma',
    mqttWeatherTopic: 'Időjárás téma',
    timezone: 'Időzóna',
    confirm: 'Megerősítés',
    
    searchingLocations: 'Keresés...',
    noLocationsFound: 'Nem található helység',
    manualCoordinates: 'Koordináták kézi megadása',
    latitude: 'Szélesség',
    longitude: 'Hosszúság',
    invalidCoordinates: 'Érvénytelen koordináták',
    latitudeMustBeBetween: 'Szélességnek -90 és 90 közöttinek kell lennie',
    longitudeMustBeBetween: 'Hosszúságnak -180 és 180 közöttinek kell lennie',
    testButton: 'Teszt',
    testing: 'Tesztelés...',
    testSuccess: 'Teszt sikeres!',
    testFailed: 'Teszt sikertelen',
  },
  en: {
    language: 'Language',
    selectLanguage: 'Select language',
    english: 'English',
    hungarian: 'Hungarian',
    
    general: 'General',
    generalSettings: 'General settings',
    
    wifi: 'WiFi',
    wifiSettings: 'WiFi settings',
    ssid: 'Network name (SSID)',
    password: 'Password',
    
    wifiTest: 'WiFi connection',
    testing: 'Testing...',
    testSuccess: 'Connected successfully!',
    testFailed: 'Connection failed',
    back: 'Back',
    skip: 'Skip',
    retry: 'Retry',
    next: 'Next',
    scan: 'Refresh',
    scanning: 'Scanning...',
    noNetworks: 'No networks found',
    manualEntry: 'Enter manually',
    showList: 'Show list',
    passwordHint: 'At least 8 characters...',
    passwordTooShort: 'Password must be at least 8 characters',
    
    weather: 'Weather',
    weatherSettings: 'Weather settings',
    location: 'Location',
    weatherSource: 'Source',
    weatherProvider: 'Provider',
    directApi: 'Direct API',
    mqtt: 'MQTT',
    openmeteo: 'Open-Meteo',
    
    departures: 'Departures',
    departuresSettings: 'Departures settings',
    departuresSource: 'Source',
    departuresProvider: 'Provider',
    bkkApiKey: 'BKK API Key',
    busStopId: 'Bus stop ID',
    trainStopId: 'Train stop ID',
    
    layout: 'Layout',
    layoutSettings: 'Layout settings',
    
    summary: 'Summary',
    summarySettings: 'Summary and finish',
    save: 'Save and start',
    reset: 'Factory reset',
    
    mqttServer: 'MQTT server',
    mqttPort: 'MQTT port',
    mqttDepTopic: 'Departures topic',
    mqttWeatherTopic: 'Weather topic',
    timezone: 'Timezone',
    confirm: 'Confirm',    
    searchingLocations: 'Searching...',
    noLocationsFound: 'No locations found',
    manualCoordinates: 'Enter coordinates manually',
    latitude: 'Latitude',
    longitude: 'Longitude',
    invalidCoordinates: 'Invalid coordinates',
    latitudeMustBeBetween: 'Latitude must be between -90 and 90',
    longitudeMustBeBetween: 'Longitude must be between -180 and 180',
    testButton: 'Test',
    testing: 'Testing...',
    testSuccess: 'Test successful!',
    testFailed: 'Test failed',
  }
};

export type TranslationKey = keyof typeof translations.hu;
