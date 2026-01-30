using Godot;
using System;
using System.Collections.Generic;
using DarkAges.Networking;

namespace DarkAges
{
    /// <summary>
    /// [CLIENT_AGENT] Client-side predicted player controller
    /// Predicts movement locally, server corrects errors via reconciliation
    /// 
    /// Design:
    /// - All inputs are predicted immediately and stored in a buffer
    /// - Server processes inputs and sends corrections when needed
    /// - On correction: rewind to server state, replay unacknowledged inputs
    /// - Smooth error correction for small drift, snap for large errors
    /// </summary>
    public partial class PredictedPlayer : CharacterBody3D
    {
        // Movement constants - MUST MATCH SERVER EXACTLY
        [Export] public float MaxSpeed = 6.0f;
        [Export] public float SprintMultiplier = 1.5f;
        [Export] public float Acceleration = 10.0f;
        [Export] public float RotationSpeed = 720.0f;
        [Export] public float JumpVelocity = 8.0f;
        [Export] public float Gravity = 20.0f;
        
        // Correction smoothing - small errors blend smoothly, large errors snap
        [Export] public float CorrectionSmoothing = 0.3f;  // Blend factor for smooth correction (0-1)
        [Export] public float ErrorSnapThreshold = 2.0f;   // Meters - snap instantly above this
        [Export] public float ErrorCorrectThreshold = 0.1f; // Meters - correct below this
        
        // Input buffer configuration
        private const int MAX_INPUT_BUFFER_SIZE = 120;  // 2 seconds at 60Hz
        private const float FIXED_DT = 1.0f / 60.0f;    // Fixed timestep for reconciliation
        
        // State
        private uint _inputSequence = 1;  // Start at 1, 0 reserved for "no input processed"
        private Queue<PredictedInput> _inputBuffer = new();
        private Vector3 _serverPosition = Vector3.Zero;
        private Vector3 _serverVelocity = Vector3.Zero;
        private Vector3 _predictedPosition = Vector3.Zero;
        private Vector3 _predictedVelocity = Vector3.Zero;
        private bool _reconciling = false;
        private uint _lastProcessedServerInput = 0;
        private float _predictionError = 0.0f;
        private int _reconciliationCount = 0;  // Debug counter
        private uint _lastCorrectionTick = 0;
        
        // Smooth correction target
        private Vector3 _correctionTargetPosition;
        private bool _isSmoothingCorrection = false;
        
        // Camera
        private Node3D _cameraRig;
        private SpringArm3D _springArm;
        private float _cameraYaw = 0.0f;
        private float _cameraPitch = 0.0f;
        
        // Components
        private AnimationPlayer _animPlayer;
        
        // Signals for UI/debug
        [Signal]
        public delegate void PredictionErrorEventHandler(float error, bool isLarge);
        
        [Signal]
        public delegate void ReconciliationEventHandler(int inputCount, float error);

        public override void _Ready()
        {
            _cameraRig = GetNode<Node3D>("CameraRig");
            _springArm = GetNode<SpringArm3D>("CameraRig/SpringArm3D");
            _animPlayer = GetNode<AnimationPlayer>("AnimationPlayer");
            
            _predictedPosition = GlobalPosition;
            _correctionTargetPosition = GlobalPosition;
            
            // Hide mouse cursor for gameplay
            Input.MouseMode = Input.MouseModeEnum.Captured;
            
            GD.Print("[PredictedPlayer] Initialized with prediction buffer");
        }

        public override void _Input(InputEvent @event)
        {
            if (@event is InputEventMouseMotion mouseMotion)
            {
                // Rotate camera with mouse
                _cameraYaw -= mouseMotion.Relative.X * 0.003f;
                _cameraPitch -= mouseMotion.Relative.Y * 0.003f;
                _cameraPitch = Mathf.Clamp(_cameraPitch, -Mathf.Pi / 3, Mathf.Pi / 4);
                
                if (_cameraRig != null)
                {
                    _cameraRig.Rotation = new Vector3(_cameraPitch, _cameraYaw, 0);
                }
            }
            
            if (@event.IsActionPressed("ui_cancel"))
            {
                // Toggle mouse capture
                Input.MouseMode = Input.MouseMode == Input.MouseModeEnum.Captured 
                    ? Input.MouseModeEnum.Visible 
                    : Input.MouseModeEnum.Captured;
            }
        }

        public override void _PhysicsProcess(double delta)
        {
            // Apply smooth correction if active
            if (_isSmoothingCorrection)
            {
                GlobalPosition = GlobalPosition.Lerp(_correctionTargetPosition, CorrectionSmoothing);
                if (GlobalPosition.DistanceSquared(_correctionTargetPosition) < 0.0001f)
                {
                    _isSmoothingCorrection = false;
                }
            }
            
            if (GameState.Instance.CurrentConnectionState != GameState.ConnectionState.Connected)
                return;
            
            // Don't predict while reconciling - server state will be applied
            if (_reconciling)
                return;
            
            // 1. Gather input
            var input = GatherInput();
            
            // 2. Apply locally immediately (prediction)
            ApplyMovement(input, (float)delta);
            _predictedPosition = GlobalPosition;
            _predictedVelocity = Velocity;
            
            // 3. Store for reconciliation and network sending
            StorePredictedInput(input, _predictedPosition, _predictedVelocity, (float)delta);
            
            // 4. Update animations
            UpdateAnimation(input);
            
            // 5. Update debug stats
            UpdateDebugStats();
        }

        /// <summary>
        /// Store input in prediction buffer for potential reconciliation
        /// </summary>
        private void StorePredictedInput(InputState input, Vector3 predictedPos, Vector3 predictedVel, float dt)
        {
            // Enforce buffer size limit to prevent unbounded growth
            while (_inputBuffer.Count >= MAX_INPUT_BUFFER_SIZE)
            {
                var removed = _inputBuffer.Dequeue();
                GD.PrintVerbose($"[PredictedPlayer] Dropping old input seq={removed.Sequence} (buffer full)");
            }
            
            // Calculate input direction from movement keys
            Vector2 inputDir = new Vector2(
                (input.Right ? 1.0f : 0.0f) - (input.Left ? 1.0f : 0.0f),
                (input.Backward ? 1.0f : 0.0f) - (input.Forward ? 1.0f : 0.0f)
            );
            
            if (inputDir.LengthSquared() > 0)
            {
                inputDir = inputDir.Normalized();
            }
            
            var predInput = new PredictedInput
            {
                Sequence = _inputSequence++,
                Timestamp = (uint)Time.GetTicksMsec(),
                InputDir = inputDir,
                Yaw = input.Yaw,
                Sprint = input.Sprint,
                Jump = input.Jump,
                Attack = input.Attack,
                Block = input.Block,
                PredictedPosition = predictedPos,
                PredictedVelocity = predictedVel,
                DeltaTime = dt
            };
            
            _inputBuffer.Enqueue(predInput);
        }

        /// <summary>
        /// Called when server sends correction
        /// Parses ServerCorrection FlatBuffer and handles reconciliation
        /// </summary>
        public void OnServerCorrection(byte[] correctionData)
        {
            try
            {
                // Parse FlatBuffer ServerCorrection
                // Format: [packet_type:1][server_tick:4][pos_x:2][pos_y:2][pos_z:2][vel_x:2][vel_y:2][vel_z:2][last_input:4]
                if (correctionData.Length < 19)
                {
                    GD.PrintErr("[PredictedPlayer] Invalid correction packet size");
                    return;
                }
                
                int offset = 1;  // Skip packet type byte
                
                uint serverTick = BitConverter.ToUInt32(correctionData, offset);
                offset += 4;
                
                // Parse quantized position (Vec3: x,y,z as int16, actual = value / 64.0)
                float posX = BitConverter.ToInt16(correctionData, offset) / 64.0f;
                offset += 2;
                float posY = BitConverter.ToInt16(correctionData, offset) / 64.0f;
                offset += 2;
                float posZ = BitConverter.ToInt16(correctionData, offset) / 64.0f;
                offset += 2;
                
                // Parse quantized velocity (Vec3Velocity: x,y,z as int16, actual = value / 256.0)
                float velX = BitConverter.ToInt16(correctionData, offset) / 256.0f;
                offset += 2;
                float velY = BitConverter.ToInt16(correctionData, offset) / 256.0f;
                offset += 2;
                float velZ = BitConverter.ToInt16(correctionData, offset) / 256.0f;
                offset += 2;
                
                uint lastProcessedInput = BitConverter.ToUInt32(correctionData, offset);
                
                // Apply the correction
                ApplyServerCorrection(
                    serverTick,
                    new Vector3(posX, posY, posZ),
                    new Vector3(velX, velY, velZ),
                    lastProcessedInput
                );
            }
            catch (Exception ex)
            {
                GD.PrintErr($"[PredictedPlayer] Failed to parse correction: {ex.Message}");
            }
        }

        /// <summary>
        /// Apply server correction with appropriate handling based on error magnitude
        /// </summary>
        private void ApplyServerCorrection(uint serverTick, Vector3 serverPos, Vector3 serverVel, uint lastProcessedSeq)
        {
            _serverPosition = serverPos;
            _serverVelocity = serverVel;
            _lastProcessedServerInput = lastProcessedSeq;
            _lastCorrectionTick = serverTick;
            
            // Remove acknowledged inputs from buffer (all inputs up to and including lastProcessedSeq)
            int removedCount = 0;
            while (_inputBuffer.Count > 0 && _inputBuffer.Peek().Sequence <= lastProcessedSeq)
            {
                _inputBuffer.Dequeue();
                removedCount++;
            }
            
            // Calculate prediction error at the point of server processing
            // We compare server's position to where we predicted we'd be at that input
            Vector3 predictedPosAtServerTime = GetPositionAtSequence(lastProcessedSeq);
            _predictionError = predictedPosAtServerTime.DistanceTo(serverPos);
            
            // Emit debug signal
            EmitSignal(SignalName.PredictionError, _predictionError, _predictionError > ErrorSnapThreshold);
            
            // Determine correction strategy based on error magnitude
            if (_predictionError <= ErrorCorrectThreshold)
            {
                // Error is negligible - no correction needed
                GD.PrintVerbose($"[PredictedPlayer] Small error ({_predictionError:F3}m) - no correction");
                return;
            }
            else if (_predictionError > ErrorSnapThreshold)
            {
                // Large error - snap instantly (possible cheat, desync, or respawn)
                GD.Print($"[PredictedPlayer] LARGE CORRECTION: {_predictionError:F2}m - snapping to server pos");
                GlobalPosition = serverPos;
                Velocity = serverVel;
                _predictedPosition = serverPos;
                _predictedVelocity = serverVel;
                _isSmoothingCorrection = false;
                
                // Still need to replay unacknowledged inputs
                if (_inputBuffer.Count > 0)
                {
                    ReplayUnacknowledgedInputs(serverPos, serverVel, lastProcessedSeq);
                }
            }
            else
            {
                // Small drift - reconcile by replaying
                GD.Print($"[PredictedPlayer] Smooth correction: {_predictionError:F3}m, replaying {_inputBuffer.Count} inputs");
                
                _isSmoothingCorrection = false;
                Reconcile(serverPos, serverVel, lastProcessedSeq);
            }
        }

        /// <summary>
        /// Get the predicted position at a specific input sequence
        /// </summary>
        private Vector3 GetPositionAtSequence(uint sequence)
        {
            // Find the input with matching sequence
            foreach (var input in _inputBuffer)
            {
                if (input.Sequence == sequence)
                {
                    return input.PredictedPosition;
                }
            }
            
            // If not found, use current position
            return _predictedPosition;
        }

        /// <summary>
        /// Reconcile by rewinding to server state and replaying unacknowledged inputs
        /// </summary>
        private void Reconcile(Vector3 serverPos, Vector3 serverVel, uint lastProcessedSeq)
        {
            _reconciling = true;
            _reconciliationCount++;
            
            // Emit signal for debug UI
            EmitSignal(SignalName.Reconciliation, _inputBuffer.Count, _predictionError);
            
            // Start from server state
            GlobalPosition = serverPos;
            Velocity = serverVel;
            
            // Replay all unacknowledged inputs
            int replayedCount = ReplayUnacknowledgedInputs(serverPos, serverVel, lastProcessedSeq);
            
            // Update predicted state
            _predictedPosition = GlobalPosition;
            _predictedVelocity = Velocity;
            
            GD.PrintVerbose($"[PredictedPlayer] Reconciliation complete: replayed {replayedCount} inputs");
            
            _reconciling = false;
        }

        /// <summary>
        /// Replay unacknowledged inputs after server correction
        /// </summary>
        private int ReplayUnacknowledgedInputs(Vector3 serverPos, Vector3 serverVel, uint lastProcessedSeq)
        {
            int replayedCount = 0;
            
            foreach (var input in _inputBuffer)
            {
                // Only replay inputs that server hasn't processed yet
                if (input.Sequence > lastProcessedSeq)
                {
                    var inputState = new InputState
                    {
                        Forward = input.InputDir.Y < -0.1f,
                        Backward = input.InputDir.Y > 0.1f,
                        Left = input.InputDir.X < -0.1f,
                        Right = input.InputDir.X > 0.1f,
                        Sprint = input.Sprint,
                        Jump = input.Jump,
                        Attack = input.Attack,
                        Block = input.Block,
                        Yaw = input.Yaw
                    };
                    
                    // Use fixed timestep for deterministic replay
                    ApplyMovement(inputState, FIXED_DT);
                    replayedCount++;
                    
                    // Store the replayed result back in the input for future error calculation
                    input.PredictedPosition = GlobalPosition;
                    input.PredictedVelocity = Velocity;
                }
            }
            
            return replayedCount;
        }

        /// <summary>
        /// Smoothly correct position over time (used for minor drift)
        /// </summary>
        private void SmoothCorrect(Vector3 targetPos)
        {
            _correctionTargetPosition = targetPos;
            _isSmoothingCorrection = true;
        }

        private InputState GatherInput()
        {
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
                Yaw = _cameraYaw,
                Pitch = _cameraPitch
            };
        }

        /// <summary>
        /// Apply movement - MUST MATCH SERVER PHYSICS EXACTLY
        /// </summary>
        private void ApplyMovement(InputState input, float dt)
        {
            // Calculate input direction
            Vector3 direction = Vector3.Zero;
            
            if (input.Forward) direction.Z -= 1;
            if (input.Backward) direction.Z += 1;
            if (input.Left) direction.X -= 1;
            if (input.Right) direction.X += 1;
            
            if (direction.LengthSquared() > 0)
            {
                direction = direction.Normalized();
                
                // Rotate by camera yaw
                direction = direction.Rotated(Vector3.Up, input.Yaw);
                
                // Smooth acceleration
                float targetSpeed = MaxSpeed * (input.Sprint ? SprintMultiplier : 1.0f);
                Vector3 targetVel = direction * targetSpeed;
                
                // Preserve Y velocity (gravity/jump), only lerp XZ
                targetVel.Y = Velocity.Y;
                Velocity = Velocity.Lerp(targetVel, Acceleration * dt);
                
                // Rotate player to face movement direction
                float targetRotation = Mathf.Atan2(direction.X, direction.Z);
                Rotation = new Vector3(
                    Rotation.X,
                    Mathf.LerpAngle(Rotation.Y, targetRotation, RotationSpeed * dt * Mathf.Pi / 180.0f),
                    Rotation.Z
                );
            }
            else
            {
                // Decelerate XZ, preserve Y
                Vector3 targetVel = new Vector3(0, Velocity.Y, 0);
                Velocity = Velocity.Lerp(targetVel, Acceleration * dt);
            }
            
            // Jump - only if on floor
            if (input.Jump && IsOnFloor())
            {
                Velocity = new Vector3(Velocity.X, JumpVelocity, Velocity.Z);
            }
            
            // Apply gravity
            if (!IsOnFloor())
            {
                Velocity += new Vector3(0, -Gravity * dt, 0);
            }
            
            // Move
            MoveAndSlide();
        }

        private void UpdateAnimation(InputState input)
        {
            if (_animPlayer == null) return;
            
            // Simple animation state machine
            if (!IsOnFloor())
            {
                // Falling/jumping
            }
            else if (Velocity.LengthSquared() > 0.1f)
            {
                if (input.Sprint)
                {
                    // Sprint animation
                }
                else
                {
                    // Walk animation
                }
            }
            else
            {
                // Idle
            }
        }

        private void UpdateDebugStats()
        {
            // Expose stats for debug UI
            GameState.Instance.PredictionError = _predictionError;
            GameState.Instance.InputBufferSize = _inputBuffer.Count;
            GameState.Instance.LastProcessedInput = _lastProcessedServerInput;
            GameState.Instance.ReconciliationCount = _reconciliationCount;
        }

        // Public accessors for debug UI
        public float GetPredictionError() => _predictionError;
        public int GetInputBufferSize() => _inputBuffer.Count;
        public uint GetLastProcessedServerInput() => _lastProcessedServerInput;
        public int GetReconciliationCount() => _reconciliationCount;
        public bool IsReconciling() => _reconciling;
        public Vector3 GetServerPosition() => _serverPosition;
        public Vector3 GetPredictedPosition() => _predictedPosition;
        
        /// <summary>
        /// Get the next input sequence number for NetworkManager to send
        /// </summary>
        public uint GetNextInputSequence() => _inputSequence;
        
        /// <summary>
        /// Get all pending inputs for NetworkManager to send
        /// </summary>
        public Queue<PredictedInput> GetPendingInputs() => _inputBuffer;
    }
}
