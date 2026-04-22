import { useState } from 'react';
import type { Telemetry } from '../../services/api';
import './ThermostatSensor.css';

interface ThermostatSensorProps {
  sensorName: string;
  nodeId: string;
  data: Telemetry[];
}

export function ThermostatSensor({ sensorName, nodeId, data }: ThermostatSensorProps) {
  const [setTemp, setSetTemp] = useState<number>(22);

  // Find latest temperature and humidity readings
  const tempData = data
    .filter(d => d.key === 'temperature')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];

  const humidityData = data
    .filter(d => d.key === 'humidity')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];

  const currentTemp = tempData?.valueNumeric ?? null;
  const currentHumidity = humidityData?.valueNumeric ?? null;
  const tempTimestamp = tempData?.time ? new Date(tempData.time).toLocaleString() : 'No data';

  const handleAdjustTemp = (delta: number) => {
    setSetTemp(prev => Math.max(15, Math.min(30, prev + delta)));
  };

  const tempDiff = currentTemp ? setTemp - currentTemp : 0;
  const isHeating = tempDiff > 0;

  return (
    <div className="thermostat-sensor-card">
      <div className="thermostat-sensor-header">
        <h3>{sensorName}</h3>
        <span className="node-badge">{nodeId}</span>
      </div>

      <div className="thermostat-sensor-content">
        <div className="temperature-display">
          <div className={`temp-circle ${isHeating ? 'heating' : currentTemp! < setTemp ? 'cooling' : 'stable'}`}>
            <div className="temp-value">
              {currentTemp !== null ? `${currentTemp.toFixed(1)}°` : 'N/A'}
            </div>
            <div className="temp-unit">C</div>
          </div>
        </div>

        <div className="thermostat-controls">
          <div className="control-row">
            <label>Set Temperature</label>
            <div className="temp-control">
              <button onClick={() => handleAdjustTemp(-1)}>−</button>
              <span className="temp-value">{setTemp.toFixed(1)}°C</span>
              <button onClick={() => handleAdjustTemp(1)}>+</button>
            </div>
          </div>

          <div className="humidity-row">
            <span className="label">Humidity</span>
            <span className="value">
              {currentHumidity !== null ? `${currentHumidity.toFixed(0)}%` : 'N/A'}
            </span>
          </div>

          <div className="status-row">
            <span className="label">Status</span>
            <span className={`status-badge ${isHeating ? 'heating' : 'idle'}`}>
              {isHeating ? '🔥 Heating' : currentTemp! < setTemp ? '❄️ Cooling' : '✓ Stable'}
            </span>
          </div>

          <div className="timestamp-row">
            <span className="label">Last Update</span>
            <span className="value">{tempTimestamp}</span>
          </div>
        </div>
      </div>
    </div>
  );
}
