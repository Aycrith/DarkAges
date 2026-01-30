using Godot;
using System;
using System.Collections.Generic;
using DarkAges.Networking;
using static DarkAges.Entities.RemotePlayer;

namespace DarkAges.Entities
{
    /// <summary>
    /// [CLIENT_AGENT] WP-7-3 Remote Player Manager
    /// Manages all remote player entities with interpolation
    /// 
    /// Features:
    /// - Spawns/despawns entities based on server snapshots
    /// - Distributes snapshots to entities for interpolation
    /// - Hit prediction for combat
    /// - Metrics collection for quality monitoring
    /// </summary>
    public partial class RemotePlayerManager : Node3D
    {
        [Export] public PackedScene RemotePlayerScene;
        [Export] public bool ShowInterpolationDebug = false;
        
        // Active remote players: entityId -> RemotePlayer
        private Dictionary<uint, RemotePlayer> _remotePlayers = new();
        
        // Parent node for all remote players
        private Node3D _playersContainer;
        
        // Local player reference (to exclude from remote players)
        private uint _localEntityId;
        
        // Metrics tracking
        private double _lastMetricsLogTime;
        private const double MetricsLogInterval = 5.0;

        public override void _Ready()
        {
            // Create container for remote players
            _playersContainer = new Node3D { Name = "RemotePlayers" };
            AddChild(_playersContainer);
            
            // Connect to network events
            NetworkManager.Instance.SnapshotReceived += OnSnapshotReceived;
            NetworkManager.Instance.Connected += OnConnected;
            
            // Connect to game state events
            GameState.Instance.EntitySpawned += OnEntitySpawned;
            GameState.Instance.EntityDespawned += OnEntityDespawned;
            
            GD.Print("[RemotePlayerManager] WP-7-3 Entity Interpolation initialized");
        }

        public override void _ExitTree()
        {
            NetworkManager.Instance.SnapshotReceived -= OnSnapshotReceived;
            NetworkManager.Instance.Connected -= OnConnected;
            GameState.Instance.EntitySpawned -= OnEntitySpawned;
            GameState.Instance.EntityDespawned -= OnEntityDespawned;
        }
        
        public override void _Process(double delta)
        {
            // Periodic metrics logging
            double now = Time.GetTicksMsec() / 1000.0;
            if (now - _lastMetricsLogTime > MetricsLogInterval)
            {
                _lastMetricsLogTime = now;
                LogMetrics();
            }
        }
        
        private void OnConnected(uint entityId)
        {
            _localEntityId = entityId;
            GD.Print($"[RemotePlayerManager] Local entity ID: {entityId}");
        }

        private void OnSnapshotReceived(uint serverTick, byte[] data)
        {
            // Parse entity states from snapshot and distribute to remote players
            // Format matches NetworkManager.ProcessSnapshot
            
            int offset = 9;  // Skip packet_type, server_tick, last_input
            if (data.Length <= offset) return;
            
            uint entityCount = BitConverter.ToUInt32(data, offset);
            offset += 4;
            
            var currentEntities = new HashSet<uint>();
            const int EntityDataSize = 28;  // As defined in NetworkManager
            
            for (int i = 0; i < entityCount && offset + EntityDataSize <= data.Length; i++)
            {
                uint entityId = BitConverter.ToUInt32(data, offset);
                offset += 4;
                
                // Skip local player
                if (entityId == _localEntityId || entityId == GameState.Instance.LocalEntityId)
                {
                    offset += EntityDataSize - 4;
                    continue;
                }
                
                // Parse position
                float x = BitConverter.ToSingle(data, offset);
                offset += 4;
                float y = BitConverter.ToSingle(data, offset);
                offset += 4;
                float z = BitConverter.ToSingle(data, offset);
                offset += 4;
                
                // Parse velocity
                float vx = BitConverter.ToSingle(data, offset);
                offset += 4;
                float vy = BitConverter.ToSingle(data, offset);
                offset += 4;
                float vz = BitConverter.ToSingle(data, offset);
                offset += 4;
                
                // Parse health and animation
                byte health = data[offset];
                offset += 1;
                byte animState = data[offset];
                offset += 1;
                
                currentEntities.Add(entityId);
                
                // Get or create remote player
                if (!_remotePlayers.TryGetValue(entityId, out var player))
                {
                    // New entity - need to spawn
                    player = SpawnRemotePlayer(entityId, new Vector3(x, y, z));
                }
                
                // Create frame and add to interpolation buffer
                var frame = new EntityFrame
                {
                    Timestamp = Time.GetTicksMsec() / 1000.0,
                    ServerTime = serverTick / 20.0,  // Convert tick to time (20Hz snapshots)
                    Position = new Vector3(x, y, z),
                    Rotation = CalculateRotationFromVelocity(new Vector3(vx, vy, vz)),
                    Velocity = new Vector3(vx, vy, vz),
                    HealthPercent = health / 100.0f,
                    AnimationState = animState
                };
                
                player.AddSnapshot(frame);
            }
            
            // Remove entities that are no longer in snapshot
            var toRemove = new List<uint>();
            foreach (var kvp in _remotePlayers)
            {
                if (!currentEntities.Contains(kvp.Key))
                {
                    toRemove.Add(kvp.Key);
                }
            }
            
            foreach (var id in toRemove)
            {
                RemoveRemotePlayer(id);
            }
        }
        
        /// <summary>
        /// Calculate rotation from velocity (face movement direction)
        /// </summary>
        private Quaternion CalculateRotationFromVelocity(Vector3 velocity)
        {
            Vector3 horizontalVel = new Vector3(velocity.X, 0, velocity.Z);
            if (horizontalVel.LengthSquared() < 0.01f)
            {
                return Quaternion.Identity;
            }
            
            horizontalVel = horizontalVel.Normalized();
            float yaw = Mathf.Atan2(horizontalVel.X, horizontalVel.Z);
            return new Quaternion(Vector3.Up, yaw);
        }

        private void OnEntitySpawned(uint entityId, Vector3 position)
        {
            // Skip local player - only spawn remote players
            if (entityId == GameState.Instance.LocalEntityId)
                return;
            
            // Check if already spawned
            if (_remotePlayers.ContainsKey(entityId))
                return;
            
            SpawnRemotePlayer(entityId, position);
        }

        private void OnEntityDespawned(uint entityId)
        {
            RemoveRemotePlayer(entityId);
        }
        
        private RemotePlayer SpawnRemotePlayer(uint entityId, Vector3 position)
        {
            if (RemotePlayerScene == null)
            {
                GD.PrintErr("[RemotePlayerManager] RemotePlayerScene not assigned!");
                return null;
            }
            
            GD.Print($"[RemotePlayerManager] Spawning remote player {entityId}");
            
            var player = RemotePlayerScene.Instantiate<RemotePlayer>();
            player.EntityId = entityId;
            player.PlayerName = $"Player {entityId}";
            player.GlobalPosition = position;
            player.Name = $"RemotePlayer_{entityId}";
            player.ShowDebugVisualization = ShowInterpolationDebug;
            
            _playersContainer.AddChild(player);
            _remotePlayers[entityId] = player;
            
            return player;
        }
        
        private void RemoveRemotePlayer(uint entityId)
        {
            if (_remotePlayers.TryGetValue(entityId, out var player))
            {
                GD.Print($"[RemotePlayerManager] Removing remote player {entityId}");
                player.OnEntityRemoved();
                _remotePlayers.Remove(entityId);
            }
        }
        
        /// <summary>
        /// Show hit feedback on a remote player (WP-7-3 hit prediction visualization)
        /// </summary>
        public void ShowHitFeedback(uint entityId, Vector3 hitPosition, int damage)
        {
            if (_remotePlayers.TryGetValue(entityId, out var player))
            {
                player.ShowHitFeedback(hitPosition, damage);
            }
        }
        
        /// <summary>
        /// Get a remote player by entity ID
        /// </summary>
        public RemotePlayer GetPlayer(uint entityId)
        {
            return _remotePlayers.TryGetValue(entityId, out var player) ? player : null;
        }
        
        /// <summary>
        /// Get all remote players
        /// </summary>
        public IEnumerable<RemotePlayer> GetAllPlayers()
        {
            return _remotePlayers.Values;
        }
        
        /// <summary>
        /// Get entity count
        /// </summary>
        public int GetPlayerCount()
        {
            return _remotePlayers.Count;
        }
        
        /// <summary>
        /// Get interpolation metrics for all remote players
        /// </summary>
        public Dictionary<uint, InterpolationMetrics> GetAllMetrics()
        {
            var metrics = new Dictionary<uint, InterpolationMetrics>();
            foreach (var kvp in _remotePlayers)
            {
                metrics[kvp.Key] = kvp.Value.GetMetrics();
            }
            return metrics;
        }
        
        /// <summary>
        /// Get the best target for aiming (considering interpolation/extrapolation)
        /// </summary>
        public RemotePlayer GetBestTarget(Vector3 fromPosition, float maxDistance, float timeAhead = 0.1f)
        {
            RemotePlayer bestTarget = null;
            float bestScore = float.MaxValue;
            
            foreach (var player in _remotePlayers.Values)
            {
                // Use predicted position for aiming
                Vector3 predictedPos = player.GetPredictedPosition(timeAhead);
                float distance = fromPosition.DistanceTo(predictedPos);
                
                if (distance < maxDistance && distance < bestScore)
                {
                    bestScore = distance;
                    bestTarget = player;
                }
            }
            
            return bestTarget;
        }
        
        /// <summary>
        /// Clear all remote players (e.g., on disconnect)
        /// </summary>
        public void ClearAll()
        {
            foreach (var player in _remotePlayers.Values)
            {
                player.QueueFree();
            }
            _remotePlayers.Clear();
        }
        
        private void LogMetrics()
        {
            if (_remotePlayers.Count == 0) return;
            
            int extrapolatingCount = 0;
            double totalExtrapolationTime = 0;
            int totalBufferSize = 0;
            
            foreach (var player in _remotePlayers.Values)
            {
                var metrics = player.GetMetrics();
                totalBufferSize += metrics.BufferSize;
                
                if (metrics.IsExtrapolating)
                {
                    extrapolatingCount++;
                    totalExtrapolationTime += metrics.ExtrapolationTime;
                }
            }
            
            double avgBufferSize = (double)totalBufferSize / _remotePlayers.Count;
            double avgExtrapolationTime = extrapolatingCount > 0 ? 
                totalExtrapolationTime / extrapolatingCount : 0;
            
            GD.Print($"[RemotePlayerManager] Players: {_remotePlayers.Count}, " +
                     $"AvgBuffer: {avgBufferSize:F1}, Extrapolating: {extrapolatingCount}, " +
                     $"AvgExtrapTime: {avgExtrapolationTime * 1000:F0}ms");
        }
    }
}
