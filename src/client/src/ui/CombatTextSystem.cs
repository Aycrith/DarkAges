using Godot;
using System;
using System.Collections.Generic;
using DarkAges.Combat;
using DarkAges.Entities;

namespace DarkAges.Client.UI
{
    /// <summary>
    /// [CLIENT_AGENT] WP-7-4 Floating combat text system for damage numbers and combat feedback.
    /// </summary>
    public partial class CombatTextSystem : Node
    {
        [Export] public int MaxCombatTexts = 50;
        [Export] public float DefaultLifetime = 1.5f;
        [Export] public float CriticalLifetime = 2.0f;
        [Export] public float HealLifetime = 1.5f;
        
        private Queue<CombatText> _combatTextPool = new Queue<CombatText>();
        private List<ActiveCombatText> _activeTexts = new List<ActiveCombatText>();
        
        private class ActiveCombatText
        {
            public CombatText TextNode;
            public double Lifetime;
            public double MaxLifetime;
            public Vector3 StartPosition;
            public Vector3 Velocity;
            public float InitialScale;
        }
        
        // Colors for different text types
        private static readonly Color DamageColor = new Color(1.0f, 0.3f, 0.3f);      // Red
        private static readonly Color CriticalColor = new Color(1.0f, 0.9f, 0.2f);    // Yellow/Gold
        private static readonly Color HealColor = new Color(0.2f, 0.9f, 0.3f);        // Green
        private static readonly Color MissColor = new Color(0.7f, 0.7f, 0.7f);        // Gray
        private static readonly Color BlockColor = new Color(0.5f, 0.5f, 0.8f);       // Blue
        
        public override void _Ready()
        {
            // Subscribe to combat events
            CombatEventSystem.Instance.DamageDealt += OnDamageDealt;
            CombatEventSystem.Instance.DamageTaken += OnDamageTaken;
            CombatEventSystem.Instance.EntityDied += OnEntityDied;
        }

        public override void _ExitTree()
        {
            if (CombatEventSystem.Instance != null)
            {
                CombatEventSystem.Instance.DamageDealt -= OnDamageDealt;
                CombatEventSystem.Instance.DamageTaken -= OnDamageTaken;
                CombatEventSystem.Instance.EntityDied -= OnEntityDied;
            }
        }
        
        /// <summary>
        /// Show damage number at world position
        /// </summary>
        public void ShowDamage(Vector3 worldPosition, int damage, bool isCritical = false, 
                               bool isHeal = false, bool isMiss = false, bool isBlock = false)
        {
            if (_activeTexts.Count >= MaxCombatTexts) return;
            
            var textNode = GetPooledText();
            if (textNode == null) return;
            
            // Configure text based on type
            string text;
            Color color;
            float lifetime;
            float fontSize;
            
            if (isMiss)
            {
                text = "MISS";
                color = MissColor;
                lifetime = DefaultLifetime;
                fontSize = 32;
            }
            else if (isBlock)
            {
                text = "BLOCK";
                color = BlockColor;
                lifetime = DefaultLifetime;
                fontSize = 32;
            }
            else if (isHeal)
            {
                text = $"+{damage}";
                color = HealColor;
                lifetime = HealLifetime;
                fontSize = 36;
            }
            else if (isCritical)
            {
                text = $"{damage}!";
                color = CriticalColor;
                lifetime = CriticalLifetime;
                fontSize = 48;
            }
            else
            {
                text = damage.ToString();
                color = DamageColor;
                lifetime = DefaultLifetime;
                fontSize = 32;
            }
            
            // Set up the combat text node
            textNode.Setup(text, color, fontSize, lifetime);
            
            // Position with slight random offset for variety
            var randomOffset = new Vector3(
                GD.Randf() * 0.6f - 0.3f,
                0,
                GD.Randf() * 0.6f - 0.3f
            );
            textNode.GlobalPosition = worldPosition + randomOffset + Vector3.Up * 0.5f;
            
            // Add to active texts
            var activeText = new ActiveCombatText
            {
                TextNode = textNode,
                Lifetime = 0,
                MaxLifetime = lifetime,
                StartPosition = textNode.GlobalPosition,
                Velocity = new Vector3(0, 2.5f, 0),  // Float up
                InitialScale = isCritical ? 1.2f : 1.0f
            };
            
            _activeTexts.Add(activeText);
            textNode.Visible = true;
            
            // Initial pop animation
            textNode.Scale = Vector3.Zero;
            var tween = CreateTween();
            tween.TweenProperty(textNode, "scale", Vector3.One * activeText.InitialScale, 0.1f)
                 .SetEase(Tween.EaseType.OutBack);
        }
        
        /// <summary>
        /// Show combat text on a specific entity
        /// </summary>
        public void ShowDamageOnEntity(uint entityId, int damage, bool isCritical = false)
        {
            Vector3 position;
            
            // Get entity position
            var entity = GameState.Instance.GetEntity(entityId);
            if (entity != null)
            {
                position = entity.Position;
            }
            else
            {
                // Try to get from remote player manager
                var player = RemotePlayerManager.Instance?.GetPlayer(entityId);
                if (player != null && IsInstanceValid(player))
                {
                    position = player.GlobalPosition;
                }
                else
                {
                    return;  // Can't find entity
                }
            }
            
            ShowDamage(position + Vector3.Up * 1.5f, damage, isCritical);
        }
        
        /// <summary>
        /// Show miss text at position
        /// </summary>
        public void ShowMiss(Vector3 worldPosition)
        {
            ShowDamage(worldPosition, 0, false, false, true);
        }
        
        /// <summary>
        /// Show block text at position
        /// </summary>
        public void ShowBlock(Vector3 worldPosition)
        {
            ShowDamage(worldPosition, 0, false, false, false, true);
        }
        
        /// <summary>
        /// Show heal at position
        /// </summary>
        public void ShowHeal(Vector3 worldPosition, int amount)
        {
            ShowDamage(worldPosition, amount, false, true);
        }

        private void OnDamageDealt(uint targetId, int damage, bool isCritical, Vector3 position)
        {
            ShowDamage(position, damage, isCritical);
        }

        private void OnDamageTaken(int damage, bool isCritical)
        {
            // Show on self (local player)
            var localPlayer = GetTree().GetNodesInGroup("local_player").FirstOrDefault() as Node3D;
            if (localPlayer != null)
            {
                ShowDamage(localPlayer.GlobalPosition + Vector3.Up * 1.5f, damage, isCritical);
            }
        }

        private void OnEntityDied(uint entityId, uint killerId)
        {
            // Show death text above entity
            Vector3 position;
            var player = RemotePlayerManager.Instance?.GetPlayer(entityId);
            if (player != null && IsInstanceValid(player))
            {
                position = player.GlobalPosition;
            }
            else
            {
                var entity = GameState.Instance.GetEntity(entityId);
                if (entity != null)
                {
                    position = entity.Position;
                }
                else
                {
                    return;
                }
            }
            
            // Show "DEATH" text
            ShowDeathText(position + Vector3.Up * 2f);
        }
        
        private void ShowDeathText(Vector3 worldPosition)
        {
            if (_activeTexts.Count >= MaxCombatTexts) return;
            
            var textNode = GetPooledText();
            if (textNode == null) return;
            
            textNode.Setup("DEATH", Colors.DarkRed, 40, 2.0f);
            textNode.GlobalPosition = worldPosition;
            
            var activeText = new ActiveCombatText
            {
                TextNode = textNode,
                Lifetime = 0,
                MaxLifetime = 2.0f,
                StartPosition = textNode.GlobalPosition,
                Velocity = new Vector3(0, 1.5f, 0),
                InitialScale = 1.0f
            };
            
            _activeTexts.Add(activeText);
            textNode.Visible = true;
        }

        private CombatText GetPooledText()
        {
            if (_combatTextPool.Count > 0)
            {
                return _combatTextPool.Dequeue();
            }
            
            // Create new combat text node
            var node = new CombatText();
            GetTree().Root.AddChild(node);
            return node;
        }

        public override void _Process(double delta)
        {
            // Update all active texts
            for (int i = _activeTexts.Count - 1; i >= 0; i--)
            {
                var text = _activeTexts[i];
                text.Lifetime += delta;
                
                // Animate position (float up with slight arc)
                float t = (float)(text.Lifetime / text.MaxLifetime);
                Vector3 currentPos = text.TextNode.GlobalPosition;
                currentPos += text.Velocity * (float)delta * (1 - t * 0.5f);  // Slow down over time
                text.TextNode.GlobalPosition = currentPos;
                
                // Fade out in last 30% of lifetime
                if (t > 0.7f)
                {
                    float fadeAlpha = 1.0f - ((t - 0.7f) / 0.3f);
                    text.TextNode.SetAlpha(fadeAlpha);
                }
                
                // Scale down slightly at end
                float scale = text.InitialScale * (1.0f - t * 0.2f);
                text.TextNode.Scale = Vector3.One * scale;
                
                // Remove if expired
                if (text.Lifetime >= text.MaxLifetime)
                {
                    text.TextNode.Visible = false;
                    text.TextNode.SetAlpha(1.0f);
                    _combatTextPool.Enqueue(text.TextNode);
                    _activeTexts.RemoveAt(i);
                }
            }
        }
        
        /// <summary>
        /// Clear all active combat texts
        /// </summary>
        public void ClearAll()
        {
            foreach (var text in _activeTexts)
            {
                text.TextNode.Visible = false;
                text.TextNode.SetAlpha(1.0f);
                _combatTextPool.Enqueue(text.TextNode);
            }
            _activeTexts.Clear();
        }
    }
    
    /// <summary>
    /// Individual combat text node (Label3D with customization)
    /// </summary>
    public partial class CombatText : Label3D
    {
        public void Setup(string text, Color color, float fontSize, float lifetime)
        {
            Text = text;
            Modulate = color;
            FontSize = (int)fontSize;
            Billboard = BaseMaterial3D.BillboardModeEnum.Enabled;
            PixelSize = 0.015f;
            OutlineSize = 2;
            OutlineColor = Colors.Black;
            HorizontalAlignment = HorizontalAlignment.Center;
        }
        
        public void SetAlpha(float alpha)
        {
            Modulate = new Color(Modulate.R, Modulate.G, Modulate.B, alpha);
        }
    }
}
