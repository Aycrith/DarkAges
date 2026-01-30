using Godot;
using System;
using System.Collections.Generic;
using DarkAges.Networking;

namespace DarkAges.Entities
{
    /// <summary>
    /// [CLIENT_AGENT] Manages all remote player entities
    /// Handles spawning, despawning, and snapshot distribution
    /// </summary>
    public partial class RemotePlayerManager : Node3D
    {
        [Export] public PackedScene? RemotePlayerScene;
        
        // Active remote players: entityId -> RemotePlayer
        private Dictionary<uint, RemotePlayer> remotePlayers_ = new();
        
        // Parent node for all remote players
        private Node3D? playersContainer_;

        public override void _Ready()
        {
            playersContainer_ = GetNode<Node3D>("..");  // Or create child
            
            // Connect to network events
            NetworkManager.Instance.SnapshotReceived += OnSnapshotReceived;
            
            // Connect to game state events
            GameState.Instance.EntitySpawned += OnEntitySpawned;
            GameState.Instance.EntityDespawned += OnEntityDespawned;
        }

        public override void _ExitTree()
        {
            NetworkManager.Instance.SnapshotReceived -= OnSnapshotReceived;
            GameState.Instance.EntitySpawned -= OnEntitySpawned;
            GameState.Instance.EntityDespawned -= OnEntityDespawned;
        }

        private void OnSnapshotReceived(uint serverTick, byte[] data)
        {
            // Parse snapshot (would use FlatBuffers in production)
            // For now, extract entity states from GameState
            
            foreach (var kvp in GameState.Instance.Entities)
            {
                uint entityId = kvp.Key;
                var entityData = kvp.Value;
                
                // Skip local player
                if (entityId == GameState.Instance.LocalEntityId)
                    continue;
                
                // Update or spawn remote player
                if (remotePlayers_.TryGetValue(entityId, out RemotePlayer? player))
                {
                    // Update existing player
                    player.OnSnapshotReceived(
                        serverTick,
                        entityData.Position,
                        entityData.Velocity,
                        entityData.Rotation,
                        entityData.HealthPercent,
                        entityData.AnimState
                    );
                }
                else
                {
                    // New entity - will be handled by OnEntitySpawned
                }
            }
        }

        private void OnEntitySpawned(uint entityId, Vector3 position)
        {
            // Skip local player
            if (entityId == GameState.Instance.LocalEntityId)
                return;
            
            GD.Print($"[RemotePlayerManager] Spawning entity {entityId}");
            
            if (RemotePlayerScene == null)
            {
                GD.PrintErr("[RemotePlayerManager] RemotePlayerScene not set!");
                return;
            }
            
            // Instantiate remote player
            var remotePlayer = RemotePlayerScene.Instantiate<RemotePlayer>();
            remotePlayer.EntityId = entityId;
            remotePlayer.GlobalPosition = position;
            remotePlayer.Name = $"RemotePlayer_{entityId}";
            
            playersContainer_?.AddChild(remotePlayer);
            remotePlayers_[entityId] = remotePlayer;
        }

        private void OnEntityDespawned(uint entityId)
        {
            if (remotePlayers_.TryGetValue(entityId, out RemotePlayer? player))
            {
                GD.Print($"[RemotePlayerManager] Despawning entity {entityId}");
                player.OnEntityRemoved();
                remotePlayers_.Remove(entityId);
            }
        }

        public RemotePlayer? GetRemotePlayer(uint entityId)
        {
            return remotePlayers_.TryGetValue(entityId, out var player) ? player : null;
        }

        public void ClearAll()
        {
            foreach (var player in remotePlayers_.Values)
            {
                player.QueueFree();
            }
            remotePlayers_.Clear();
        }
    }
}
