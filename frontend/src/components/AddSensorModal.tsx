import { useState, useEffect } from 'react';
import { createSensor } from '../services/api';
import './AddSensorModal.css';

interface AddSensorModalProps {
  isOpen: boolean;
  onClose: () => void;
  onSensorAdded: () => void;
}

export function AddSensorModal({ isOpen, onClose, onSensorAdded }: AddSensorModalProps) {
  const [sensorId, setSensorId] = useState<string>('');
  const [sensorName, setSensorName] = useState<string>('');
  const [sensorType, setSensorType] = useState<string>('unknown');
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  // Determine sensor type based on first 2 bits of ID
  useEffect(() => {
    if (sensorId === '') {
      setSensorType('unknown');
      return;
    }

    const id = parseInt(sensorId, 10);
    if (isNaN(id) || id < 0 || id > 255) {
      setSensorType('invalid');
      return;
    }

    // Extract the first 2 bits (bits 6-7)
    const typeBits = (id >> 6) & 0b11;
    const typeMap: Record<number, string> = {
      0b00: 'thermostat',
      0b01: 'door',
      0b10: 'pet',
    };
    setSensorType(typeMap[typeBits] || 'unknown');
  }, [sensorId]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);

    if (sensorId === '' || sensorName === '') {
      setError('Please fill in all fields');
      return;
    }

    const id = parseInt(sensorId, 10);
    if (isNaN(id) || id < 0 || id > 255) {
      setError('Sensor ID must be between 0 and 255');
      return;
    }

    if (sensorType === 'invalid' || sensorType === 'unknown') {
      setError('Invalid sensor ID or type');
      return;
    }

    setLoading(true);
    try {
      await createSensor({ id, name: sensorName });
      setSensorId('');
      setSensorName('');
      setSensorType('unknown');
      onSensorAdded();
      onClose();
    } catch (err) {
      setError('Failed to create sensor. Please try again.');
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  if (!isOpen) return null;

  const typeColors: Record<string, string> = {
    'thermostat': '#FF6B6B',
    'door': '#4ECDC4',
    'pet': '#95E1D3',
    'unknown': '#CCCCCC',
    'invalid': '#FFB6B6',
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-content" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Add New Sensor</h2>
          <button className="close-button" onClick={onClose}>×</button>
        </div>

        <form onSubmit={handleSubmit} className="add-sensor-form">
          <div className="form-group">
            <label htmlFor="sensorId">Sensor ID (0-255)</label>
            <input
              id="sensorId"
              type="number"
              min="0"
              max="255"
              value={sensorId}
              onChange={(e) => setSensorId(e.target.value)}
              placeholder="Enter sensor ID"
              disabled={loading}
            />
            {sensorId !== '' && (
              <div className="type-display" style={{ borderColor: typeColors[sensorType] }}>
                <span className="type-label">Sensor Type:</span>
                <span className="type-value" style={{ color: typeColors[sensorType] }}>
                  {sensorType.charAt(0).toUpperCase() + sensorType.slice(1)}
                </span>
              </div>
            )}
          </div>

          <div className="form-group">
            <label htmlFor="sensorName">Sensor Name</label>
            <input
              id="sensorName"
              type="text"
              value={sensorName}
              onChange={(e) => setSensorName(e.target.value)}
              placeholder="e.g., Living Room Thermostat"
              disabled={loading}
            />
          </div>

          {error && <div className="error-message">{error}</div>}

          <div className="modal-actions">
            <button type="button" className="btn-cancel" onClick={onClose} disabled={loading}>
              Cancel
            </button>
            <button type="submit" className="btn-submit" disabled={loading || sensorType === 'invalid' || sensorType === 'unknown'}>
              {loading ? 'Creating...' : 'Create Sensor'}
            </button>
          </div>
        </form>
      </div>
    </div>
  );
}
