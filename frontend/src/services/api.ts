import axios from 'axios';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'http://localhost:8000';

export const api = axios.create({
  baseURL: API_BASE_URL,
  headers: {
    'Content-Type': 'application/json',
  },
});

export const getRealtimeUrl = (): string => {
  const url = new URL(API_BASE_URL, window.location.origin);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  url.pathname = '/api/v1/ws';
  url.search = '';
  url.hash = '';
  return url.toString();
};

// ==========================================
// Data Transfer Objects (DTOs)
// ==========================================

export interface Telemetry {
  time: string;
  nodeId: string;
  nodeName?: string | null;
  sensorId: number;
  sensorName?: string | null;
  sensorType: string;
  key: string;
  unit?: string | null;
  valueNumeric?: number | null;
  valueText?: string | null;
  valueBool?: boolean | null;
  rssi?: number | null;
  snr?: number | null;
  batteryPct?: number | null;
  rawPayloadHex?: string | null;
}

export interface NodeLatest {
  nodeId: string;
  nodeName?: string | null;
  lastSeenAt?: string | null;
  isActive: boolean;
  latestEventType?: string | null;
  batteryVoltage?: number | null;
  batteryPct?: number | null;
  rssi?: number | null;
  snr?: number | null;
  // Based on your new backend, latestTelemetry uses the full Telemetry schema
  latestTelemetry: Telemetry[]; 
}

export interface StatusResponse {
  status: string;
}

export interface CommandResponse {
  status: string;
  devEui: string;
  deviceQueueUrl: string;
  body: {
    flushQueue: boolean;
    queueItem: {
      confirmed: boolean;
      data: string;
      fCntDown?: number | null;
      fPort: number;
      id?: string | null;
      isEncrypted?: boolean | null;
      isPending?: boolean | null;
    };
  };
  chirpstackResponse?: Record<string, any> | null;
}

export interface SensorInfo {
  sensorId: number;
  nodeId?: string | null;
  sensorName?: string | null;
  sensorType: string;
  batteryPct?: number | null;
  isActive: boolean;
}

// ==========================================
// API Functions
// ==========================================

export const getHealth = async (): Promise<StatusResponse> => {
  const response = await api.get('/api/v1/health');
  return response.data;
};

// --- Telemetry & Nodes ---

export const getTelemetry = async (params?: {
  nodeId?: string;
  sensorId?: number;
  start?: string;
  end?: string;
  limit?: number;
}): Promise<Telemetry[]> => {
  const response = await api.get('/api/v1/telemetry', { params });
  return response.data;
};

// Note: Renamed from getNodeLatest to reflect the new bulk backend endpoint
export const getAllNodesLatest = async (): Promise<NodeLatest[]> => {
  const response = await api.get('/api/v1/node/latest');
  return response.data;
};

export const getAllSensors = async (): Promise<SensorInfo[]> => {
  const response = await api.get('/api/v1/sensors');
  return response.data;
};

// --- Administration & Commands ---

export const createNode = async (payload: { nodeId: string; name?: string }): Promise<StatusResponse> => {
  const response = await api.post('/api/v1/node', payload);
  return response.data;
};

export const createSensor = async (payload: { id: number; name?: string }): Promise<StatusResponse> => {
  const response = await api.post('/api/v1/sensors', payload);
  return response.data;
};

export const renameSensor = async (sensorId: number, name: string): Promise<StatusResponse> => {
  const response = await api.post(`/api/v1/sensors/${sensorId}/name`, { name });
  return response.data;
};

export const deleteSensor = async (sensorId: number): Promise<StatusResponse> => {
  const response = await api.post(`/api/v1/sensors/${sensorId}/delete`);
  return response.data;
};

export const sendCommand = async (
  nodeId: string,
  command: {
    requestedBy?: string;
    commandType: string;
    payload: Record<string, any>;
    expiresAt?: string;
    confirmed?: boolean;
    flushQueue?: boolean;
    fCntDown?: number;
    id?: string;
    isEncrypted?: boolean;
    isPending?: boolean;
  }
): Promise<CommandResponse> => {
  const response = await api.post(`/api/v1/nodes/${nodeId}/commands`, command);
  return response.data;
};

// --- Hardware Ingestion (Usually for admin simulation/testing) ---

export const ingestStatus = async (payload: any): Promise<StatusResponse> => {
  const response = await api.post('/api/v1/node/status', payload);
  return response.data;
};

export const ingestUplink = async (payload: any): Promise<StatusResponse> => {
  const response = await api.post('/api/v1/uplink', payload);
  return response.data;
};
