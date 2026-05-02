import { useState } from 'react';
import { sendCommand, type Telemetry } from '../../services/api';
import { HistoryModal } from '../HistoryModal';
import './DoorSensor.css';

interface DoorSensorProps {
  sensorName: string;
  nodeName: string;
  sensorId: number;
  data: Telemetry[];
  onDelete?: (sensorId: number, sensorName: string) => void;
}

export function DoorSensor({ sensorName, nodeName, sensorId, data, onDelete }: DoorSensorProps) {
  const succTries = data
    .filter(d => d.key === 'successfulAttempts')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];
  const failedTried = data
    .filter(d => d.key === 'failedAttempts')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];
  const [showHistory, setShowHistory] = useState(false);
  const [commandLoading, setCommandLoading] = useState(false);
  const [commandStatus, setCommandStatus] = useState<string | null>(null);
  
  // Door state: true = locked, false = unlocked (or determine from data)
  const isLocked = true;
  const timestamp = succTries?.time ? new Date(succTries.time).toLocaleString() : 'No data';

  const batteryPct = succTries?.batteryPct ?? null;
  const rssi = succTries?.rssi ?? null;

  const getRssiLevel = (rssi: number | null) => {
    if (rssi === null) return { level: 0, color: 'gray', label: 'Unknown' };
    if (rssi >= -60) return { level: 3, color: 'green', label: 'Excellent' };
    if (rssi >= -80) return { level: 2, color: 'orange', label: 'Moderate' };
    return { level: 1, color: 'red', label: 'Poor' };
  };

  const rssiInfo = getRssiLevel(rssi);
  const nodeId = data[0]?.nodeId || '';

  const handleToggle = async () => {
    if (!nodeId) {
      setCommandStatus('No node available');
      return;
    }

    try {
      setCommandLoading(true);
      setCommandStatus(null);
      await sendCommand(nodeId, {
        commandType: 'openDoor',
        requestedBy: 'dashboard',
        payload: {
          sensorId,
        },
      });
      setCommandStatus('Queued');
    } catch (err) {
      console.error('Failed to enqueue set temperature downlink:', err);
      setCommandStatus('Failed');
    } finally {
      setCommandLoading(false);
    }
  };

  return (
    <>
      <div className="door-sensor-card">
        <div className="door-sensor-header">
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

        <div className="door-sensor-content">
          <div className="door-state-display">
            <button
              className={`door-button ${isLocked ? 'locked' : 'unlocked'}`}
              onClick={handleToggle}
              title={isLocked ? 'Click to unlock' : 'Click to lock'}
              disabled={commandLoading}
            >
              {isLocked ? '🔒' : '🫢'}
            </button>
            {commandStatus && <div className="command-message">{commandStatus}</div>}
            <div className="door-state-text">
              <div className="state-label">Homies</div>
              <div className={`state-value locked`}>
                {succTries.valueNumeric}
              </div>
              <div className="state-label">Impostors</div>
              <div className={`state-value unlocked`}>
                {succTries.valueNumeric}
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
        nodeId={succTries?.nodeId || ''}
        sensorId={sensorId}
        sensorType="door"
        sensorName={sensorName}
      />
    </>
  );
}
