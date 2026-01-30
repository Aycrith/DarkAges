using Godot;
using System;
using System.Collections.Generic;

namespace DarkAges.Entities
{
    /// <summary>
    /// [CLIENT_AGENT] Interpolated remote player entity
    /// Renders other players with smooth interpolation between snapshots
    /// </summary>
    public partial class RemotePlayer : CharacterBody3D
    {
        [Export] public float InterpolationDelay = 0.1f;  // 100ms behind server
        [Export] public float InterpolationSpeed = 15.0f; // Lerp speed
        
        // Entity identification
        public uint EntityId { get; set; }
        
        // State buffer for interpolation (holds last N states)
        private class EntityState
        {
            public double Timestamp;
            public Vector3 Position;
            public Vector3 Velocity;
            public Quaternion Rotation;
            public byte HealthPercent;
            public byte AnimState;
        }
        
        private Queue<EntityState> stateBuffer_ = new();
        private const int MAX_BUFFER_SIZE = 10;  // ~166ms at 60Hz
        
        // Current interpolation targets
        private Vector3 targetPosition_;
        private Quaternion targetRotation_;
        private Vector3 currentVelocity_;
        
        // Visual components
        private Node3D? modelRoot_;
        private AnimationPlayer? animPlayer_;
        
        // Extrapolation when buffer runs low
        private bool isExtrapolating_ = false;
        private double extrapolationStartTime_;
        private Vector3 extrapolationVelocity_;
        private const double MAX_EXTRAPOLATION_TIME = 0.25;  // 250ms max

        public override void _Ready()
        {
            modelRoot_ = GetNode<Node3D>("Model");
            animPlayer_ = GetNode<AnimationPlayer>("Model/AnimationPlayer");
            
            targetPosition_ = GlobalPosition;
            targetRotation_ = Quaternion.Identity;
        }

        public override void _Process(double delta)
        {
            if (isExtrapolating_)
            {
                UpdateExtrapolation(delta);
            }
            else if (stateBuffer_.Count >= 2)
            {
                UpdateInterpolation(delta);
            }
            else if (stateBuffer_.Count == 1)
            {
                // Not enough states, just snap to target
                var state = stateBuffer_.Peek();
                GlobalPosition = GlobalPosition.Lerp(state.Position, (float)delta * InterpolationSpeed);
            }
            
            // Update animation based on movement
            UpdateAnimation(delta);
        }

        /// <summary>
        /// Called when new snapshot arrives from server
        /// </summary>
        public void OnSnapshotReceived(uint serverTick, Vector3 position, Vector3 velocity, 
                                       Quaternion rotation, byte health, byte animState)
        {
            var state = new EntityState
            {
                Timestamp = Time.GetTicksMsec() / 1000.0,  // Current time
                Position = position,
                Velocity = velocity,
                Rotation = rotation,
                HealthPercent = health,
                AnimState = animState
            };
            
            // Add to buffer, remove old if full
            stateBuffer_.Enqueue(state);
            if (stateBuffer_.Count > MAX_BUFFER_SIZE)
            {
                stateBuffer_.Dequeue();
            }
            
            // Stop extrapolating if we got new data
            if (isExtrapolating_)
            {
                isExtrapolating_ = false;
                GD.Print($"[RemotePlayer {EntityId}] Extrapolation ended, back to interpolation");
            }
        }

        private void UpdateInterpolation(double delta)
        {
            // Get the two states to interpolate between
            var stateArray = stateBuffer_.ToArray();
            
            // Find the two states bracketing our render time
            double renderTime = Time.GetTicksMsec() / 1000.0 - InterpolationDelay;
            
            EntityState? fromState = null;
            EntityState? toState = null;
            
            for (int i = 0; i < stateArray.Length - 1; i++)
            {
                if (stateArray[i].Timestamp <= renderTime && stateArray[i + 1].Timestamp >= renderTime)
                {
                    fromState = stateArray[i];
                    toState = stateArray[i + 1];
                    break;
                }
            }
            
            if (fromState != null && toState != null)
            {
                // Calculate interpolation alpha
                double timeDiff = toState.Timestamp - fromState.Timestamp;
                if (timeDiff > 0)
                {
                    float t = (float)((renderTime - fromState.Timestamp) / timeDiff);
                    t = Mathf.Clamp(t, 0.0f, 1.0f);
                    
                    // Interpolate position
                    targetPosition_ = fromState.Position.Lerp(toState.Position, t);
                    GlobalPosition = GlobalPosition.Lerp(targetPosition_, (float)delta * InterpolationSpeed);
                    
                    // Interpolate rotation (spherical)
                    targetRotation_ = fromState.Rotation.Slerp(toState.Rotation, t);
                    if (modelRoot_ != null)
                    {
                        modelRoot_.Quaternion = modelRoot_.Quaternion.Slerp(targetRotation_, (float)delta * InterpolationSpeed);
                    }
                    
                    currentVelocity_ = fromState.Velocity.Lerp(toState.Velocity, t);
                }
            }
            else
            {
                // Not enough data, start extrapolating
                StartExtrapolation();
            }
        }

        private void StartExtrapolation()
        {
            if (stateBuffer_.Count == 0) return;
            
            var lastState = stateBuffer_.ToArray()[stateBuffer_.Count - 1];
            
            isExtrapolating_ = true;
            extrapolationStartTime_ = Time.GetTicksMsec() / 1000.0;
            extrapolationVelocity_ = lastState.Velocity;
            
            GD.Print($"[RemotePlayer {EntityId}] Starting extrapolation");
        }

        private void UpdateExtrapolation(double delta)
        {
            double elapsed = Time.GetTicksMsec() / 1000.0 - extrapolationStartTime_;
            
            if (elapsed > MAX_EXTRAPOLATION_TIME)
            {
                // Stop moving if extrapolating too long
                extrapolationVelocity_ = Vector3.Zero;
            }
            
            // Simple linear extrapolation
            GlobalPosition += extrapolationVelocity_ * (float)delta;
        }

        private void UpdateAnimation(double delta)
        {
            if (animPlayer_ == null) return;
            
            float speed = currentVelocity_.Length();
            string targetAnim = "Idle";
            
            if (speed > 0.1f)
            {
                targetAnim = speed > 8.0f ? "Sprint" : "Walk";
            }
            
            // Crossfade to target animation
            if (!animPlayer_.IsPlaying() || animPlayer_.CurrentAnimation != targetAnim)
            {
                animPlayer_.Play(targetAnim, customSpeed: speed / 6.0f);
            }
        }

        public void OnEntityRemoved()
        {
            // Play despawn effect or fade out
            QueueFree();
        }
    }
}
