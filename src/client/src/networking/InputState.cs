using Godot;

namespace DarkAges.Networking
{
    /// <summary>
    /// [CLIENT_AGENT] Input state structure
    /// Captures all player input for a frame
    /// </summary>
    public struct InputState
    {
        public bool Forward;
        public bool Backward;
        public bool Left;
        public bool Right;
        public bool Jump;
        public bool Sprint;
        public bool Attack;
        public bool Block;
        public float Yaw;
        public float Pitch;
        public uint Sequence;
        public uint Timestamp;
        
        public override string ToString()
        {
            return $"[InputState seq={Sequence} fwd={Forward} bwd={Backward} l={Left} r={Right} jmp={Jump} spr={Sprint} yaw={Yaw:F2}]";
        }
    }
}
