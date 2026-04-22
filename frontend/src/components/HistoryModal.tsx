import { useEffect, useState } from 'react';
import { getTelemetry, type Telemetry } from '../services/api';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';
import './HistoryModal.css';

interface HistoryModalProps {
  isOpen: boolean;
  onClose: () => void;
  nodeId: string;
  sensorId: number;
  sensorType: string;
  sensorName: string;
}

export function HistoryModal({ isOpen, onClose, nodeId, sensorId, sensorType, sensorName }: HistoryModalProps) {
  const [history, setHistory] = useState<Telemetry[]>([]);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (isOpen) {
      fetchHistory();
    }
  }, [isOpen, nodeId, sensorId]);

  const fetchHistory = async () => {
    setLoading(true);
    try {
      const data = await getTelemetry({ nodeId, sensorId, limit: 100 });
      setHistory(data);
    } catch (error) {
      console.error('Failed to fetch history', error);
    } finally {
      setLoading(false);
    }
  };

  if (!isOpen) return null;

  const prepareChartData = () => {
    if (sensorType === 'thermostat') {
      // Group by time, collect temp and humidity
      const timeMap = new Map<string, { time: string, temperature?: number, humidity?: number }>();
      history.forEach(item => {
        const timeKey = new Date(item.time).toLocaleTimeString();
        if (!timeMap.has(timeKey)) {
          timeMap.set(timeKey, { time: timeKey });
        }
        const entry = timeMap.get(timeKey)!;
        if (item.key === 'temperature') entry.temperature = item.valueNumeric;
        if (item.key === 'humidity') entry.humidity = item.valueNumeric;
      });
      return Array.from(timeMap.values());
    } else {
      // For other sensors, just plot valueNumeric or valueBool as number
      return history.map(item => ({
        time: new Date(item.time).toLocaleTimeString(),
        value: item.valueNumeric ?? (item.valueBool ? 1 : 0),
        key: item.key
      }));
    }
  };

  const chartData = prepareChartData();

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-content" onClick={e => e.stopPropagation()}>
        <div className="modal-header">
          <h2>History: {sensorName}</h2>
          <button className="close-button" onClick={onClose}>×</button>
        </div>
        <div className="modal-body">
          {loading ? (
            <div>Loading history...</div>
          ) : (
            <ResponsiveContainer width="100%" height={400}>
              <LineChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="time" />
                <YAxis />
                <Tooltip />
                <Legend />
                {sensorType === 'thermostat' ? (
                  <>
                    <Line type="monotone" dataKey="temperature" stroke="#8884d8" name="Temperature (°C)" />
                    <Line type="monotone" dataKey="humidity" stroke="#82ca9d" name="Humidity (%)" />
                  </>
                ) : (
                  <Line type="monotone" dataKey="value" stroke="#8884d8" name="Value" />
                )}
              </LineChart>
            </ResponsiveContainer>
          )}
        </div>
      </div>
    </div>
  );
}