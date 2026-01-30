using Godot;
using System;

namespace DarkAges
{
    /// <summary>
    /// [CLIENT_AGENT] Stored predicted input for reconciliation
    /// Captures the full input state and predicted result for replay
    /// </summary>
    public class PredictedInput
    {
        /// <summary>Monotonically increasing sequence number for this input</summary>
        public uint Sequence { get; set; }
        
        /// <summary>Client timestamp when input was generated (ms)</summary>
        public uint Timestamp { get; set; }
        
        /// <summary>Normalized movement direction (X=left/right, Y=forward/back)</summary>
        public Vector2 InputDir { get; set; }
        
        /// <summary>Camera yaw rotation in radians</summary>
        public float Yaw { get; set; }
        
        /// <summary>Sprint button pressed</summary>
        public bool Sprint { get; set; }
        
        /// <summary>Jump button pressed</summary>
        public bool Jump { get; set; }
        
        /// <summary>Attack button pressed</summary>
        public bool Attack { get; set; }
        
        /// <summary>Block button pressed</summary>
        public bool Block { get; set; }
        
        /// <summary>Position predicted by client after applying this input</summary>
        public Vector3 PredictedPosition { get; set; }
        
        /// <summary>Velocity predicted by client after applying this input</summary>
        public Vector3 PredictedVelocity { get; set; }
        
        /// <summary>Delta time used for this input</summary>
        public float DeltaTime { get; set; }

        public override string ToString()
        {
            return $"[PredictedInput seq={Sequence} pos={PredictedPosition} vel={PredictedVelocity}]";
        }
    }
}
