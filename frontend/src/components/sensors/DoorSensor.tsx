import { useState } from 'react';
import type { Telemetry } from '../../services/api';
import { HistoryModal } from '../HistoryModal';
import './DoorSensor.css';

interface DoorSensorProps {
  sensorName: string;
  nodeName: string;
  sensorId: number;
  data: Telemetry[];
}

export function DoorSensor({ sensorName, nodeName, sensorId, data }: DoorSensorProps) {
  const latestData = data.length > 0 ? data[data.length - 1] : null;
  const [showHistory, setShowHistory] = useState(false);
  
  // Door state: true = locked, false = unlocked (or determine from data)
  const isLocked = latestData?.valueBool ?? true;
  const timestamp = latestData?.time ? new Date(latestData.time).toLocaleString() : 'No data';

  const batteryPct = latestData?.batteryPct ?? null;
  const rssi = latestData?.rssi ?? null;

  const getRssiLevel = (rssi: number | null) => {
    if (rssi === null) return { level: 0, color: 'gray', label: 'Unknown' };
    if (rssi >= -60) return { level: 3, color: 'green', label: 'Excellent' };
    if (rssi >= -80) return { level: 2, color: 'orange', label: 'Moderate' };
    return { level: 1, color: 'red', label: 'Poor' };
  };

  const rssiInfo = getRssiLevel(rssi);

  const handleToggle = () => {
    // Send command to lock/unlock
    console.log(`Toggling ${sensorName}`);
  };

  return (
    <>
      <div className="door-sensor-card">
        <div className="door-sensor-header">
          <h3>{sensorName}</h3>
          <div className="header-actions">
            <button className="history-button" onClick={() => setShowHistory(true)}>📊</button>
            <span className="node-badge">{nodeName}</span>
          </div>
        </div>

        <div className="door-sensor-content">
          <div className="door-state-display">
            <button
              className={`door-button ${isLocked ? 'locked' : 'unlocked'}`}
              onClick={handleToggle}
              title={isLocked ? 'Click to unlock' : 'Click to lock'}
            >
              {isLocked ? '🔒' : '🔓'}
            </button>
            <div className="door-state-text">
              <div className="state-label">Door Status</div>
              <div className={`state-value ${isLocked ? 'locked' : 'unlocked'}`}>
                {isLocked ? 'Locked' : 'Unlocked'}
              </div>
            </div>
          </div>

          <div className="door-info">
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
                      backgroundColor: batteryPct > 20 ? '#4CAF50' : '#FF5722'
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
        nodeId={latestData?.nodeId || ''}
        sensorId={sensorId}
        sensorType="door"
        sensorName={sensorName}
      />
    </>
  );
}
