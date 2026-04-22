import { useEffect, useMemo, useState } from 'react';
import { getHealth, getAllNodesLatest, type NodeLatest, type Telemetry } from '../services/api';
import { DoorSensor } from '../components/sensors/DoorSensor';
import { ThermostatSensor } from '../components/sensors/ThermostatSensor';
import { PetSensor } from '../components/sensors/PetSensor';
import './Dashboard.css';

export function Dashboard() {
  const [nodes, setNodes] = useState<NodeLatest[]>([]);
  const [health, setHealth] = useState<{ status: string } | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchData = async () => {
      try {
        setLoading(true);
        // STRIPPED: We now only fetch the exact data we need.
        const [nodesData, healthData] = await Promise.all([
          getAllNodesLatest(),
          getHealth(),
        ]);
        setNodes(nodesData);
        setHealth(healthData);
      } catch (err) {
        setError('Failed to load dashboard data');
        console.error(err);
      } finally {
        setLoading(false);
      }
    };

    fetchData();
  }, []);

  const summary = useMemo(() => {
    // Map strictly holds the single latest Telemetry object
    const sensorsByType = new Map<string, Map<number, Telemetry>>();

    nodes.forEach((node) => {
      if (!node.latestTelemetry) return;

      node.latestTelemetry.forEach((latestItem) => {
        if (!sensorsByType.has(latestItem.sensorType)) {
          sensorsByType.set(latestItem.sensorType, new Map());
        }
        
        // Store ONLY the absolute latest reading
        sensorsByType.get(latestItem.sensorType)!.set(latestItem.sensorId, latestItem);
      });
    });

    return {
      totalNodes: nodes.length,
      totalSensors: sensorsByType.size > 0 
        ? Array.from(sensorsByType.values()).reduce((sum, map) => sum + map.size, 0)
        : 0,
      sensorsByType,
      activeNodes: nodes.filter(n => n.isActive).length,
    };
  }, [nodes]); // Dependency array is now super clean

  if (loading) {
    return <div className="dashboard-loading">Loading dashboard...</div>;
  }

  if (error) {
    return <div className="dashboard-error">{error}</div>;
  }

  return (
    <div className="dashboard">
      {/* Header */}
      <div className="dashboard-header">
        <div className="header-content">
          <div className="header-title">
            <h1>Dashboard</h1>
            <p className="subtitle">Real-time sensor monitoring</p>
          </div>
          <div className={`health-status ${health?.status === 'ok' ? 'healthy' : 'unhealthy'}`}>
            <div className="status-dot"></div>
            <span>{health?.status === 'ok' ? 'System Healthy' : 'System Offline'}</span>
          </div>
        </div>
      </div>

      {/* Stats Overview */}
      <div className="stats-overview">
        <div className="stat-card">
          <div className="stat-icon">🏠</div>
          <div className="stat-content">
            <div className="stat-value">{summary.totalNodes}</div>
            <div className="stat-label">Total Nodes</div>
          </div>
        </div>
        <div className="stat-card">
          <div className="stat-icon">✓</div>
          <div className="stat-content">
            <div className="stat-value">{summary.activeNodes}</div>
            <div className="stat-label">Active Nodes</div>
          </div>
        </div>
        <div className="stat-card">
          <div className="stat-icon">📊</div>
          <div className="stat-content">
            <div className="stat-value">{summary.totalSensors}</div>
            <div className="stat-label">Connected Sensors</div>
          </div>
        </div>
      </div>

      {/* Sensors by Type */}
      {Array.from(summary.sensorsByType.entries()).map(([sensorType, sensorsMap]) => (
        <div key={sensorType} className="sensor-type-section">
          <h2 className="section-title">
            {sensorType === 'door' && '🚪 Doors'}
            {sensorType === 'thermostat' && '🌡️ Thermostats'}
            {sensorType === 'pet' && '🐾 Pet Sensors'}
            {!['door', 'thermostat', 'pet'].includes(sensorType) && `📦 ${sensorType}`}
          </h2>

          {sensorsMap.size > 0 ? (
            <div className="sensors-grid">
              {Array.from(sensorsMap.entries()).map(([sensorId, latestData]) => {
                
                const sensorNodeId = latestData.nodeId;
                const sensorName = latestData.sensorName || `Sensor #${sensorId}`;
                
                // Assuming your components map over an array, we pass it an array of exactly length 1
                const strictLatestData = [latestData];

                return (
                  <div key={`${sensorType}-${sensorId}`}>
                    {sensorType === 'door' && (
                      <DoorSensor
                        sensorName={sensorName}
                        nodeId={sensorNodeId}
                        data={strictLatestData}
                      />
                    )}
                    {sensorType === 'thermostat' && (
                      <ThermostatSensor
                        sensorName={sensorName}
                        nodeId={sensorNodeId}
                        data={strictLatestData}
                      />
                    )}
                    {sensorType === 'pet' && (
                      <PetSensor
                        sensorName={sensorName}
                        nodeId={sensorNodeId}
                        sensorId={sensorId}       // <-- Added to allow history fetching
                        latestData={latestData}   // <-- Passed just the single object
                      />
                    )}
                  </div>
                );
              })}
            </div>
          ) : (
            <div className="no-data">No {sensorType} sensors available</div>
          )}
        </div>
      ))}

      {/* Empty State */}
      {summary.sensorsByType.size === 0 && (
        <div className="empty-state">
          <div className="empty-icon">📭</div>
          <h3>No Sensors Found</h3>
          <p>Start by registering some sensors and sending telemetry data</p>
        </div>
      )}
    </div>
  );
}