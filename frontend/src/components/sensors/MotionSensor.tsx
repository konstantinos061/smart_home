import { useState } from 'react';
import { type Telemetry } from '../../services/api';
import { HistoryModal } from '../HistoryModal';
import './MotionSensor.css';

interface MotionSensorProps {
  sensorName: string;
  nodeName: string;
  sensorId: number;
  data: Telemetry[];
  onDelete?: (sensorId: number, sensorName: string) => void;
}

export function MotionSensor({ sensorName, nodeName, sensorId, data, onDelete }: MotionSensorProps) {
  const [showHistory, setShowHistory] = useState(false);

  // Get latest readings
  const motionData = data
    .filter(d => d.key === 'motionStatus')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];

  const countData = data
    .filter(d => d.key === 'counts')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];

  // Logic for UI
  const isMotionActive = motionData?.valueNumeric === 1;
  const lastMotionTimestamp = motionData?.time 
    ? new Date(motionData.time).toLocaleString() 
    : 'No motion detected';

  const batteryPct = motionData?.batteryPct ?? countData?.batteryPct ?? null;
  const rssi = motionData?.rssi ?? motionData?.rssi ?? null;
  const nodeId = data[0]?.nodeId || '';

  const getRssiLevel = (rssi: number | null) => {
    if (rssi === null) return { level: 0, color: 'gray', label: 'Unknown' };
    if (rssi >= -60) return { level: 3, color: 'green', label: 'Excellent' };
    if (rssi >= -80) return { level: 2, color: 'orange', label: 'Moderate' };
    return { level: 1, color: 'red', label: 'Poor' };
  };

  const rssiInfo = getRssiLevel(rssi);

  return (
    <>
      <div className={`motion-sensor-card ${isMotionActive ? 'active-alert' : ''}`}>
        <div className="motion-sensor-header">
          <h3>{sensorName}</h3>
          <div className="header-actions">
            <span className="node-badge">{nodeName}</span>
            <button className="history-button" onClick={() => setShowHistory(true)}>📊</button>
            {onDelete && (
              <button className="delete-button" onClick={() => onDelete(sensorId, sensorName)}>✕</button>
            )}
          </div>
        </div>

        <div className="motion-sensor-content">
          <div className="status-display">
            <div className={`status-orb ${isMotionActive ? 'detected' : 'still'}`}>
              <div className="status-icon">
                {isMotionActive ? '🚶' : '🏠'}
              </div>
              <div className="status-text">
                {isMotionActive ? 'MOTION' : 'STILL'}
              </div>
            </div>
          </div>

          <div className="info-list">
            <div className="info-item">
              <span className="label">Counter</span>
              <span className="value-counts">{countData?.valueNumeric ?? 0}</span>
            </div>

            <div className="info-item">
              <span className="label">Last Activity</span>
              <span className="value-small">{lastMotionTimestamp}</span>
            </div>
          </div>

          <div className="sensor-indicators">
            {batteryPct !== null && (
              <div className="indicator-group">
                <span className="label">Battery</span>
                <div className="battery-container">
                  <div className="battery-fill" style={{ 
                    width: `${batteryPct}%`, 
                    backgroundColor: batteryPct > 20 ? '#4CAF50' : '#FF5722' 
                  }} />
                </div>
                <span className="value">{batteryPct}%</span>
              </div>
            )}

            {rssi !== null && (
              <div className="indicator-group">
                <span className="label">Signal</span>
                <div className="antenna">
                  {[10, 15, 20].map((h, i) => (
                    <div key={i} className="antenna-bar" style={{ 
                      height: `${h}px`, 
                      backgroundColor: rssiInfo.level > i ? rssiInfo.color : 'gray' 
                    }} />
                  ))}
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
        sensorType="motion"
        sensorName={sensorName}
      />
    </>
  );
}