using Godot;
using System;
using System.Collections.Generic;
using System.Linq;

namespace DarkAges.Entities
{
    /// <summary>
    /// [CLIENT_AGENT] WP-7-3 Entity Interpolation
    /// Renders remote players with smooth interpolation between snapshots
    /// 
    /// Features:
    /// - 100ms interpolation delay for smooth viewing
    /// - Linear interpolation between known states
    /// - Extrapolation when data is missing (up to 500ms limit)
    /// - Hit prediction visualization
    /// - Metrics for quality monitoring
    /// </summary>
    public partial class RemotePlayer : CharacterBody3D
    {
        // Configuration - exported for scene tuning
        [Export] public float InterpolationDelay = 0.1f;      // 100ms delay (P0 requirement)
        [Export] public float ExtrapolationLimit = 0.5f;      // 500ms max extrapolation (P0 requirement)
        [Export] public float PositionSmoothing = 15.0f;      // Position lerp speed
        [Export] public float RotationSmoothing = 10.0f;      // Rotation slerp speed
        [Export] public bool ShowDebugVisualization = false;  // Debug color changes
        
        // Entity identification
        public uint EntityId { get; set; }
        public string PlayerName { get; set; } = "Unknown";
        
        /// <summary>
        /// Frame of entity state for interpolation buffer
        /// </summary>
        private class EntityFrame
        {
            public double Timestamp;      // Local receive time
            public double ServerTime;     // Server timestamp
            public Vector3 Position;
            public Quaternion Rotation;
            public Vector3 Velocity;
            public float HealthPercent;
            public byte AnimationState;
        }
        
        // State buffer for interpolation (circular buffer for performance)
        private Queue<EntityFrame> _stateBuffer = new Queue<EntityFrame>();
        private const int MaxBufferSize = 10;  // ~166ms at 60Hz
        
        // Current display state
        private Vector3 _displayPosition;
        private Quaternion _displayRotation;
        private Vector3 _estimatedVelocity;
        
        // Extrapolation state
        private bool _isExtrapolating = false;
        private double _extrapolationTime = 0;
        private Vector3 _extrapolationVelocity;
        private double _lastExtrapolationTime;
        
        // Visual components
        private Node3D _modelRoot;
        private MeshInstance3D _meshInstance;
        private AnimationPlayer _animPlayer;
        private Label3D _nameLabel;
        
        // Hit visualization
        private MeshInstance3D _hitMarker;
        private double _hitMarkerVisibleTime;
        private const double HitMarkerDuration = 0.5f;
        
        // Debug materials
        private StandardMaterial3D _normalMaterial;
        private StandardMaterial3D _extrapolateMaterial;
        
        // Metrics
        private double _lastSnapshotTime;
        private float _averageJitter;
        private int _snapshotsReceived;
        
        // Animation state tracking
        private byte _currentAnimState = 0;

        public override void _Ready()
        {
            // Get components
            _modelRoot = GetNode<Node3D>("Model");
            _meshInstance = GetNode<MeshInstance3D>("Model/MeshInstance3D");
            _animPlayer = GetNode<AnimationPlayer>("Model/AnimationPlayer");
            _nameLabel = GetNode<Label3D>("NameLabel");
            
            // Initialize display state
            _displayPosition = GlobalPosition;
            _displayRotation = Quaternion.Identity;
            _lastExtrapolationTime = Time.GetTicksMsec() / 1000.0;
            
            // Set up materials
            SetupMaterials();
            
            // Set name label
            _nameLabel.Text = PlayerName;
            
            // Create hit marker (hidden by default)
            CreateHitMarker();
            
            GD.Print($"[RemotePlayer {EntityId}] Initialized with interpolation delay {InterpolationDelay * 1000:F0}ms");
        }
        
        private void SetupMaterials()
        {
            // Store original material
            if (_meshInstance != null)
            {
                _normalMaterial = _meshInstance.MaterialOverride as StandardMaterial3D;
                if (_normalMaterial == null)
                {
                    _normalMaterial = new StandardMaterial3D
                    {
                        AlbedoColor = new Color(0.8f, 0.3f, 0.3f)  // Reddish default
                    };
                }
                
                // Yellow material for extrapolation warning
                _extrapolateMaterial = new StandardMaterial3D
                {
                    AlbedoColor = Colors.Yellow,
                    Roughness = 0.5f
                };
            }
        }
        
        private void CreateHitMarker()
        {
            _hitMarker = new MeshInstance3D
            {
                Name = "HitMarker",
                Mesh = new SphereMesh { Radius = 0.15f, Height = 0.3f },
                Visible = false
            };
            
            var hitMaterial = new StandardMaterial3D
            {
                AlbedoColor = Colors.Red,
                Emission = Colors.Red,
                EmissionEnergyMultiplier = 3.0f,
                NoDepthTest = true
            };
            _hitMarker.MaterialOverride = hitMaterial;
            _hitMarker.Position = new Vector3(0, 1.0f, 0);
            
            AddChild(_hitMarker);
        }

        /// <summary>
        /// Add a new state from server snapshot
        /// Called by RemotePlayerManager when snapshot arrives
        /// </summary>
        public void AddSnapshot(EntityFrame frame)
        {
            _stateBuffer.Enqueue(frame);
            
            // Remove old states to keep buffer size limited
            while (_stateBuffer.Count > MaxBufferSize)
                _stateBuffer.Dequeue();
            
            // Track metrics
            double now = Time.GetTicksMsec() / 1000.0;
            if (_lastSnapshotTime > 0)
            {
                double delta = now - _lastSnapshotTime;
                _averageJitter = (_averageJitter * _snapshotsReceived + (float)Math.Abs(delta - 0.05)) / (_snapshotsReceived + 1);
            }
            _lastSnapshotTime = now;
            _snapshotsReceived++;
            
            // Stop extrapolating when we get fresh data
            if (_isExtrapolating)
            {
                _isExtrapolating = false;
                _extrapolationTime = 0;
                GD.PrintVerbose($"[RemotePlayer {EntityId}] Resumed interpolation");
                
                // Reset material
                if (_meshInstance != null && ShowDebugVisualization)
                {
                    _meshInstance.MaterialOverride = _normalMaterial;
                }
            }
        }
        
        /// <summary>
        /// Legacy method for compatibility - creates frame from individual parameters
        /// </summary>
        public void OnSnapshotReceived(uint serverTick, Vector3 position, Vector3 velocity,
                                       Quaternion rotation, byte health, byte animState)
        {
            var frame = new EntityFrame
            {
                Timestamp = Time.GetTicksMsec() / 1000.0,
                ServerTime = serverTick / 20.0,  // Convert tick to approximate time (20Hz)
                Position = position,
                Rotation = rotation,
                Velocity = velocity,
                HealthPercent = health / 100.0f,
                AnimationState = animState
            };
            
            AddSnapshot(frame);
            
            // Update animation
            UpdateAnimation(animState);
        }

        public override void _Process(double delta)
        {
            // Calculate render time (current time - interpolation delay)
            double currentTime = Time.GetTicksMsec() / 1000.0;
            double renderTime = currentTime - InterpolationDelay;
            
            // Get interpolated/extrapolated state for render time
            (Vector3 targetPos, Quaternion targetRot) = GetInterpolatedState(renderTime);
            
            // Smoothly interpolate to target (double smoothing for extra smoothness)
            _displayPosition = _displayPosition.Lerp(targetPos, (float)delta * PositionSmoothing);
            _displayRotation = _displayRotation.Slerp(targetRot, (float)delta * RotationSmoothing);
            
            // Apply to transform
            GlobalPosition = _displayPosition;
            if (_modelRoot != null)
            {
                _modelRoot.Quaternion = _displayRotation;
            }
            else
            {
                // Fallback if model not found
                GlobalRotation = _displayRotation.GetEuler();
            }
            
            // Update hit marker
            UpdateHitMarker(delta);
            
            // Update extrapolation visualization
            UpdateExtrapolationVisuals();
        }
        
        /// <summary>
        /// Get interpolated or extrapolated state for given render time
        /// </summary>
        private (Vector3, Quaternion) GetInterpolatedState(double renderTime)
        {
            var frames = _stateBuffer.ToArray();
            
            if (frames.Length == 0)
            {
                // No data yet - use current position
                return (_displayPosition, _displayRotation);
            }
            
            if (frames.Length == 1)
            {
                // Only one frame available
                _estimatedVelocity = frames[0].Velocity;
                return (frames[0].Position, frames[0].Rotation);
            }
            
            // Find two frames to interpolate between
            // We want frames where: frame[i].ServerTime <= renderTime <= frame[i+1].ServerTime
            for (int i = 0; i < frames.Length - 1; i++)
            {
                if (frames[i].ServerTime <= renderTime && frames[i + 1].ServerTime >= renderTime)
                {
                    // Calculate interpolation factor
                    double timeDiff = frames[i + 1].ServerTime - frames[i].ServerTime;
                    if (timeDiff > 0.001)  // Avoid division by zero
                    {
                        float t = (float)((renderTime - frames[i].ServerTime) / timeDiff);
                        t = Mathf.Clamp(t, 0.0f, 1.0f);
                        
                        // Interpolate position (linear)
                        Vector3 pos = frames[i].Position.Lerp(frames[i + 1].Position, t);
                        
                        // Interpolate rotation (spherical)
                        Quaternion rot = frames[i].Rotation.Slerp(frames[i + 1].Rotation, t);
                        
                        // Estimate velocity
                        _estimatedVelocity = frames[i].Velocity.Lerp(frames[i + 1].Velocity, t);
                        
                        return (pos, rot);
                    }
                }
            }
            
            // Render time is ahead of our newest frame - need to extrapolate
            var newestFrame = frames[frames.Length - 1];
            
            if (renderTime > newestFrame.ServerTime)
            {
                double extrapolateDelta = renderTime - newestFrame.ServerTime;
                
                // Cap extrapolation to avoid running too far ahead
                if (extrapolateDelta > ExtrapolationLimit)
                {
                    extrapolateDelta = ExtrapolationLimit;
                    _isExtrapolating = true;
                }
                else
                {
                    _isExtrapolating = true;
                }
                
                // Linear extrapolation using last known velocity
                Vector3 extrapolatedPos = newestFrame.Position + newestFrame.Velocity * (float)extrapolateDelta;
                
                _extrapolationTime = extrapolateDelta;
                _extrapolationVelocity = newestFrame.Velocity;
                _estimatedVelocity = newestFrame.Velocity;
                _lastExtrapolationTime = Time.GetTicksMsec() / 1000.0;
                
                return (extrapolatedPos, newestFrame.Rotation);
            }
            
            // Render time is before our oldest frame - use oldest frame
            return (frames[0].Position, frames[0].Rotation);
        }
        
        private void UpdateAnimation(byte newState)
        {
            if (newState == _currentAnimState || _animPlayer == null) return;
            
            _currentAnimState = newState;
            
            // Map animation states to actual animations
            string animName = newState switch
            {
                0 => "Idle",
                1 => "Walk",
                2 => "Run",
                3 => "Sprint",
                4 => "Jump",
                5 => "Fall",
                10 => "Attack",
                11 => "Attack_Heavy",
                12 => "Block",
                13 => "Hit",
                14 => "Death",
                15 => "Respawn",
                _ => "Idle"
            };
            
            // Play animation if it exists
            if (_animPlayer.HasAnimation(animName))
            {
                _animPlayer.Play(animName);
            }
            else if (_animPlayer.HasAnimation("Idle"))
            {
                _animPlayer.Play("Idle");
            }
        }
        
        /// <summary>
        /// Show hit feedback for combat visualization (WP-7-3 requirement)
        /// </summary>
        public void ShowHitFeedback(Vector3 hitPosition, int damage)
        {
            // Show hit marker
            _hitMarker.GlobalPosition = ToLocal(hitPosition);
            _hitMarker.Visible = true;
            _hitMarkerVisibleTime = HitMarkerDuration;
            
            // Create floating damage number
            CreateDamageNumber(hitPosition, damage);
        }
        
        private void CreateDamageNumber(Vector3 position, int damage)
        {
            var damageLabel = new Label3D
            {
                Text = damage.ToString(),
                Billboard = BaseMaterial3D.BillboardModeEnum.Enabled,
                PixelSize = 0.015f,
                Position = position + Vector3.Up * 0.5f,
                Modulate = damage > 0 ? Colors.Yellow : Colors.Green
            };
            
            GetTree().Root.AddChild(damageLabel);
            
            // Animate and remove
            var tween = CreateTween();
            tween.SetParallel(true);
            tween.TweenProperty(damageLabel, "position", damageLabel.Position + Vector3.Up * 1.0f, 1.0f);
            tween.TweenProperty(damageLabel, "modulate:a", 0.0f, 0.5f).SetDelay(0.5f);
            tween.Chain().TweenCallback(Callable.From(() => damageLabel.QueueFree()));
        }
        
        private void UpdateHitMarker(double delta)
        {
            if (!_hitMarker.Visible) return;
            
            _hitMarkerVisibleTime -= delta;
            if (_hitMarkerVisibleTime <= 0)
            {
                _hitMarker.Visible = false;
            }
            else
            {
                // Pulse effect
                float pulse = 1.0f + Mathf.Sin((float)(_hitMarkerVisibleTime * 10)) * 0.2f;
                _hitMarker.Scale = new Vector3(pulse, pulse, pulse);
            }
        }
        
        private void UpdateExtrapolationVisuals()
        {
            if (!ShowDebugVisualization || _meshInstance == null) return;
            
            if (_isExtrapolating && _extrapolationTime > 0.1f)
            {
                // Turn yellow when extrapolating for a while
                _meshInstance.MaterialOverride = _extrapolateMaterial;
            }
            else
            {
                // Normal color
                _meshInstance.MaterialOverride = _normalMaterial;
            }
        }
        
        /// <summary>
        /// Update health display
        /// </summary>
        public void SetHealth(float percent)
        {
            // Could update health bar here
        }
        
        /// <summary>
        /// Get the visual position (for hit detection/aiming)
        /// </summary>
        public Vector3 GetVisualPosition()
        {
            return _displayPosition;
        }
        
        /// <summary>
        /// Get the predicted position for given time in future (for hit prediction)
        /// </summary>
        public Vector3 GetPredictedPosition(float timeAhead)
        {
            return _displayPosition + _estimatedVelocity * timeAhead;
        }
        
        /// <summary>
        /// Check if entity is currently extrapolating
        /// </summary>
        public bool IsExtrapolating => _isExtrapolating;
        
        /// <summary>
        /// Current extrapolation time in seconds
        /// </summary>
        public double ExtrapolationTime => _extrapolationTime;
        
        /// <summary>
        /// Get interpolation quality metrics
        /// </summary>
        public InterpolationMetrics GetMetrics()
        {
            return new InterpolationMetrics
            {
                BufferSize = _stateBuffer.Count,
                IsExtrapolating = _isExtrapolating,
                ExtrapolationTime = _extrapolationTime,
                EstimatedVelocity = _estimatedVelocity,
                AverageJitter = _averageJitter,
                SnapshotsReceived = _snapshotsReceived
            };
        }
        
        public void OnEntityRemoved()
        {
            // Fade out effect could go here
            QueueFree();
        }
    }
    
    /// <summary>
    /// Interpolation quality metrics for monitoring
    /// </summary>
    public struct InterpolationMetrics
    {
        public int BufferSize;
        public bool IsExtrapolating;
        public double ExtrapolationTime;
        public Vector3 EstimatedVelocity;
        public float AverageJitter;
        public int SnapshotsReceived;
    }
}
