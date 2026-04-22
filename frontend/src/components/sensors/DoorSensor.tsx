import type { Telemetry } from '../../services/api';
import './DoorSensor.css';

interface DoorSensorProps {
  sensorName: string;
  nodeId: string;
  data: Telemetry[];
}

export function DoorSensor({ sensorName, nodeId, data }: DoorSensorProps) {
  const latestData = data.length > 0 ? data[data.length - 1] : null;
  
  // Door state: true = locked, false = unlocked (or determine from data)
  const isLocked = latestData?.valueBool ?? true;
  const timestamp = latestData?.time ? new Date(latestData.time).toLocaleString() : 'No data';

  const handleToggle = () => {
    // Send command to lock/unlock
    console.log(`Toggling ${sensorName} on ${nodeId}`);
  };

  return (
    <div className="door-sensor-card">
      <div className="door-sensor-header">
        <h3>{sensorName}</h3>
        <span className="node-badge">{nodeId}</span>
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
      </div>
    </div>
  );
}
