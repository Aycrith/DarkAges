using Godot;
using System;
using DarkAges.Networking;
using DarkAges.Combat;

namespace DarkAges.Client.UI
{
    /// <summary>
    /// [CLIENT_AGENT] WP-7-4 Main health bar display for local player.
    /// Shows health, max health, and damage feedback with smooth animation.
    /// </summary>
    public partial class HealthBarSystem : Control
    {
        [Export] public Color HealthyColor = new Color(0.2f, 0.8f, 0.2f);
        [Export] public Color WarningColor = new Color(1.0f, 0.8f, 0.2f);
        [Export] public Color CriticalColor = new Color(1.0f, 0.2f, 0.2f);
        [Export] public float WarningThreshold = 0.5f;
        [Export] public float CriticalThreshold = 0.25f;
        [Export] public float SmoothSpeed = 10.0f;
        
        // UI Elements
        private ProgressBar _healthBar;
        private ProgressBar _damageBar;  // Shows lost health before shrinking
        private Label _healthText;
        private Label _playerNameLabel;
        private TextureRect _portrait;
        private AnimationPlayer _damageAnim;
        private Panel _damageFlashPanel;
        
        // State
        private float _currentHealth = 100;
        private float _maxHealth = 100;
        private float _displayHealth = 100;
        private float _targetDamageBarValue = 100;
        private double _lastDamageTime = 0;
        private const double DamageBarDelay = 0.5;  // Delay before damage bar shrinks
        
        // Target health from server (for latency requirement)
        private float _serverHealth = 100;
        private double _lastServerUpdateTime = 0;
        private const double MaxHealthUpdateLatency = 0.1;  // 100ms requirement

        public override void _Ready()
        {
            // Get UI references
            _healthBar = GetNode<ProgressBar>("HealthBar");
            _damageBar = GetNodeOrNull<ProgressBar>("DamageBar");
            _healthText = GetNode<Label>("HealthText");
            _playerNameLabel = GetNodeOrNull<Label>("PlayerName");
            _portrait = GetNodeOrNull<TextureRect>("Portrait");
            _damageAnim = GetNodeOrNull<AnimationPlayer>("DamageAnimation");
            _damageFlashPanel = GetNodeOrNull<Panel>("DamageFlash");
            
            // Subscribe to events
            CombatEventSystem.Instance.DamageTaken += OnDamageTaken;
            CombatEventSystem.Instance.EntityDied += OnEntityDied;
            NetworkManager.Instance.EntityStateReceived += OnEntityStateReceived;
            
            // Initial update
            UpdateHealthDisplay();
            UpdateColor();
        }

        public override void _ExitTree()
        {
            if (CombatEventSystem.Instance != null)
            {
                CombatEventSystem.Instance.DamageTaken -= OnDamageTaken;
                CombatEventSystem.Instance.EntityDied -= OnEntityDied;
            }
            if (NetworkManager.Instance != null)
            {
                NetworkManager.Instance.EntityStateReceived -= OnEntityStateReceived;
            }
        }

        public override void _Process(double delta)
        {
            // Smooth health bar animation towards server value
            if (Mathf.Abs(_displayHealth - _serverHealth) > 0.1f)
            {
                _displayHealth = Mathf.Lerp(_displayHealth, _serverHealth, (float)delta * SmoothSpeed);
                UpdateHealthDisplay();
            }
            
            // Update damage bar (delayed shrink effect)
            if (_damageBar != null)
            {
                double timeSinceDamage = Time.GetTicksMsec() / 1000.0 - _lastDamageTime;
                if (timeSinceDamage > DamageBarDelay && _damageBar.Value > _healthBar.Value)
                {
                    float targetValue = _healthBar.Value;
                    _damageBar.Value = Mathf.Lerp((float)_damageBar.Value, targetValue, (float)delta * 5f);
                }
            }
            
            // Check for critical health pulse
            float percent = _displayHealth / _maxHealth;
            if (percent <= CriticalThreshold && _damageAnim != null && !_damageAnim.IsPlaying())
            {
                _damageAnim.Play("critical_pulse");
            }
        }

        /// <summary>
        /// Called when entity state is received from server (for 100ms update requirement)
        /// </summary>
        private void OnEntityStateReceived(uint entityId, Vector3 position, Vector3 velocity)
        {
            if (entityId != GameState.Instance.LocalEntityId) return;
            
            var entity = GameState.Instance.GetEntity(entityId);
            if (entity != null)
            {
                float healthPercent = entity.HealthPercent;
                float actualHealth = healthPercent / 100.0f * _maxHealth;
                
                // Track server update time for latency check
                double now = Time.GetTicksMsec() / 1000.0;
                double updateLatency = now - _lastServerUpdateTime;
                _lastServerUpdateTime = now;
                
                // Verify 100ms requirement
                if (updateLatency > MaxHealthUpdateLatency)
                {
                    GD.PrintVerbose($"[HealthBarSystem] Health update latency: {updateLatency * 1000:F1}ms (target: <100ms)");
                }
                
                SetServerHealth(actualHealth, _maxHealth);
            }
        }

        /// <summary>
        /// Set health directly from server (authoritative)
        /// </summary>
        public void SetServerHealth(float current, float max)
        {
            _serverHealth = current;
            _maxHealth = max;
            
            // Only update current if it's an increase (healing)
            // Damage is handled through event for visual feedback
            if (current > _currentHealth)
            {
                _currentHealth = current;
                _displayHealth = current;
                UpdateHealthDisplay();
            }
            
            UpdateColor();
        }

        /// <summary>
        /// Update health from local prediction (immediate feedback)
        /// </summary>
        public void SetPredictedHealth(float current, float max)
        {
            _currentHealth = current;
            _maxHealth = max;
            // Don't update _serverHealth - wait for server confirmation
        }

        private void OnDamageTaken(int damage, bool isCritical)
        {
            float oldHealth = _currentHealth;
            _currentHealth = Mathf.Max(0, _currentHealth - damage);
            
            // Update damage bar position
            if (_damageBar != null)
            {
                _damageBar.Value = oldHealth / _maxHealth * 100;
            }
            
            _lastDamageTime = Time.GetTicksMsec() / 1000.0;
            
            // Visual feedback
            FlashDamage();
            UpdateColor();
            
            // Show floating text on self
            if (damage > 0)
            {
                ShowFloatingText($"-{damage}", Colors.Red);
            }
        }

        private void OnEntityDied(uint entityId, uint killerId)
        {
            if (entityId == GameState.Instance.LocalEntityId)
            {
                _currentHealth = 0;
                _displayHealth = 0;
                _serverHealth = 0;
                UpdateHealthDisplay();
                UpdateColor();
            }
        }

        private void UpdateHealthDisplay()
        {
            if (_healthBar == null) return;
            
            float percent = _maxHealth > 0 ? _displayHealth / _maxHealth : 0;
            _healthBar.Value = percent * 100;
            _healthBar.MaxValue = 100;
            
            if (_healthText != null)
            {
                _healthText.Text = $"{Mathf.Round(_displayHealth)}/{Mathf.Round(_maxHealth)}";
            }
        }

        private void UpdateColor()
        {
            if (_healthBar == null) return;
            
            float percent = _displayHealth / _maxHealth;
            
            if (percent <= CriticalThreshold)
            {
                _healthBar.Modulate = CriticalColor;
            }
            else if (percent <= WarningThreshold)
            {
                _healthBar.Modulate = WarningColor;
            }
            else
            {
                _healthBar.Modulate = HealthyColor;
            }
        }

        private void FlashDamage()
        {
            // Play animation if available
            if (_damageAnim != null && _damageAnim.HasAnimation("damage_flash"))
            {
                _damageAnim.Play("damage_flash");
            }
            
            // Flash red overlay
            if (_damageFlashPanel != null)
            {
                _damageFlashPanel.Visible = true;
                _damageFlashPanel.Modulate = new Color(1, 0, 0, 0.3f);
                
                var tween = CreateTween();
                tween.TweenProperty(_damageFlashPanel, "modulate:a", 0.0f, 0.3f);
                tween.TweenCallback(Callable.From(() => _damageFlashPanel.Visible = false));
            }
        }

        private void ShowFloatingText(string text, Color color)
        {
            var floating = new Label
            {
                Text = text,
                Modulate = color,
                Position = _healthBar.GlobalPosition + new Vector2(0, -30),
                ThemeTypeVariation = "HeaderLarge"
            };
            
            AddChild(floating);
            
            // Animate up and fade
            var tween = CreateTween();
            tween.SetParallel();
            tween.TweenProperty(floating, "position", floating.Position + new Vector2(0, -50), 1.0f);
            tween.TweenProperty(floating, "modulate:a", 0.0f, 1.0f);
            tween.TweenCallback(Callable.From(() => floating.QueueFree()));
        }

        public void SetPlayerName(string name)
        {
            if (_playerNameLabel != null)
            {
                _playerNameLabel.Text = name;
            }
        }
        
        /// <summary>
        /// Get current health percentage (0-1)
        /// </summary>
        public float GetHealthPercent()
        {
            return _maxHealth > 0 ? _displayHealth / _maxHealth : 0;
        }
        
        /// <summary>
        /// Check if health update is within 100ms requirement
        /// </summary>
        public bool IsHealthUpdateTimely()
        {
            double timeSinceUpdate = Time.GetTicksMsec() / 1000.0 - _lastServerUpdateTime;
            return timeSinceUpdate <= MaxHealthUpdateLatency;
        }
    }
}
