using Godot;
using System;
using System.Collections.Generic;

namespace DarkAges
{
    /// <summary>
    /// [CLIENT_AGENT] Global game state singleton
    /// Manages connection state, player data, entity registry, and debug metrics
    /// </summary>
    public partial class GameState : Node
    {
        public static GameState Instance { get; private set; } = null!;
        
        // Connection state
        public enum ConnectionState
        {
            Disconnected,
            Connecting,
            Connected,
            Error
        }
        
        [Signal]
        public delegate void ConnectionStateChangedEventHandler(ConnectionState newState);
        
        [Signal]
        public delegate void EntitySpawnedEventHandler(uint entityId, Vector3 position);
        
        [Signal]
        public delegate void EntityDespawnedEventHandler(uint entityId);
        
        [Signal]
        public delegate void PredictionErrorEventHandler(float error);
        
        [Signal]
        public delegate void ReconciliationEventHandler(int inputCount, float error);
        
        public ConnectionState CurrentConnectionState { get; private set; } = ConnectionState.Disconnected;
        
        // Local player
        public uint LocalPlayerId { get; set; } = 0;
        public uint LocalEntityId { get; set; } = 0;
        
        // Known entities (entity_id -> EntityData)
        public Dictionary<uint, EntityData> Entities { get; } = new();
        
        // Server time synchronization
        public uint ServerTick { get; set; } = 0;
        public uint ServerTimeOffsetMs { get; set; } = 0;
        
        // Metrics
        public uint LastRttMs { get; set; } = 0;
        public float PacketLoss { get; set; } = 0.0f;
        
        // Prediction debug metrics (updated by PredictedPlayer)
        public float PredictionError { get; set; } = 0.0f;
        public int InputBufferSize { get; set; } = 0;
        public uint LastProcessedInput { get; set; } = 0;
        public int ReconciliationCount { get; set; } = 0;

        public override void _EnterTree()
        {
            Instance = this;
        }

        public override void _Ready()
        {
            GD.Print("[GameState] Initialized");
        }

        public void SetConnectionState(ConnectionState state)
        {
            if (CurrentConnectionState != state)
            {
                CurrentConnectionState = state;
                EmitSignal(SignalName.ConnectionStateChanged, (int)state);
                GD.Print($"[GameState] Connection state: {state}");
            }
        }

        public void RegisterEntity(uint entityId, EntityData data)
        {
            Entities[entityId] = data;
            EmitSignal(SignalName.EntitySpawned, entityId, data.Position);
        }

        public void UnregisterEntity(uint entityId)
        {
            if (Entities.Remove(entityId))
            {
                EmitSignal(SignalName.EntityDespawned, entityId);
            }
        }

        public EntityData? GetEntity(uint entityId)
        {
            return Entities.TryGetValue(entityId, out var data) ? data : null;
        }
        
        /// <summary>
        /// Get a formatted string of current prediction debug stats
        /// </summary>
        public string GetPredictionStats()
        {
            return $"Error: {PredictionError:F3}m | Buffer: {InputBufferSize} | Reconciliations: {ReconciliationCount}";
        }
    }

    /// <summary>
    /// Represents a networked entity's state
    /// </summary>
    public class EntityData
    {
        public uint Id { get; set; }
        public string Name { get; set; } = $"Player_{Id}";
        public byte Type { get; set; }
        public Vector3 Position { get; set; }
        public Vector3 Velocity { get; set; }
        public Quaternion Rotation { get; set; }
        public byte HealthPercent { get; set; }
        public byte AnimState { get; set; }
        public uint LastUpdateTick { get; set; }
        
        // Interpolation data
        public Vector3 TargetPosition { get; set; }
        public Vector3 LastPosition { get; set; }
        public double InterpAlpha { get; set; } = 0.0;
    }
}
