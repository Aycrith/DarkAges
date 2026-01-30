using Godot;
using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace DarkAges.Networking
{
    /// <summary>
    /// [NETWORK_AGENT] Client network manager
    /// Handles UDP socket, input sending at fixed rate, and server packet processing
    /// 
    /// Design:
    /// - Sends ClientInput packets at 60Hz with sequence numbers
    /// - Processes ServerCorrection packets for reconciliation
    /// - Processes Snapshot packets for entity sync
    /// - Maintains RTT measurement for latency display
    /// </summary>
    public partial class NetworkManager : Node
    {
        public static NetworkManager Instance { get; private set; } = null!;
        
        [Export] public string ServerAddress = "127.0.0.1";
        [Export] public int ServerPort = 7777;
        [Export] public float InputSendRate = 60.0f;  // Hz - must match physics tick
        
        // Events
        [Signal]
        public delegate void SnapshotReceivedEventHandler(uint serverTick, byte[] data);
        
        [Signal]
        public delegate void ServerCorrectionEventHandler(byte[] correctionData);
        
        [Signal]
        public delegate void ConnectionResultEventHandler(bool success, string error);
        
        [Signal]
        public delegate void EntityStateReceivedEventHandler(uint entityId, Vector3 position, Vector3 velocity);
        
        [Signal]
        public delegate void CombatEventReceivedEventHandler(uint eventType, byte[] data);
        
        // Socket
        private UdpClient? _udpClient;
        private IPEndPoint? _serverEndPoint;
        private Thread? _receiveThread;
        private bool _running = false;
        
        // Input tracking
        private uint _inputSequence = 1;  // Monotonically increasing sequence
        private double _inputAccumulator = 0.0;
        private Queue<InputState> _inputQueue = new();  // Inputs waiting to be sent
        
        // Reference to local player for correction handling
        private PredictedPlayer? _predictedPlayer;
        
        // Ping tracking
        private uint _lastPingSent = 0;
        private uint _pingSentTime = 0;
        private uint _currentRtt = 0;
        
        // Packet type constants - must match server
        private const byte PACKET_CLIENT_INPUT = 1;
        private const byte PACKET_SNAPSHOT = 2;
        private const byte PACKET_EVENT = 3;
        private const byte PACKET_PING = 4;
        private const byte PACKET_PONG = 5;
        private const byte PACKET_CONNECTION_REQUEST = 6;
        private const byte PACKET_CONNECTION_RESPONSE = 7;
        private const byte PACKET_SERVER_CORRECTION = 8;
        private const byte PACKET_RESPAWN_REQUEST = 9;

        public override void _EnterTree()
        {
            Instance = this;
        }

        public override void _Ready()
        {
            GD.Print("[NetworkManager] Initialized");
        }

        public override void _ExitTree()
        {
            Disconnect();
        }

        /// <summary>
        /// Set the local predicted player reference for correction handling
        /// </summary>
        public void SetPredictedPlayer(PredictedPlayer player)
        {
            _predictedPlayer = player;
            GD.Print("[NetworkManager] Predicted player reference set");
        }

        /// <summary>
        /// Connect to game server
        /// </summary>
        public void Connect(string address, int port)
        {
            ServerAddress = address;
            ServerPort = port;
            
            GD.Print($"[NetworkManager] Connecting to {address}:{port}...");
            GameState.Instance.SetConnectionState(GameState.ConnectionState.Connecting);
            
            try
            {
                _serverEndPoint = new IPEndPoint(IPAddress.Parse(address), port);
                _udpClient = new UdpClient();
                _udpClient.Connect(_serverEndPoint);
                
                _running = true;
                _receiveThread = new Thread(ReceiveLoop);
                _receiveThread.Start();
                
                // Send connection request
                SendConnectionRequest();
                
                // Wait for response (simplified - would use async/await in production)
                var timer = new Timer();
                timer.WaitTime = 5.0f;
                timer.OneShot = true;
                timer.Timeout += () =>
                {
                    if (GameState.Instance.CurrentConnectionState != GameState.ConnectionState.Connected)
                    {
                        EmitSignal(SignalName.ConnectionResult, false, "Connection timeout");
                        Disconnect();
                    }
                };
                timer.Start();
            }
            catch (Exception ex)
            {
                GD.PrintErr($"[NetworkManager] Connection failed: {ex.Message}");
                EmitSignal(SignalName.ConnectionResult, false, ex.Message);
                GameState.Instance.SetConnectionState(GameState.ConnectionState.Error);
            }
        }

        public void Disconnect()
        {
            _running = false;
            
            _udpClient?.Close();
            _udpClient?.Dispose();
            _udpClient = null;
            
            if (_receiveThread != null && _receiveThread.IsAlive)
            {
                _receiveThread.Join(1000);
            }
            
            GameState.Instance.SetConnectionState(GameState.ConnectionState.Disconnected);
            GD.Print("[NetworkManager] Disconnected");
        }

        public override void _PhysicsProcess(double delta)
        {
            if (GameState.Instance.CurrentConnectionState != GameState.ConnectionState.Connected)
                return;
            
            // Gather current input for sending
            var input = GatherInput();
            
            // Queue input for sending
            input.Sequence = _inputSequence++;
            input.Timestamp = (uint)Time.GetTicksMsec();
            _inputQueue.Enqueue(input);
            
            // Send inputs at configured rate
            _inputAccumulator += delta;
            float inputInterval = 1.0f / InputSendRate;
            
            while (_inputAccumulator >= inputInterval)
            {
                _inputAccumulator -= inputInterval;
                SendPendingInputs();
            }
            
            // Periodic ping
            _lastPingSent += (uint)(delta * 1000);
            if (_lastPingSent > 1000)  // Ping every second
            {
                SendPing();
                _lastPingSent = 0;
            }
        }

        /// <summary>
        /// Gather current input state from player
        /// </summary>
        private InputState GatherInput()
        {
            // Get camera rotation from viewport
            float yaw = 0;
            float pitch = 0;
            var camera = GetViewport().GetCamera3D();
            if (camera != null)
            {
                yaw = camera.Rotation.Y;
                pitch = camera.Rotation.X;
            }
            
            return new InputState
            {
                Forward = Input.IsActionPressed("move_forward"),
                Backward = Input.IsActionPressed("move_backward"),
                Left = Input.IsActionPressed("move_left"),
                Right = Input.IsActionPressed("move_right"),
                Jump = Input.IsActionPressed("jump"),
                Sprint = Input.IsActionPressed("sprint"),
                Attack = Input.IsActionPressed("attack"),
                Block = Input.IsActionPressed("block"),
                Yaw = yaw,
                Pitch = pitch,
                Sequence = 0,  // Will be set when queued
                Timestamp = 0
            };
        }

        /// <summary>
        /// Send all pending inputs to server
        /// Can batch multiple inputs if needed for bandwidth optimization
        /// </summary>
        private void SendPendingInputs()
        {
            if (_udpClient == null || _serverEndPoint == null) return;
            
            // Send most recent input (can be extended to batch multiple)
            if (_inputQueue.Count > 0)
            {
                var input = _inputQueue.Dequeue();
                var data = SerializeInput(input);
                
                try
                {
                    _udpClient.Send(data, data.Length);
                    GD.PrintVerbose($"[NetworkManager] Sent input seq={input.Sequence}");
                }
                catch (Exception ex)
                {
                    GD.PrintErr($"[NetworkManager] Send failed: {ex.Message}");
                }
            }
            
            // Clear old inputs if queue grows too large (shouldn't happen at 60Hz)
            while (_inputQueue.Count > 10)
            {
                var dropped = _inputQueue.Dequeue();
                GD.PrintVerbose($"[NetworkManager] Dropping old input seq={dropped.Sequence}");
            }
        }

        /// <summary>
        /// Serialize input to FlatBuffer format
        /// </summary>
        private byte[] SerializeInput(InputState input)
        {
            // FlatBuffer format: [packet_type:1][sequence:4][timestamp:4][input_flags:1][yaw:2][pitch:2][target:4]
            var data = new byte[18];
            
            data[0] = PACKET_CLIENT_INPUT;
            BitConverter.GetBytes(input.Sequence).CopyTo(data, 1);
            BitConverter.GetBytes(input.Timestamp).CopyTo(data, 5);
            
            // Bit-packed input flags
            byte flags = 0;
            if (input.Forward) flags |= 0x01;
            if (input.Backward) flags |= 0x02;
            if (input.Left) flags |= 0x04;
            if (input.Right) flags |= 0x08;
            if (input.Jump) flags |= 0x10;
            if (input.Attack) flags |= 0x20;
            if (input.Block) flags |= 0x40;
            if (input.Sprint) flags |= 0x80;
            data[9] = flags;
            
            // Quantized rotation (yaw/pitch in radians * 10000)
            short yawQuantized = (short)(input.Yaw * 10000);
            short pitchQuantized = (short)(input.Pitch * 10000);
            BitConverter.GetBytes(yawQuantized).CopyTo(data, 10);
            BitConverter.GetBytes(pitchQuantized).CopyTo(data, 12);
            
            // Target entity (0 for now - no targeting)
            BitConverter.GetBytes((uint)0).CopyTo(data, 14);
            
            return data;
        }

        private void SendConnectionRequest()
        {
            if (_udpClient == null) return;
            
            GD.Print("[NetworkManager] Sending connection request...");
            
            // Simple connection request: [packet_type:1][version:4][player_id:4]
            var data = new byte[9];
            data[0] = PACKET_CONNECTION_REQUEST;
            BitConverter.GetBytes((uint)1).CopyTo(data, 1);  // Protocol version
            BitConverter.GetBytes(GameState.Instance.LocalPlayerId).CopyTo(data, 5);
            
            try
            {
                _udpClient.Send(data, data.Length);
            }
            catch (Exception ex)
            {
                GD.PrintErr($"[NetworkManager] Connection request failed: {ex.Message}");
            }
        }

        private void SendPing()
        {
            if (_udpClient == null) return;
            
            _pingSentTime = (uint)Time.GetTicksMsec();
            
            var data = new byte[5];
            data[0] = PACKET_PING;
            BitConverter.GetBytes(_pingSentTime).CopyTo(data, 1);
            
            try
            {
                _udpClient.Send(data, data.Length);
            }
            catch (Exception ex)
            {
                GD.PrintErr($"[NetworkManager] Ping failed: {ex.Message}");
            }
        }

        /// <summary>
        /// Background thread for receiving packets
        /// </summary>
        private void ReceiveLoop()
        {
            while (_running && _udpClient != null)
            {
                try
                {
                    IPEndPoint remote = new IPEndPoint(IPAddress.Any, 0);
                    byte[] data = _udpClient.Receive(ref remote);
                    
                    // Process on main thread via CallDeferred
                    CallDeferred(nameof(ProcessPacket), data);
                }
                catch (SocketException)
                {
                    // Socket closed or error
                    break;
                }
                catch (Exception ex)
                {
                    GD.PrintErr($"[NetworkManager] Receive error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Process received packet (called on main thread)
        /// </summary>
        private void ProcessPacket(byte[] data)
        {
            if (data.Length < 1) return;
            
            byte packetType = data[0];
            
            switch (packetType)
            {
                case PACKET_SNAPSHOT:
                    ProcessSnapshot(data);
                    break;
                case PACKET_SERVER_CORRECTION:
                    ProcessServerCorrection(data);
                    break;
                case PACKET_EVENT:
                    ProcessEvent(data);
                    break;
                case PACKET_PONG:
                    ProcessPong(data);
                    break;
                case PACKET_CONNECTION_RESPONSE:
                    ProcessConnectionResponse(data);
                    break;
                default:
                    GD.PrintVerbose($"[NetworkManager] Unknown packet type: {packetType}");
                    break;
            }
        }

        /// <summary>
        /// Process server snapshot containing entity states
        /// Enhanced for Phase 2: Parses entity states from snapshot data
        /// </summary>
        private void ProcessSnapshot(byte[] data)
        {
            if (data.Length < 5) return;
            
            // Parse server tick from packet
            uint serverTick = BitConverter.ToUInt32(data, 1);
            GameState.Instance.ServerTick = serverTick;
            
            // Parse last processed input for reconciliation
            if (data.Length >= 9)
            {
                uint lastProcessedInput = BitConverter.ToUInt32(data, 5);
                GameState.Instance.LastProcessedInput = lastProcessedInput;
            }
            
            // [PHASE 2C] Parse entity states from snapshot
            // Format: [packet_type:1][server_tick:4][last_input:4][entity_count:4][entity_data...]
            int offset = 9;
            if (data.Length > offset)
            {
                uint entityCount = BitConverter.ToUInt32(data, offset);
                offset += 4;
                
                var currentEntities = new HashSet<uint>();
                
                // Each entity: [entity_id:4][pos_x:4][pos_y:4][pos_z:4][vel_x:4][vel_y:4][vel_z:4][health:1][anim:1]
                const int ENTITY_DATA_SIZE = 28;
                
                for (int i = 0; i < entityCount && offset + ENTITY_DATA_SIZE <= data.Length; i++)
                {
                    uint entityId = BitConverter.ToUInt32(data, offset);
                    offset += 4;
                    
                    float x = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    float y = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    float z = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    
                    float vx = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    float vy = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    float vz = BitConverter.ToSingle(data, offset);
                    offset += 4;
                    
                    byte health = data[offset];
                    offset += 1;
                    
                    byte animState = data[offset];
                    offset += 1;
                    
                    currentEntities.Add(entityId);
                    
                    // Update or create entity
                    var entityData = GameState.Instance.GetEntity(entityId);
                    if (entityData == null)
                    {
                        // New entity - register with GameState
                        entityData = new EntityData
                        {
                            Id = entityId,
                            Position = new Vector3(x, y, z),
                            Velocity = new Vector3(vx, vy, vz),
                            TargetPosition = new Vector3(x, y, z),
                            LastPosition = new Vector3(x, y, z),
                            HealthPercent = health,
                            AnimState = animState,
                            LastUpdateTick = serverTick
                        };
                        GameState.Instance.RegisterEntity(entityId, entityData);
                    }
                    else
                    {
                        // Update existing entity - set targets for interpolation
                        entityData.LastPosition = entityData.Position;
                        entityData.Position = new Vector3(x, y, z);
                        entityData.TargetPosition = new Vector3(x, y, z);
                        entityData.Velocity = new Vector3(vx, vy, vz);
                        entityData.HealthPercent = health;
                        entityData.AnimState = animState;
                        entityData.LastUpdateTick = serverTick;
                    }
                    
                    // Emit signal for individual entity update (for debug/monitoring)
                    EmitSignal(SignalName.EntityStateReceived, entityId, 
                        new Vector3(x, y, z), new Vector3(vx, vy, vz));
                }
                
                // Check for removed entities (entities no longer in snapshot)
                var toRemove = new List<uint>();
                foreach (uint id in GameState.Instance.Entities.Keys)
                {
                    if (!currentEntities.Contains(id) && id != GameState.Instance.LocalEntityId)
                    {
                        toRemove.Add(id);
                    }
                }
                foreach (uint id in toRemove)
                {
                    GameState.Instance.UnregisterEntity(id);
                }
            }
            
            // Emit signal for entity interpolation system
            EmitSignal(SignalName.SnapshotReceived, serverTick, data);
            
            GD.PrintVerbose($"[NetworkManager] Snapshot received tick={serverTick}");
        }

        /// <summary>
        /// Process server correction for client-side prediction
        /// Parses ServerCorrection FlatBuffer and forwards to PredictedPlayer
        /// </summary>
        private void ProcessServerCorrection(byte[] data)
        {
            GD.PrintVerbose("[NetworkManager] Server correction received");
            
            // Forward to predicted player for reconciliation
            if (_predictedPlayer != null)
            {
                _predictedPlayer.OnServerCorrection(data);
            }
            else
            {
                // Store for later when player is set
                CallDeferred(nameof(DeferredCorrection), data);
            }
            
            // Emit signal for debug UI
            EmitSignal(SignalName.ServerCorrection, data);
        }

        /// <summary>
        /// Handle correction received before player was set
        /// </summary>
        private void DeferredCorrection(byte[] data)
        {
            if (_predictedPlayer != null)
            {
                _predictedPlayer.OnServerCorrection(data);
            }
        }

        private void ProcessEvent(byte[] data)
        {
            // Handle reliable events (damage, pickups, etc.)
            ProcessCombatEvent(data);
        }

        /// <summary>
        /// Process combat event from server
        /// Event format: [packet_type:1][event_type:2][event_data...]
        /// </summary>
        private void ProcessCombatEvent(byte[] data)
        {
            if (data.Length < 3) return;
            
            // Parse: [event_type:2][event_data...]
            uint eventType = BitConverter.ToUInt16(data, 1);
            byte[] eventData = new byte[data.Length - 3];
            Array.Copy(data, 3, eventData, 0, eventData.Length);
            
            EmitSignal(SignalName.CombatEventReceived, eventType, eventData);
        }

        /// <summary>
        /// Send respawn request to server
        /// </summary>
        public void SendRespawnRequest()
        {
            // Send respawn request to server
            byte[] data = new byte[5];
            data[0] = PACKET_RESPAWN_REQUEST;
            BitConverter.GetBytes(GameState.Instance.LocalEntityId).CopyTo(data, 1);
            
            SendReliable(data);
        }

        /// <summary>
        /// Send reliable data to server
        /// </summary>
        private void SendReliable(byte[] data)
        {
            if (_udpClient == null || _serverEndPoint == null) return;
            
            try
            {
                _udpClient.Send(data, data.Length);
            }
            catch (Exception ex)
            {
                GD.PrintErr($"[NetworkManager] Reliable send failed: {ex.Message}");
            }
        }

        private void ProcessPong(byte[] data)
        {
            if (data.Length >= 5)
            {
                uint receivedTime = BitConverter.ToUInt32(data, 1);
                uint now = (uint)Time.GetTicksMsec();
                _currentRtt = now - _pingSentTime;
                GameState.Instance.LastRttMs = _currentRtt;
            }
        }

        private void ProcessConnectionResponse(byte[] data)
        {
            if (data.Length < 6) return;
            
            bool success = data[1] != 0;
            
            if (success)
            {
                uint entityId = BitConverter.ToUInt32(data, 2);
                GameState.Instance.LocalEntityId = entityId;
                GameState.Instance.SetConnectionState(GameState.ConnectionState.Connected);
                EmitSignal(SignalName.ConnectionResult, true, "");
                GD.Print($"[NetworkManager] Connected! Entity ID: {entityId}");
            }
            else
            {
                string error = "Connection rejected";
                if (data.Length > 6)
                {
                    // Could parse error message from remaining bytes
                }
                EmitSignal(SignalName.ConnectionResult, false, error);
                GameState.Instance.SetConnectionState(GameState.ConnectionState.Error);
            }
        }

        /// <summary>
        /// Get current RTT in milliseconds
        /// </summary>
        public uint GetRtt() => _currentRtt;
        
        /// <summary>
        /// Get current input sequence number
        /// </summary>
        public uint GetInputSequence() => _inputSequence;
    }
}
