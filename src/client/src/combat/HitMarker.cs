using Godot;

namespace DarkAges.Combat
{
    /// <summary>
    /// [CLIENT_AGENT] Crosshair hit marker
    /// </summary>
    public partial class HitMarker : Control
    {
        [Export] public Color NormalColor = new(1, 1, 1, 0.8f);
        [Export] public Color CriticalColor = new(1, 0.5f, 0, 1);
        [Export] public float DisplayTime = 0.3f;
        
        private Timer? timer;
        private ColorRect? markerRect;

        public override void _Ready()
        {
            timer = new Timer();
            timer.WaitTime = DisplayTime;
            timer.OneShot = true;
            timer.Timeout += () => Hide();
            AddChild(timer);
            
            markerRect = GetNode<ColorRect>("Marker");
            Hide();
        }

        public void ShowHit(bool isCritical)
        {
            if (markerRect == null) return;
            
            markerRect.Color = isCritical ? CriticalColor : NormalColor;
            
            // Scale up for crit
            markerRect.Scale = isCritical ? new Vector2(1.5f, 1.5f) : Vector2.One;
            
            Show();
            timer?.Start();
        }
    }
}
