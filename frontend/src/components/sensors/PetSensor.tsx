import { useEffect, useState } from 'react';
import { getTelemetry, type Telemetry } from '../../services/api';
import { HistoryModal } from '../HistoryModal';
import './PetSensor.css';

interface PetSensorProps {
  sensorName: string;
  nodeId: string;
  nodeName: string;
  sensorId: number;
  latestData: Telemetry;
}

export function PetSensor({ sensorName, nodeId, nodeName, sensorId, latestData }: PetSensorProps) {
  const [lastDetectionTime, setLastDetectionTime] = useState<Date | null>(null);
  const [isFetchingHistory, setIsFetchingHistory] = useState(false);
  const [showHistory, setShowHistory] = useState(false);

  // 1. The absolute newest ping from the sensor (for battery and hardware health)
  const lastSensorPingTime = latestData?.time ? new Date(latestData.time) : null;

  useEffect(() => {
    // Check if the pet is detected in the absolute latest ping
    const isCurrentlyDetected = 
      latestData.valueBool === true || 
      latestData.valueText === 'detected' || 
      latestData.valueText === 'present' || 
      latestData.valueNumeric === 1;

    if (isCurrentlyDetected) {
      // OPTIMIZATION: If they are present right now, we don't need to fetch history!
      setLastDetectionTime(new Date(latestData.time));
      return;
    }

    // If absent, we must fetch this specific sensor's history to find the last detection
    const fetchDetectionHistory = async () => {
      try {
        setIsFetchingHistory(true);
        // We use the API filters you specifically built to only grab this sensor's data
        const history = await getTelemetry({ nodeId, sensorId, limit: 50 });

        const lastDetectedReading = history.find(
          (d) => d.valueBool === true || d.valueText === 'detected' || d.valueText === 'present' || d.valueNumeric === 1
        );

        if (lastDetectedReading) {
          setLastDetectionTime(new Date(lastDetectedReading.time));
        } else {
          setLastDetectionTime(null);
        }
      } catch (error) {
        console.error(`Failed to fetch history for pet sensor ${sensorId}`, error);
      } finally {
        setIsFetchingHistory(false);
      }
    };

    fetchDetectionHistory();
  }, [nodeId, sensorId, latestData]);

  // Logic: Pet is present ONLY if they were detected within the last 5 minutes (300,000 ms)
  const isPresent = lastDetectionTime 
    ? (Date.now() - lastDetectionTime.getTime()) <= 5 * 60 * 1000 
    : false;

  // Formatting helpers
  const sensorTimeAgo = lastSensorPingTime ? getTimeAgo(lastSensorPingTime) : 'Never';
  const detectionTimeAgo = lastDetectionTime ? getTimeAgo(lastDetectionTime) : 'Never';

  function getTimeAgo(date: Date): string {
    const seconds = Math.floor((Date.now() - date.getTime()) / 1000);
    if (seconds < 60) return `${seconds}s ago`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
    if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`;
    return `${Math.floor(seconds / 86400)}d ago`;
  }

  const rssi = latestData?.rssi ?? null;

  const getRssiLevel = (rssi: number | null) => {
    if (rssi === null) return { level: 0, color: 'gray', label: 'Unknown' };
    if (rssi >= -60) return { level: 3, color: 'green', label: 'Excellent' };
    if (rssi >= -80) return { level: 2, color: 'orange', label: 'Moderate' };
    return { level: 1, color: 'red', label: 'Poor' };
  };

  const rssiInfo = getRssiLevel(rssi);

  return (
    <>
      <div className="pet-sensor-card">
        <div className="pet-sensor-header">
          <h3>{sensorName}</h3>
          <div className="header-actions">
            <button className="history-button" onClick={() => setShowHistory(true)}>📊</button>
            <span className="node-badge">{nodeName}</span>
          </div>
        </div>

        <div className="pet-sensor-content">
          <div className="pet-status-display">
            <div className={`pet-icon ${isPresent ? 'present' : 'absent'}`}>
              {isPresent ? '🐾' : '👁️'}
            </div>
            <div className="pet-status-text">
              <div className="status-label">Current Status</div>
              <div className={`status-value ${isPresent ? 'present' : 'absent'}`}>
                {isPresent ? 'Present' : 'Absent'}
              </div>
            </div>
          </div>

          <div className="pet-info">
            <div className="info-item">
              <span className="info-label">Sensor Last Ping</span>
              <span className="info-value">{sensorTimeAgo}</span>
            </div>
            
            <div className="info-item">
              <span className="info-label">Pet Last Detected</span>
              <span className="info-value">
                {isFetchingHistory 
                  ? 'Searching history...' 
                  : lastDetectionTime 
                    ? `${lastDetectionTime.toLocaleTimeString()} (${detectionTimeAgo})` 
                    : 'No history'
                }
              </span>
            </div>
          </div>

          <div className="sensor-indicators">
            {latestData?.batteryPct !== undefined && latestData?.batteryPct !== null && (
              <div className="battery-indicator">
                <span className="label">Battery</span>
                <div className="battery-bar">
                  <div
                    className="battery-fill"
                    style={{
                      width: `${latestData.batteryPct}%`,
                      backgroundColor: latestData.batteryPct > 20 ? '#4CAF50' : '#FF5722'
                    }}
                  ></div>
                </div>
                <span className="value">{latestData.batteryPct}%</span>
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
        sensorType="pet"
        sensorName={sensorName}
      />
    </>
  );
}