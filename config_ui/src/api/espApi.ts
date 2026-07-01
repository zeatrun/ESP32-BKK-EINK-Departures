import { WiFiTestResult, GeoLocation, ConfigData } from '../types';
export interface WiFiNetwork {
  ssid: string;
  rssi: number;
  open: boolean;
}

export interface WiFiScanResponse {
  status: 'scanning' | 'done' | 'failed';
  networks: WiFiNetwork[];
}

export async function scanWiFiNetworks(forceRefresh = false): Promise<WiFiScanResponse> {
  try {
    const response = await fetch(`${API_BASE}/wifi-scan${forceRefresh ? '?refresh=1' : ''}`);
    const data = await response.json();
    return {
      status: data.status ?? 'done',
      networks: data.networks ?? []
    };
  } catch (error) {
    return {
      status: 'failed',
      networks: []
    };
  }
}


const API_BASE = '/api';

export async function testWiFi(ssid: string, password: string): Promise<WiFiTestResult> {
  try {
    const body = new URLSearchParams({
      ssid,
      password,
    }).toString();

    const response = await fetch(`${API_BASE}/wifi-test`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
      body
    });
    
    const data = await response.json();
    return data;
  } catch (error) {
    return {
      success: false,
      ssid,
      message: 'Network error'
    };
  }
}

export async function searchLocations(query: string): Promise<GeoLocation[]> {
  try {
    const response = await fetch(`${API_BASE}/geocode?q=${encodeURIComponent(query)}`);
    const data = await response.json();
    
    if (data.results) {
      return data.results.map((result: any) => ({
        name: result.name,
        admin1: result.admin1 || '',
        country: result.country || '',
        latitude: result.latitude,
        longitude: result.longitude
      }));
    }
    return [];
  } catch (error) {
    console.error('Geocoding error:', error);
    return [];
  }
}

export async function saveConfiguration(config: ConfigData): Promise<{ success: boolean; message: string }> {
  try {
    const response = await fetch(`${API_BASE}/config/save`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config)
    });
    
    const data = await response.json();
    return data;
  } catch (error) {
    return {
      success: false,
      message: 'Failed to save configuration'
    };
  }
}

export async function testWeather(lat: string, lon: string): Promise<{ success: boolean; message: string }> {
  try {
    const response = await fetch(`${API_BASE}/weather-test?lat=${encodeURIComponent(lat)}&lon=${encodeURIComponent(lon)}`);
    const data = await response.json();
    return data;
  } catch (error) {
    return {
      success: false,
      message: 'Failed to fetch weather data'
    };
  }
}

export async function testDepartures(apiKey: string, busStopId: string, trainStopId: string): Promise<{ success: boolean; message: string }> {
  try {
    const body = new URLSearchParams({
      apiKey,
      busStopId,
      trainStopId,
    }).toString();

    const response = await fetch(`${API_BASE}/departures-test`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
      body
    });
    const data = await response.json();
    return data;
  } catch (error) {
    return {
      success: false,
      message: 'Failed to fetch departures data'
    };
  }
}

export async function factoryReset(): Promise<{ success: boolean; message: string }> {
  try {
    const response = await fetch(`${API_BASE}/config/reset`, { method: 'POST' });
    const data = await response.json();
    return data;
  } catch (error) {
    return {
      success: false,
      message: 'Failed to reset'
    };
  }
}
