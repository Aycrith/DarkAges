using Godot;
using DarkAges.Combat;

namespace DarkAges.UI
{
    /// <summary>
    /// [CLIENT_AGENT] Player health bar
    /// </summary>
    public partial class HealthBar : Control
    {
        [Export] public float SmoothSpeed = 10.0f;
        
        private ProgressBar? healthBar;
        private Label? healthLabel;
        private float targetHealth = 100;
        private float currentHealth = 100;
        private float maxHealth = 100;

        public override void _Ready()
        {
            healthBar = GetNode<ProgressBar>("HealthBar");
            healthLabel = GetNode<Label>("HealthLabel");
            
            // Connect to combat events
            CombatEventSystem.Instance.DamageTaken += OnDamageTaken;
        }

        public override void _ExitTree()
        {
            if (CombatEventSystem.Instance != null)
            {
                CombatEventSystem.Instance.DamageTaken -= OnDamageTaken;
            }
        }

        private void OnDamageTaken(int damage, bool isCritical)
        {
            targetHealth = Mathf.Max(0, targetHealth - damage);
        }

        public void SetHealth(float current, float max)
        {
            targetHealth = current;
            maxHealth = max;
        }

        public override void _Process(double delta)
        {
            // Smooth health bar
            currentHealth = Mathf.Lerp(currentHealth, targetHealth, (float)delta * SmoothSpeed);
            
            if (healthBar != null)
            {
                healthBar.Value = currentHealth;
                healthBar.MaxValue = maxHealth;
            }
            
            if (healthLabel != null)
            {
                healthLabel.Text = $"{Mathf.Round(currentHealth)} / {maxHealth}";
            }
            
            // Color based on health percentage
            float pct = currentHealth / maxHealth;
            if (healthBar != null)
            {
                if (pct > 0.6f)
                    healthBar.Modulate = new Color(0.2f, 0.8f, 0.2f);  // Green
                else if (pct > 0.3f)
                    healthBar.Modulate = new Color(0.9f, 0.9f, 0.2f);  // Yellow
                else
                    healthBar.Modulate = new Color(0.9f, 0.2f, 0.2f);  // Red
            }
        }
    }
}
