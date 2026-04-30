import { useEffect, useMemo, useState } from 'react';
import { getHealth, getAllNodesLatest, getAllSensors, deleteSensor, getRealtimeUrl, type NodeLatest, type Telemetry, type SensorInfo } from '../services/api';
import { DoorSensor } from '../components/sensors/DoorSensor';
import { ThermostatSensor } from '../components/sensors/ThermostatSensor';
import { PetSensor } from '../components/sensors/PetSensor';
import { MotionSensor } from '../components/sensors/MotionSensor';
import { AddSensorModal } from '../components/AddSensorModal';
import { DeleteConfirmModal } from '../components/DeleteConfirmModal';
import './Dashboard.css';

export function Dashboard() {
  const [nodes, setNodes] = useState<NodeLatest[]>([]);
  const [allSensors, setAllSensors] = useState<SensorInfo[]>([]);
  const [health, setHealth] = useState<{ status: string } | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [showAddModal, setShowAddModal] = useState(false);
  const [showDeleteConfirm, setShowDeleteConfirm] = useState(false);
  const [sensorToDelete, setSensorToDelete] = useState<{ id: number; name: string } | null>(null);
  const [deleteLoading, setDeleteLoading] = useState(false);

  useEffect(() => {
    let isMounted = true;

    const fetchData = async (showLoading = false) => {
      try {
        if (showLoading) {
          setLoading(true);
        }

        const [nodesData, sensorsData, healthData] = await Promise.all([
          getAllNodesLatest(),
          getAllSensors(),
          getHealth(),
        ]);

        if (!isMounted) return;

        setNodes(nodesData);
        setAllSensors(sensorsData);
        setHealth(healthData);
        setError(null);
      } catch (err) {
        if (!isMounted) return;

        setError('Failed to load dashboard data');
        console.error(err);
      } finally {
        if (isMounted) {
          setLoading(false);
        }
      }
    };

    fetchData(true);

    const socket = new WebSocket(getRealtimeUrl());
    socket.onmessage = () => {
      fetchData(false);
    };
    socket.onerror = (err) => {
      console.error('Dashboard WebSocket error', err);
    };

    return () => {
      isMounted = false;
      socket.close();
    };
  }, []);

  const summary = useMemo(() => {
    // Map holds arrays of Telemetry objects for each sensor
    const sensorsByType = new Map<string, Map<number, Telemetry[] | null>>();

    // First, add all sensors (with null telemetry if no data)
    allSensors.forEach((sensor) => {
      if (!sensorsByType.has(sensor.sensorType)) {
        sensorsByType.set(sensor.sensorType, new Map());
      }
      
      // Initialize with null, will be replaced if telemetry exists
      sensorsByType.get(sensor.sensorType)!.set(sensor.sensorId, null);
    });

    // Then, add/override with actual telemetry data
    nodes.forEach((node) => {
      if (!node.latestTelemetry) return;

      node.latestTelemetry.forEach((latestItem) => {
        if (!sensorsByType.has(latestItem.sensorType)) {
          sensorsByType.set(latestItem.sensorType, new Map());
        }
        
        if (!sensorsByType.get(latestItem.sensorType)!.has(latestItem.sensorId)) {
          sensorsByType.get(latestItem.sensorType)!.set(latestItem.sensorId, []);
        }
        
        // Add all readings for this sensor
        const existing = sensorsByType.get(latestItem.sensorType)!.get(latestItem.sensorId);
        if (Array.isArray(existing)) {
          existing.push(latestItem);
        } else {
          sensorsByType.get(latestItem.sensorType)!.set(latestItem.sensorId, [latestItem]);
        }
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
  }, [nodes, allSensors]);

  const handleDeleteClick = (sensorId: number, sensorName: string) => {
    setSensorToDelete({ id: sensorId, name: sensorName });
    setShowDeleteConfirm(true);
  };

  const handleConfirmDelete = async () => {
    if (!sensorToDelete) return;
    setDeleteLoading(true);
    try {
      await deleteSensor(sensorToDelete.id);
      // Refresh data after deletion
      const [nodesData, sensorsData, healthData] = await Promise.all([
        getAllNodesLatest(),
        getAllSensors(),
        getHealth(),
      ]);
      setNodes(nodesData);
      setAllSensors(sensorsData);
      setHealth(healthData);
      setShowDeleteConfirm(false);
      setSensorToDelete(null);
    } catch (err) {
      console.error('Failed to delete sensor:', err);
      setError('Failed to delete sensor');
    } finally {
      setDeleteLoading(false);
    }
  };

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
            {sensorType === 'motion' && '💡 Motion Sensors'}
            {sensorType === 'pet' && '🐾 Pet Sensors'}
            {!['door', 'thermostat', 'motion', 'pet'].includes(sensorType) && `📦 ${sensorType}`}
          </h2>

          {sensorsMap.size > 0 ? (
            <div className="sensors-grid">
              {Array.from(sensorsMap.entries()).map(([sensorId, sensorData]) => {
                // Get sensor info from allSensors
                const sensorInfo = allSensors.find(s => s.sensorId === sensorId);
                const sensorName = sensorInfo?.sensorName || `Sensor #${sensorId}`;
                const sensorNodeId = sensorData ? sensorData[0].nodeId : sensorInfo?.nodeId;
                const node = nodes.find(n => n.nodeId === sensorNodeId);
                const nodeName = node?.nodeName || sensorNodeId || 'Unknown Node';
                
                // Handle sensors with no telemetry
                if (!sensorData || sensorData.length === 0) {
                  return (
                    <div key={`${sensorType}-${sensorId}`}>
                      <div className="sensor-card-no-data">
                        <div className="card-header">
                          <h3>{sensorName}</h3>
                          <div className="header-actions">
                            <span className="node-badge">{nodeName}</span>
                            {sensorInfo && (
                              <button 
                                className="delete-button" 
                                onClick={() => handleDeleteClick(sensorId, sensorName)}
                                title="Delete sensor"
                              >
                                ✕
                              </button>
                            )}
                          </div>
                        </div>
                        <div className="no-telemetry-message">
                          <div className="icon">📡</div>
                          <p>No telemetry data yet</p>
                          <small>Waiting for first sensor reading...</small>
                        </div>
                      </div>
                    </div>
                  );
                }
                
                return (
                  <div key={`${sensorType}-${sensorId}`}>
                    {sensorType === 'door' && (
                      <DoorSensor
                        sensorName={sensorName}
                        nodeName={nodeName}
                        sensorId={sensorId}
                        data={sensorData}
                        onDelete={handleDeleteClick}
                      />
                    )}
                    {sensorType === 'thermostat' && (
                      <ThermostatSensor
                        sensorName={sensorName}
                        nodeName={nodeName}
                        sensorId={sensorId}
                        data={sensorData}
                        onDelete={handleDeleteClick}
                      />
                    )}
                    {sensorType === 'pet' && (
                      <PetSensor
                        sensorName={sensorName}
                        nodeId={sensorNodeId ?? ""}
                        nodeName={nodeName}
                        sensorId={sensorId}
                        latestData={sensorData[0]}
                        onDelete={handleDeleteClick}
                      />
                    )}
                    {sensorType === 'motion' && (
                      <MotionSensor
                        sensorName={sensorName}
                        nodeName={nodeName}
                        sensorId={sensorId}
                        data={sensorData}
                        onDelete={handleDeleteClick}
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

      {/* Floating Add Button */}
      <button className="fab-button" onClick={() => setShowAddModal(true)} title="Add new sensor">
        ➕
      </button>

      {/* Modals */}
      <AddSensorModal
        isOpen={showAddModal}
        onClose={() => setShowAddModal(false)}
        onSensorAdded={() => {
          setShowAddModal(false);
          // Refresh dashboard
          const fetchData = async () => {
            const [nodesData, sensorsData, healthData] = await Promise.all([
              getAllNodesLatest(),
              getAllSensors(),
              getHealth(),
            ]);
            setNodes(nodesData);
            setAllSensors(sensorsData);
            setHealth(healthData);
          };
          fetchData();
        }}
      />

      <DeleteConfirmModal
        isOpen={showDeleteConfirm}
        onClose={() => {
          setShowDeleteConfirm(false);
          setSensorToDelete(null);
        }}
        onConfirm={handleConfirmDelete}
        sensorName={sensorToDelete?.name || ''}
        loading={deleteLoading}
      />
    </div>
  );
}
