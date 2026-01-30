using Godot;
using System;
using System.Collections.Generic;
using DarkAges.Networking;

namespace DarkAges.Client.UI
{
    /// <summary>
    /// [CLIENT_AGENT] WP-7-4 Ability bar with server-synchronized cooldown tracking.
    /// Displays abilities 1-8 with visual cooldown overlays.
    /// </summary>
    public partial class AbilityBar : HBoxContainer
    {
        [Export] public int AbilityCount = 8;
        [Export] public Vector2 SlotSize = new Vector2(64, 64);
        
        private class AbilitySlot
        {
            public Button Button;
            public TextureRect Icon;
            public TextureProgressBar CooldownOverlay;
            public Label KeybindLabel;
            public Label CooldownText;
            public string AbilityName;
            public float CooldownDuration;
            public double ServerCooldownEndTime;  // Server-authoritative end time
            public bool IsOnCooldown;
        }
        
        private List<AbilitySlot> _slots = new List<AbilitySlot>();
        private string[] _keybinds = { "1", "2", "3", "4", "5", "6", "7", "8" };
        
        // Ability definitions (can be loaded from config)
        private readonly string[] _defaultAbilityNames = {
            "Attack", "Block", "Heal", "Sprint",
            "Shield Slam", "Whirlwind", "Berserker", "Ultimate"
        };
        
        private readonly float[] _defaultCooldowns = { 0.5f, 0.5f, 5.0f, 10.0f, 8.0f, 12.0f, 15.0f, 30.0f };
        
        // Server time tracking for accurate cooldowns
        private double _serverTimeOffset = 0;

        public override void _Ready()
        {
            for (int i = 0; i < AbilityCount; i++)
            {
                CreateAbilitySlot(i);
            }
            
            // Connect to network events
            NetworkManager.Instance.CombatEventReceived += OnCombatEventReceived;
        }

        public override void _ExitTree()
        {
            if (NetworkManager.Instance != null)
            {
                NetworkManager.Instance.CombatEventReceived -= OnCombatEventReceived;
            }
        }
        
        private void CreateAbilitySlot(int index)
        {
            var slot = new AbilitySlot
            {
                AbilityName = _defaultAbilityNames[index],
                CooldownDuration = _defaultCooldowns[index]
            };
            
            // Container for this slot
            var container = new VBoxContainer
            {
                Name = $"Slot_{index}"
            };
            AddChild(container);
            
            // Button background
            slot.Button = new Button
            {
                CustomMinimumSize = SlotSize,
                FocusMode = FocusModeEnum.None,
                ToggleMode = false
            };
            container.AddChild(slot.Button);
            
            // Icon (centered in button)
            slot.Icon = new TextureRect
            {
                StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered,
                AnchorsPreset = (int)LayoutPreset.FullRect,
                SizeFlagsHorizontal = SizeFlags.ExpandFill,
                SizeFlagsVertical = SizeFlags.ExpandFill
            };
            slot.Button.AddChild(slot.Icon);
            
            // Cooldown overlay (fills from top to bottom as cooldown progresses)
            slot.CooldownOverlay = new TextureProgressBar
            {
                FillMode = TextureProgressBar.FillModeEnum.TopToBottom,
                Value = 0,
                MinValue = 0,
                MaxValue = 100,
                Visible = false,
                AnchorsPreset = (int)LayoutPreset.FullRect,
                SizeFlagsHorizontal = SizeFlags.ExpandFill,
                SizeFlagsVertical = SizeFlags.ExpandFill,
                Modulate = new Color(0, 0, 0, 0.7f)  // Dark overlay
            };
            slot.Button.AddChild(slot.CooldownOverlay);
            
            // Cooldown text (centered)
            slot.CooldownText = new Label
            {
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
                Visible = false,
                AnchorsPreset = (int)LayoutPreset.Center,
                OffsetLeft = -20,
                OffsetTop = -10,
                OffsetRight = 20,
                OffsetBottom = 10,
                ThemeTypeVariation = "HeaderLarge"
            };
            slot.Button.AddChild(slot.CooldownText);
            
            // Keybind label (below button)
            slot.KeybindLabel = new Label
            {
                Text = _keybinds[index],
                HorizontalAlignment = HorizontalAlignment.Center,
                SizeFlagsHorizontal = SizeFlags.ExpandFill
            };
            container.AddChild(slot.KeybindLabel);
            
            // Connect input
            int slotIndex = index; // Capture for lambda
            slot.Button.Pressed += () => OnAbilityPressed(slotIndex);
            
            _slots.Add(slot);
        }

        public override void _Process(double delta)
        {
            double currentTime = Time.GetTicksMsec() / 1000.0;
            
            // Update cooldown timers based on server time
            foreach (var slot in _slots)
            {
                if (slot.IsOnCooldown)
                {
                    double remaining = slot.ServerCooldownEndTime - currentTime;
                    
                    if (remaining <= 0)
                    {
                        // Cooldown complete
                        slot.IsOnCooldown = false;
                        slot.CooldownOverlay.Visible = false;
                        slot.CooldownText.Visible = false;
                        slot.Button.Disabled = false;
                        slot.CooldownOverlay.Value = 0;
                    }
                    else
                    {
                        // Update visual
                        float percent = (float)(remaining / slot.CooldownDuration);
                        slot.CooldownOverlay.Value = percent * 100;
                        slot.CooldownText.Text = remaining > 1 ? $"{remaining:F0}" : $"{remaining:F1}";
                        
                        // Change color based on remaining time
                        if (remaining < 1)
                        {
                            slot.CooldownText.Modulate = Colors.Green;  // Almost ready
                        }
                        else
                        {
                            slot.CooldownText.Modulate = Colors.White;
                        }
                    }
                }
            }
        }
        
        private void OnCombatEventReceived(uint eventType, byte[] data)
        {
            // Event type 6 = Ability cooldown update from server
            if (eventType == 6)
            {
                ParseCooldownUpdate(data);
            }
        }
        
        /// <summary>
        /// Parse server cooldown update for accurate synchronization
        /// Format: [ability_id:1][cooldown_duration:4][remaining:4]
        /// </summary>
        private void ParseCooldownUpdate(byte[] data)
        {
            if (data.Length < 9) return;
            
            byte abilityId = data[0];
            float cooldownDuration = BitConverter.ToSingle(data, 1);
            float remainingTime = BitConverter.ToSingle(data, 5);
            
            if (abilityId < _slots.Count)
            {
                UpdateServerCooldown(abilityId, cooldownDuration, remainingTime);
            }
        }
        
        /// <summary>
        /// Update cooldown from server (authoritative)
        /// </summary>
        public void UpdateServerCooldown(byte abilityId, float cooldownDuration, float remainingTime)
        {
            if (abilityId >= _slots.Count) return;
            
            var slot = _slots[abilityId];
            double currentTime = Time.GetTicksMsec() / 1000.0;
            
            // Set server-authoritative end time
            slot.CooldownDuration = cooldownDuration;
            slot.ServerCooldownEndTime = currentTime + remainingTime;
            slot.IsOnCooldown = true;
            slot.Button.Disabled = true;
            slot.CooldownOverlay.Visible = true;
            slot.CooldownText.Visible = true;
            
            GD.PrintVerbose($"[AbilityBar] Server cooldown update: ability={abilityId}, duration={cooldownDuration:F2}, remaining={remainingTime:F2}");
        }

        private void OnAbilityPressed(int slotIndex)
        {
            var slot = _slots[slotIndex];
            
            if (slot.IsOnCooldown) return;
            
            // Send to server
            SendAbilityActivation(slotIndex);
            
            // Start predicted cooldown (will be corrected by server if needed)
            StartLocalCooldown(slotIndex, slot.CooldownDuration);
            
            GD.Print($"[AbilityBar] Activated ability {slotIndex}: {slot.AbilityName}");
        }
        
        /// <summary>
        /// Start local cooldown prediction
        /// </summary>
        public void StartLocalCooldown(int slotIndex, float duration)
        {
            if (slotIndex < 0 || slotIndex >= _slots.Count) return;
            
            var slot = _slots[slotIndex];
            double currentTime = Time.GetTicksMsec() / 1000.0;
            
            slot.IsOnCooldown = true;
            slot.CooldownDuration = duration;
            slot.ServerCooldownEndTime = currentTime + duration;
            slot.CooldownOverlay.Visible = true;
            slot.CooldownText.Visible = true;
            slot.Button.Disabled = true;
            
            // Visual feedback
            var tween = CreateTween();
            tween.TweenProperty(slot.Button, "scale", new Vector2(0.9f, 0.9f), 0.05f);
            tween.TweenProperty(slot.Button, "scale", Vector2.One, 0.05f);
        }
        
        /// <summary>
        /// Send ability activation to server
        /// </summary>
        private void SendAbilityActivation(int slotIndex)
        {
            // Format: [packet_type:1][ability_id:1][target_id:4]
            var data = new byte[6];
            data[0] = 10;  // PACKET_ABILITY_ACTIVATION
            data[1] = (byte)slotIndex;
            BitConverter.GetBytes((uint)0).CopyTo(data, 2);  // Target (0 = current target)
            
            // Send via NetworkManager
            NetworkManager.Instance.CallDeferred("SendReliable", data);
        }

        public override void _Input(InputEvent @event)
        {
            // Keybind shortcuts 1-8
            for (int i = 0; i < _keybinds.Length && i < _slots.Count; i++)
            {
                if (@event.IsActionPressed($"ability_{i + 1}"))
                {
                    OnAbilityPressed(i);
                }
            }
        }
        
        /// <summary>
        /// Set ability icon (can be called to customize abilities)
        /// </summary>
        public void SetAbilityIcon(int slotIndex, Texture2D icon)
        {
            if (slotIndex >= 0 && slotIndex < _slots.Count)
            {
                _slots[slotIndex].Icon.Texture = icon;
            }
        }
        
        /// <summary>
        /// Set ability name for tooltip
        /// </summary>
        public void SetAbilityName(int slotIndex, string name)
        {
            if (slotIndex >= 0 && slotIndex < _slots.Count)
            {
                _slots[slotIndex].AbilityName = name;
                _slots[slotIndex].Button.TooltipText = name;
            }
        }
        
        /// <summary>
        /// Set ability cooldown duration
        /// </summary>
        public void SetAbilityCooldown(int slotIndex, float cooldown)
        {
            if (slotIndex >= 0 && slotIndex < _slots.Count)
            {
                _slots[slotIndex].CooldownDuration = cooldown;
            }
        }
        
        /// <summary>
        /// Check if an ability is ready to use
        /// </summary>
        public bool IsAbilityReady(int slotIndex)
        {
            if (slotIndex < 0 || slotIndex >= _slots.Count) return false;
            return !_slots[slotIndex].IsOnCooldown;
        }
        
        /// <summary>
        /// Get remaining cooldown for an ability
        /// </summary>
        public float GetRemainingCooldown(int slotIndex)
        {
            if (slotIndex < 0 || slotIndex >= _slots.Count) return 0;
            
            var slot = _slots[slotIndex];
            if (!slot.IsOnCooldown) return 0;
            
            double currentTime = Time.GetTicksMsec() / 1000.0;
            return (float)Math.Max(0, slot.ServerCooldownEndTime - currentTime);
        }
    }
}
