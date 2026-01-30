using Godot;
using System;
using DarkAges.Combat;
using DarkAges.Networking;

namespace DarkAges.Client.UI
{
    /// <summary>
    /// [CLIENT_AGENT] WP-7-4 Death and respawn UI overlay.
    /// Shows death screen with killer info and respawn timer.
    /// </summary>
    public partial class DeathRespawnUI : Control
    {
        [Export] public float RespawnDelay = 5.0f;  // Seconds before can respawn
        [Export] public float CameraOrbitSpeed = 30.0f;  // Degrees per second
        
        // UI Elements
        private Panel _deathPanel;
        private Label _deathTitleLabel;
        private Label _killerLabel;
        private Label _respawnTimerLabel;
        private Button _respawnButton;
        private ProgressBar _respawnProgress;
        
        // Death camera
        private Camera3D _deathCamera;
        private Node3D _cameraPivot;
        private Node3D _killerFocus;
        
        // State
        private bool _isDead = false;
        private double _deathTime = 0;
        private double _respawnAvailableTime = 0;
        private uint _killerId = 0;
        private string _killerName = "";
        
        // References
        private Camera3D _originalCamera;
        private Node3D _localPlayer;

        public override void _Ready()
        {
            CreateUI();
            CreateDeathCamera();
            
            // Subscribe to events
            CombatEventSystem.Instance.LocalPlayerDied += OnLocalPlayerDied;
            CombatEventSystem.Instance.EntityDied += OnEntityDied;
            
            // Initially hidden
            HideDeathUI();
        }

        public override void _ExitTree()
        {
            if (CombatEventSystem.Instance != null)
            {
                CombatEventSystem.Instance.LocalPlayerDied -= OnLocalPlayerDied;
                CombatEventSystem.Instance.EntityDied -= OnEntityDied;
            }
        }

        private void CreateUI()
        {
            // Main death panel (full screen overlay)
            _deathPanel = new Panel
            {
                Name = "DeathPanel",
                Visible = false,
                AnchorsPreset = (int)LayoutPreset.FullRect,
                Modulate = new Color(0, 0, 0, 0.7f)
            };
            AddChild(_deathPanel);
            
            // Center container
            var centerContainer = new CenterContainer
            {
                AnchorsPreset = (int)LayoutPreset.FullRect
            };
            _deathPanel.AddChild(centerContainer);
            
            // Content container
            var vbox = new VBoxContainer
            {
                Alignment = BoxContainer.AlignmentMode.Center,
                Separation = 20
            };
            centerContainer.AddChild(vbox);
            
            // Death title
            _deathTitleLabel = new Label
            {
                Text = "YOU DIED",
                ThemeTypeVariation = "HeaderLarge",
                HorizontalAlignment = HorizontalAlignment.Center
            };
            _deathTitleLabel.AddThemeFontSizeOverride("font_size", 72);
            _deathTitleLabel.AddThemeColorOverride("font_color", Colors.DarkRed);
            vbox.AddChild(_deathTitleLabel);
            
            // Killer info
            _killerLabel = new Label
            {
                Text = "",
                HorizontalAlignment = HorizontalAlignment.Center
            };
            _killerLabel.AddThemeFontSizeOverride("font_size", 32);
            vbox.AddChild(_killerLabel);
            
            // Separator
            vbox.AddChild(new HSeparator { CustomMinimumSize = new Vector2(0, 20) });
            
            // Respawn timer
            _respawnTimerLabel = new Label
            {
                Text = "Respawn in: 5",
                HorizontalAlignment = HorizontalAlignment.Center
            };
            _respawnTimerLabel.AddThemeFontSizeOverride("font_size", 28);
            vbox.AddChild(_respawnTimerLabel);
            
            // Respawn progress bar
            _respawnProgress = new ProgressBar
            {
                CustomMinimumSize = new Vector2(300, 20),
                MaxValue = RespawnDelay,
                Value = 0
            };
            vbox.AddChild(_respawnProgress);
            
            // Respawn button (initially disabled)
            _respawnButton = new Button
            {
                Text = "RESPAWN",
                Disabled = true,
                CustomMinimumSize = new Vector2(200, 50)
            };
            _respawnButton.AddThemeFontSizeOverride("font_size", 24);
            _respawnButton.Pressed += OnRespawnButtonPressed;
            vbox.AddChild(_respawnButton);
            
            // Damage recap / killed by info
            var infoLabel = new Label
            {
                Text = "Waiting for respawn...",
                HorizontalAlignment = HorizontalAlignment.Center
            };
            infoLabel.AddThemeFontSizeOverride("font_size", 18);
            infoLabel.AddThemeColorOverride("font_color", Colors.Gray);
            vbox.AddChild(infoLabel);
        }
        
        private void CreateDeathCamera()
        {
            // Create death camera setup
            _cameraPivot = new Node3D { Name = "DeathCameraPivot" };
            _deathCamera = new Camera3D { Name = "DeathCamera" };
            
            _cameraPivot.AddChild(_deathCamera);
            GetTree().Root.AddChild(_cameraPivot);
            
            // Position camera back and up from pivot
            _deathCamera.Position = new Vector3(0, 3, 8);
            _deathCamera.LookAt(Vector3.Zero);
            
            // Initially disabled
            _cameraPivot.Visible = false;
        }

        public override void _Process(double delta)
        {
            if (!_isDead) return;
            
            double currentTime = Time.GetTicksMsec() / 1000.0;
            double timeSinceDeath = currentTime - _deathTime;
            double timeUntilRespawn = _respawnAvailableTime - currentTime;
            
            // Update timer display
            if (timeUntilRespawn > 0)
            {
                _respawnTimerLabel.Text = $"Respawn in: {Mathf.Ceil((float)timeUntilRespawn)}";
                _respawnProgress.Value = RespawnDelay - timeUntilRespawn;
                _respawnButton.Disabled = true;
                _respawnButton.Text = "RESPAWN";
            }
            else
            {
                _respawnTimerLabel.Text = "Ready to respawn!";
                _respawnProgress.Value = RespawnDelay;
                _respawnButton.Disabled = false;
                _respawnButton.Text = "PRESS TO RESPAWN";
            }
            
            // Orbit camera around death position or killer
            UpdateDeathCamera(delta);
        }
        
        private void UpdateDeathCamera(double delta)
        {
            if (_cameraPivot == null) return;
            
            // Orbit around the killer or death position
            _cameraPivot.RotateY((float)delta * Mathf.DegToRad(CameraOrbitSpeed));
            
            // If we have a killer, keep looking at them
            if (_killerFocus != null && IsInstanceValid(_killerFocus))
            {
                _deathCamera.LookAt(_killerFocus.GlobalPosition);
            }
        }

        private void OnLocalPlayerDied(uint killerId)
        {
            _killerId = killerId;
            _killerName = GameState.Instance.GetEntity(killerId)?.Name ?? "Unknown";
            
            ActivateDeathCam();
        }
        
        private void OnEntityDied(uint victimId, uint killerId)
        {
            // Track if killer died so we stop focusing on them
            if (victimId == _killerId && _isDead)
            {
                _killerFocus = null;
            }
        }

        private void ActivateDeathCam()
        {
            if (_isDead) return;  // Already dead
            
            _isDead = true;
            _deathTime = Time.GetTicksMsec() / 1000.0;
            _respawnAvailableTime = _deathTime + RespawnDelay;
            
            // Get local player position for camera
            _localPlayer = GetTree().GetNodesInGroup("local_player").FirstOrDefault() as Node3D;
            if (_localPlayer != null)
            {
                _cameraPivot.GlobalPosition = _localPlayer.GlobalPosition;
            }
            
            // Try to find killer for camera focus
            var killerPlayer = RemotePlayerManager.Instance?.GetPlayer(_killerId);
            if (killerPlayer != null && IsInstanceValid(killerPlayer))
            {
                _killerFocus = killerPlayer;
            }
            
            // Store original camera and switch to death cam
            var viewport = GetViewport();
            _originalCamera = viewport.GetCamera3D();
            _deathCamera.Current = true;
            _cameraPivot.Visible = true;
            
            // Show death UI
            ShowDeathUI();
            
            // Disable player controls
            SetPlayerControlsEnabled(false);
            
            GD.Print($"[DeathRespawnUI] Player died. Killer: {_killerName} ({_killerId})");
        }
        
        private void DeactivateDeathCam()
        {
            if (!_isDead) return;
            
            _isDead = false;
            
            // Restore original camera
            if (_originalCamera != null && IsInstanceValid(_originalCamera))
            {
                _originalCamera.Current = true;
            }
            _deathCamera.Current = false;
            _cameraPivot.Visible = false;
            _killerFocus = null;
            
            // Hide death UI
            HideDeathUI();
            
            // Re-enable player controls
            SetPlayerControlsEnabled(true);
            
            GD.Print("[DeathRespawnUI] Player respawned");
        }
        
        private void ShowDeathUI()
        {
            _deathPanel.Visible = true;
            _killerLabel.Text = $"Killed by: {_killerName}";
            _respawnProgress.Value = 0;
            
            // Animation for dramatic effect
            var tween = CreateTween();
            tween.TweenProperty(_deathTitleLabel, "scale", new Vector2(1.2f, 1.2f), 0.3f)
                 .SetEase(Tween.EaseType.OutBack);
            tween.TweenProperty(_deathTitleLabel, "scale", Vector2.One, 0.2f);
        }
        
        private void HideDeathUI()
        {
            _deathPanel.Visible = false;
        }
        
        private void OnRespawnButtonPressed()
        {
            if (_isDead && Time.GetTicksMsec() / 1000.0 >= _respawnAvailableTime)
            {
                RequestRespawn();
            }
        }
        
        private void RequestRespawn()
        {
            // Send respawn request to server
            NetworkManager.Instance.SendRespawnRequest();
            
            // Deactivate death cam (server will confirm respawn)
            DeactivateDeathCam();
            
            GD.Print("[DeathRespawnUI] Respawn requested");
        }
        
        private void SetPlayerControlsEnabled(bool enabled)
        {
            // Disable/enable player input processing
            if (_localPlayer != null)
            {
                _localPlayer.SetProcessInput(enabled);
                _localPlayer.SetProcessUnhandledInput(enabled);
            }
            
            // Could also pause physics processing
            if (_localPlayer is CharacterBody3D body)
            {
                body.SetPhysicsProcess(enabled);
            }
        }
        
        /// <summary>
        /// Check if player is currently dead
        /// </summary>
        public bool IsDead => _isDead;
        
        /// <summary>
        /// Get remaining respawn time
        /// </summary>
        public double GetRemainingRespawnTime()
        {
            if (!_isDead) return 0;
            double remaining = _respawnAvailableTime - Time.GetTicksMsec() / 1000.0;
            return Math.Max(0, remaining);
        }
    }
}
