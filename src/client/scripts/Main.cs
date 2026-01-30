using Godot;
using System;
using DarkAges.Networking;
using DarkAges.Entities;

namespace DarkAges
{
    /// <summary>
    /// [CLIENT_AGENT] Main game scene controller
    /// Manages world loading, player spawning, and entity interpolation
    /// </summary>
    public partial class Main : Node3D
    {
        [Export] public PackedScene? PlayerScene;
        [Export] public PackedScene? RemotePlayerScene;
        
        private Node3D? _playersContainer;
        private PredictedPlayer? _localPlayer;
        private RemotePlayerManager? _remotePlayerManager;

        public override void _Ready()
        {
            GD.Print("[Main] Game starting...");
            
            _playersContainer = GetNode<Node3D>("Players");
            
            // Setup RemotePlayerManager for handling remote players
            _remotePlayerManager = new RemotePlayerManager();
            _remotePlayerManager.RemotePlayerScene = RemotePlayerScene;
            _playersContainer.AddChild(_remotePlayerManager);
            
            // Connect to network events
            if (NetworkManager.Instance != null)
            {
                NetworkManager.Instance.SnapshotReceived += OnSnapshotReceived;
                NetworkManager.Instance.ConnectionResult += OnConnectionResult;
            }
            
            // Connect to game state events
            GameState.Instance.EntitySpawned += OnEntitySpawned;
            GameState.Instance.EntityDespawned += OnEntityDespawned;
            
            // Find local player
            _localPlayer = GetNode<PredictedPlayer>("Players/Player");
        }

        public override void _ExitTree()
        {
            if (NetworkManager.Instance != null)
            {
                NetworkManager.Instance.SnapshotReceived -= OnSnapshotReceived;
                NetworkManager.Instance.ConnectionResult -= OnConnectionResult;
            }
            
            GameState.Instance.EntitySpawned -= OnEntitySpawned;
            GameState.Instance.EntityDespawned -= OnEntityDespawned;
            
            _remotePlayerManager?.ClearAll();
        }

        public override void _Process(double delta)
        {
            // Remote entity interpolation is now handled by RemotePlayerManager
            // and individual RemotePlayer components
        }

        private void OnConnectionResult(bool success, string error)
        {
            if (success)
            {
                GD.Print("[Main] Connected to server!");
            }
            else
            {
                GD.PrintErr($"[Main] Connection failed: {error}");
            }
        }

        private void OnSnapshotReceived(uint serverTick, byte[] data)
        {
            // Enhanced snapshot processing is now in NetworkManager
            // RemotePlayerManager receives the signal and distributes to entities
            // This handler can be used for additional game logic if needed
        }

        private void OnEntitySpawned(uint entityId, Vector3 position)
        {
            // Local player is handled separately
            if (entityId == GameState.Instance.LocalEntityId)
            {
                GD.Print($"[Main] Local player entity confirmed: {entityId}");
                return;
            }
            
            GD.Print($"[Main] Entity spawned event: {entityId} at {position}");
            // Remote player instantiation is handled by RemotePlayerManager
        }

        private void OnEntityDespawned(uint entityId)
        {
            GD.Print($"[Main] Entity despawned event: {entityId}");
            // Remote player cleanup is handled by RemotePlayerManager
        }

        public void ConnectToServer(string address, int port)
        {
            NetworkManager.Instance?.Connect(address, port);
        }
    }
}
