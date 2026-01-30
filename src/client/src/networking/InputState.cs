namespace DarkAges.Networking
{
    /// <summary>
    /// [CLIENT_AGENT] Compact input state structure for network transmission.
    /// This is what gets sent to the server 60 times per second.
    /// </summary>
    public struct InputState
    {
        /// <summary>Monotonically increasing sequence number</summary>
        public uint Sequence;
        
        /// <summary>Client timestamp (milliseconds)</summary>
        public uint Timestamp;
        
        /// <summary>Forward movement key pressed</summary>
        public bool Forward;
        
        /// <summary>Backward movement key pressed</summary>
        public bool Backward;
        
        /// <summary>Left movement key pressed</summary>
        public bool Left;
        
        /// <summary>Right movement key pressed</summary>
        public bool Right;
        
        /// <summary>Jump key pressed</summary>
        public bool Jump;
        
        /// <summary>Sprint modifier pressed</summary>
        public bool Sprint;
        
        /// <summary>Attack triggered this frame</summary>
        public bool Attack;
        
        /// <summary>Block held this frame</summary>
        public bool Block;
        
        /// <summary>Camera yaw (horizontal rotation)</summary>
        public float Yaw;
        
        /// <summary>Camera pitch (vertical rotation)</summary>
        public float Pitch;
        
        /// <summary>
        /// Pack input flags into a single byte for efficient transmission.
        /// Bit layout: [Sprint][Block][Attack][Jump][Right][Left][Backward][Forward]
        /// </summary>
        public byte PackFlags()
        {
            byte flags = 0;
            if (Forward) flags |= 0x01;
            if (Backward) flags |= 0x02;
            if (Left) flags |= 0x04;
            if (Right) flags |= 0x08;
            if (Jump) flags |= 0x10;
            if (Attack) flags |= 0x20;
            if (Block) flags |= 0x40;
            if (Sprint) flags |= 0x80;
            return flags;
        }
        
        /// <summary>
        /// Unpack input flags from a byte.
        /// </summary>
        public void UnpackFlags(byte flags)
        {
            Forward = (flags & 0x01) != 0;
            Backward = (flags & 0x02) != 0;
            Left = (flags & 0x04) != 0;
            Right = (flags & 0x08) != 0;
            Jump = (flags & 0x10) != 0;
            Attack = (flags & 0x20) != 0;
            Block = (flags & 0x40) != 0;
            Sprint = (flags & 0x80) != 0;
        }
        
        public override string ToString()
        {
            return $"InputState(seq={Sequence}, flags={PackFlags():X2})";
        }
    }
}
