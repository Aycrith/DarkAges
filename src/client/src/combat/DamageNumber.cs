using Godot;
using System;

namespace DarkAges.Combat
{
    /// <summary>
    /// [CLIENT_AGENT] Floating damage number that rises and fades
    /// </summary>
    public partial class DamageNumber : Label3D
    {
        private float lifetime = 1.5f;
        private float elapsed = 0.0f;
        private Vector3 velocity = new(0, 1.0f, 0);  // Rise up
        private int damage = 0;

        public void Initialize(int dmg, Vector3 worldPosition, bool isCritical)
        {
            damage = dmg;
            GlobalPosition = worldPosition + new Vector3(0, 1.5f, 0);  // Above head
            
            Text = dmg.ToString();
            FontSize = isCritical ? 72 : 48;
            
            if (isCritical)
            {
                Modulate = new Color(1.0f, 0.3f, 0.0f);  // Orange-red for crit
                Text = $"{dmg}!";
            }
            else
            {
                Modulate = new Color(1.0f, 1.0f, 1.0f);  // White for normal
            }
            
            Billboard = BaseMaterial3D.BillboardModeEnum.Enabled;
            DoubleSided = true;
        }

        public override void _Process(double delta)
        {
            elapsed += (float)delta;
            
            // Move up
            GlobalPosition += velocity * (float)delta;
            
            // Fade out
            float alpha = 1.0f - (elapsed / lifetime);
            if (alpha < 0) alpha = 0;
            
            var color = Modulate;
            color.A = alpha;
            Modulate = color;
            
            // Destroy when faded
            if (elapsed >= lifetime)
            {
                QueueFree();
            }
        }
    }
}
