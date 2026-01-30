using Godot;

namespace DarkAges
{
    /// <summary>
    /// [CLIENT_AGENT] Represents a predicted input with its predicted result.
    /// Stored in a buffer for potential reconciliation.
    /// </summary>
    public class PredictedInput
    {
        /// <summary>Input sequence number for acknowledgment tracking</summary>
        public uint Sequence { get; set; }
        
        /// <summary>Timestamp when input was generated</summary>
        public uint Timestamp { get; set; }
        
        /// <summary>Normalized input direction (X = right/left, Y = forward/back)</summary>
        public Vector2 InputDir { get; set; }
        
        /// <summary>Camera yaw for movement direction</summary>
        public float Yaw { get; set; }
        
        /// <summary>Camera pitch for looking</summary>
        public float Pitch { get; set; }
        
        /// <summary>Sprint modifier active</summary>
        public bool Sprint { get; set; }
        
        /// <summary>Jump was pressed this frame</summary>
        public bool Jump { get; set; }
        
        /// <summary>Attack was triggered</summary>
        public bool Attack { get; set; }
        
        /// <summary>Block is held</summary>
        public bool Block { get; set; }
        
        /// <summary>The position predicted for this input</summary>
        public Vector3 PredictedPosition { get; set; }
        
        /// <summary>The velocity predicted for this input</summary>
        public Vector3 PredictedVelocity { get; set; }
        
        /// <summary>Delta time used for this prediction</summary>
        public float DeltaTime { get; set; }
        
        /// <summary>
        /// Whether this input has been acknowledged by the server
        /// </summary>
        public bool Acknowledged { get; set; }
        
        /// <summary>
        /// The server position when this input was acknowledged (for error calculation)
        /// </summary>
        public Vector3 ServerPositionAtAck { get; set; }
        
        public override string ToString()
        {
            return $"Input(seq={Sequence}, dir={InputDir}, pos={PredictedPosition}, ack={Acknowledged})";
        }
    }
}
