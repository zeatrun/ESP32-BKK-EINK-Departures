import { WiFiTestResult, GeoLocation, ConfigData } from '../types';

const API_BASE = '/api';

export async function testWiFi(ssid: string, password: string): Promise<WiFiTestResult> {
  try {
    const response = await fetch(`${API_BASE}/wifi-test`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password })
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
