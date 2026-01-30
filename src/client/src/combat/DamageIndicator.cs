using Godot;

namespace DarkAges.Combat
{
    /// <summary>
    /// [CLIENT_AGENT] Damage indicator showing damage taken direction
    /// </summary>
    public partial class DamageIndicator : Control
    {
        [Export] public float DisplayTime = 0.5f;
        [Export] public Color NormalColor = new(1, 0, 0, 0.5f);
        [Export] public Color CriticalColor = new(1, 0, 0, 0.9f);
        
        private Timer? timer;
        private ColorRect? indicatorRect;

        public override void _Ready()
        {
            timer = new Timer();
            timer.WaitTime = DisplayTime;
            timer.OneShot = true;
            timer.Timeout += () => Hide();
            AddChild(timer);
            
            indicatorRect = GetNode<ColorRect>("Indicator");
            Hide();
        }

        public void ShowDamage(int damage, bool isCritical)
        {
            if (indicatorRect == null) return;
            
            indicatorRect.Color = isCritical ? CriticalColor : NormalColor;
            
            // Flash effect
            var tween = CreateTween();
            tween.TweenProperty(indicatorRect, "color:a", 0.0f, DisplayTime);
            
            Show();
            timer?.Start();
        }
    }
}
