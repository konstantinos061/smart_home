import { useEffect, useState } from 'react';
import type { Telemetry } from '../../services/api';
import { HistoryModal } from '../HistoryModal';
import './ThermostatSensor.css';

interface ThermostatSensorProps {
  sensorName: string;
  nodeName: string;
  sensorId: number;
  data: Telemetry[];
}

export function ThermostatSensor({ sensorName, nodeName, sensorId, data }: ThermostatSensorProps) {
  const setTempData = data
    .filter(d => d.key === 'setTemperature')
    .sort((a, b) => new Date(b.time).getTime() - new Date(a.time).getTime())[0];

  const [setTemp, setSetTemp] = useState<number>(22);
  const [showHistory, setShowHistory] = useState(false);

  useEffect(() => {
    if (setTempData?.valueNumeric != null) {
      setSetTemp(setTempData.valueNumeric);
      return;
    }

    if (data.length > 0) {
      setSetTemp(22);
    }
  }, [setTempData?.valueNumeric, data.length]);
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

  const batteryPct = tempData?.batteryPct ?? humidityData?.batteryPct ?? null;
  const rssi = tempData?.rssi ?? humidityData?.rssi ?? null;

  const getRssiLevel = (rssi: number | null) => {
    if (rssi === null) return { level: 0, color: 'gray', label: 'Unknown' };
    if (rssi >= -60) return { level: 3, color: 'green', label: 'Excellent' };
    if (rssi >= -80) return { level: 2, color: 'orange', label: 'Moderate' };
    return { level: 1, color: 'red', label: 'Poor' };
  };

  const rssiInfo = getRssiLevel(rssi);

  const handleAdjustTemp = (delta: number) => {
    setSetTemp(prev => Math.max(15, Math.min(30, prev + delta)));
  };

  const tempDiff = currentTemp ? setTemp - currentTemp : 0;
  const isHeating = tempDiff > 0;

  return (
    <>
      <div className="thermostat-sensor-card">
        <div className="thermostat-sensor-header">
          <h3>{sensorName}</h3>
          <div className="header-actions">
            <button className="history-button" onClick={() => setShowHistory(true)}>📊</button>
            <span className="node-badge">{nodeName}</span>
          </div>
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
              <button onClick={() => handleAdjustTemp(-0.1)}>−</button>
              <span className="temp-value">{setTemp.toFixed(1)}°C</span>
              <button onClick={() => handleAdjustTemp(0.1)}>+</button>
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
        nodeId={data[0]?.nodeId || ''}
        sensorId={sensorId}
        sensorType="thermostat"
        sensorName={sensorName}
      />
    </>
  );
}
