import { useState } from 'react';
import type { Telemetry } from '../../services/api';
import { HistoryModal } from '../HistoryModal';
import './LightSensor.css';

interface LightSensorProps {
  sensorName: string;
  nodeName: string;
  sensorId: number;
  data: Telemetry[];
  onDelete?: (sensorId: number, sensorName: string) => void;
}

export function LightSensor({ sensorName, nodeName, sensorId, data, onDelete }: LightSensorProps) {
  const [showHistory, setShowHistory] = useState(false);

  const brightnessData = data
    .filter((item) => item.key === 'brightness')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];

  const brightness = brightnessData?.valueNumeric ?? null;
  const timestamp = brightnessData?.time ? new Date(brightnessData.time).toLocaleString() : 'No data';
  const batteryPct = brightnessData?.batteryPct ?? null;
  const rssi = brightnessData?.rssi ?? null;
  const nodeId = data[0]?.nodeId || '';

  const getRssiLevel = (value: number | null) => {
    if (value === null) return { level: 0, color: 'gray', label: 'Unknown' };
    if (value >= -60) return { level: 3, color: 'green', label: 'Excellent' };
    if (value >= -80) return { level: 2, color: 'orange', label: 'Moderate' };
    return { level: 1, color: 'red', label: 'Poor' };
  };

  const getBrightnessState = (value: number | null) => {
    if (value === null) return { label: 'Unknown', className: 'unknown', icon: '◌' };
    if (value >= 75) return { label: 'Bright', className: 'bright', icon: '☀️' };
    if (value >= 35) return { label: 'Ambient', className: 'ambient', icon: '⛅' };
    return { label: 'Dim', className: 'dim', icon: '🌙' };
  };

  const rssiInfo = getRssiLevel(rssi);
  const brightnessState = getBrightnessState(brightness);

  return (
    <>
      <div className="light-sensor-card">
        <div className="light-sensor-header">
          <h3>{sensorName}</h3>
          <div className="header-actions">
            <span className="node-badge">{nodeName}</span>
            <button className="history-button" onClick={() => setShowHistory(true)}>📊</button>
            {onDelete && (
              <button
                className="delete-button"
                onClick={() => onDelete(sensorId, sensorName)}
                title="Delete sensor"
              >
                ✕
              </button>
            )}
          </div>
        </div>

        <div className="light-sensor-content">
          <div className="light-status-display">
            <div className={`light-icon ${brightnessState.className}`}>
              <span>{brightnessState.icon}</span>
            </div>
            <div className="light-status-text">
              <div className="status-label">Brightness</div>
              <div className="brightness-value">
                {brightness !== null ? `${brightness.toFixed(0)}%` : 'N/A'}
              </div>
              <div className={`status-badge ${brightnessState.className}`}>
                {brightnessState.label}
              </div>
            </div>
          </div>

          <div className="light-info">
            <div className="info-row">
              <span className="info-label">Current Level</span>
              <span className="info-value">{brightness !== null ? `${brightness.toFixed(0)}%` : 'No reading'}</span>
            </div>
            <div className="brightness-bar">
              <div
                className={`brightness-fill ${brightnessState.className}`}
                style={{ width: `${Math.max(0, Math.min(100, brightness ?? 0))}%` }}
              ></div>
            </div>
            <div className="info-row">
              <span className="info-label">Last Updated</span>
              <span className="info-value">{timestamp}</span>
            </div>
          </div>

          <div className="sensor-indicators">
            {batteryPct !== null && (
              <div className="battery-indicator">
                <span className="label">Battery</span>
                <div className="battery-bar">
                  <div
                    className="battery-fill"
                    style={{
                      width: `${batteryPct}%`,
                      backgroundColor: batteryPct > 20 ? '#4CAF50' : '#FF5722',
                    }}
                  ></div>
                </div>
                <span className="value">{batteryPct}%</span>
              </div>
            )}

            {rssi !== null && (
              <div className="rssi-indicator">
                <span className="label">Signal</span>
                <div className="antenna">
                  <div className="antenna-bar" style={{ height: '10px', backgroundColor: rssiInfo.level >= 1 ? rssiInfo.color : 'gray' }}></div>
                  <div className="antenna-bar" style={{ height: '15px', backgroundColor: rssiInfo.level >= 2 ? rssiInfo.color : 'gray' }}></div>
                  <div className="antenna-bar" style={{ height: '20px', backgroundColor: rssiInfo.level >= 3 ? rssiInfo.color : 'gray' }}></div>
                </div>
                <span className="value">{rssiInfo.label}</span>
              </div>
            )}
          </div>
        </div>
      </div>
      <HistoryModal
        isOpen={showHistory}
        onClose={() => setShowHistory(false)}
        nodeId={nodeId}
        sensorId={sensorId}
        sensorType="light"
        sensorName={sensorName}
      />
    </>
  );
}
