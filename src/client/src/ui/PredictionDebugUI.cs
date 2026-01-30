using Godot;
using System;

namespace DarkAges.UI
{
    /// <summary>
    /// [CLIENT_AGENT] Debug UI for client-side prediction
    /// Displays prediction error, buffer size, reconciliation count, and RTT
    /// </summary>
    public partial class PredictionDebugUI : Control
    {
        [Export] public Color NormalColor = new Color(0, 1, 0);      // Green
        [Export] public Color WarningColor = new Color(1, 1, 0);     // Yellow
        [Export] public Color ErrorColor = new Color(1, 0, 0);       // Red
        
        [Export] public float WarningThreshold = 0.5f;   // Meters
        [Export] public float ErrorThreshold = 1.0f;     // Meters
        
        private Label _errorLabel;
        private Label _bufferLabel;
        private Label _reconLabel;
        private Label _rttLabel;
        private Label _statusLabel;
        private ProgressBar _errorBar;
        
        private PredictedPlayer _predictedPlayer;
        private float _maxError = 0.0f;
        private float _errorDecayTimer = 0.0f;

        public override void _Ready()
        {
            // Create UI elements if not present in scene
            SetupUI();
            
            // Find predicted player
            _predictedPlayer = GetNode<PredictedPlayer>("/root/Main/Player");
            if (_predictedPlayer != null)
            {
                // Connect to signals
                _predictedPlayer.PredictionError += OnPredictionError;
                _predictedPlayer.Reconciliation += OnReconciliation;
            }
            
            GD.Print("[PredictionDebugUI] Initialized");
        }

        public override void _Process(double delta)
        {
            UpdateDisplay();
            
            // Decay max error display
            _errorDecayTimer += (float)delta;
            if (_errorDecayTimer > 2.0f)
            {
                _maxError = 0.0f;
                _errorDecayTimer = 0.0f;
            }
        }

        private void SetupUI()
        {
            // Check if labels already exist (set in editor)
            _errorLabel = GetNodeOrNull<Label>("ErrorLabel");
            _bufferLabel = GetNodeOrNull<Label>("BufferLabel");
            _reconLabel = GetNodeOrNull<Label>("ReconLabel");
            _rttLabel = GetNodeOrNull<Label>("RttLabel");
            _statusLabel = GetNodeOrNull<Label>("StatusLabel");
            _errorBar = GetNodeOrNull<ProgressBar>("ErrorBar");
            
            // Create default layout if not set up in editor
            if (_errorLabel == null)
            {
                CreateDefaultLayout();
            }
        }

        private void CreateDefaultLayout()
        {
            // Container
            var panel = new Panel();
            panel.SetAnchorsPreset(LayoutPreset.TopRight);
            panel.Position = new Vector2(-210, 10);
            panel.Size = new Vector2(200, 150);
            AddChild(panel);
            
            var vbox = new VBoxContainer();
            vbox.SetAnchorsPreset(LayoutPreset.FullRect);
            vbox.OffsetLeft = 10;
            vbox.OffsetTop = 10;
            vbox.OffsetRight = -10;
            vbox.OffsetBottom = -10;
            panel.AddChild(vbox);
            
            // Title
            var title = new Label();
            title.Text = "Prediction Debug";
            title.HorizontalAlignment = HorizontalAlignment.Center;
            vbox.AddChild(title);
            
            // Error display
            _errorLabel = new Label();
            _errorLabel.Text = "Error: 0.000m";
            vbox.AddChild(_errorLabel);
            
            // Error bar
            _errorBar = new ProgressBar();
            _errorBar.MaxValue = 2.0f;
            _errorBar.Value = 0.0f;
            _errorBar.CustomMinimumSize = new Vector2(0, 10);
            vbox.AddChild(_errorBar);
            
            // Buffer size
            _bufferLabel = new Label();
            _bufferLabel.Text = "Buffer: 0";
            vbox.AddChild(_bufferLabel);
            
            // Reconciliation count
            _reconLabel = new Label();
            _reconLabel.Text = "Recons: 0";
            vbox.AddChild(_reconLabel);
            
            // RTT
            _rttLabel = new Label();
            _rttLabel.Text = "RTT: 0ms";
            vbox.AddChild(_rttLabel);
            
            // Status
            _statusLabel = new Label();
            _statusLabel.Text = "Status: OK";
            vbox.AddChild(_statusLabel);
        }

        private void UpdateDisplay()
        {
            if (GameState.Instance == null) return;
            
            // Get values from GameState
            float error = GameState.Instance.PredictionError;
            int bufferSize = GameState.Instance.InputBufferSize;
            int reconCount = GameState.Instance.ReconciliationCount;
            uint rtt = GameState.Instance.LastRttMs;
            
            // Track max error
            if (error > _maxError)
            {
                _maxError = error;
                _errorDecayTimer = 0.0f;
            }
            
            // Update labels
            if (_errorLabel != null)
            {
                _errorLabel.Text = $"Error: {error:F3}m (max: {_maxError:F3}m)";
                _errorLabel.Modulate = GetErrorColor(error);
            }
            
            if (_errorBar != null)
            {
                _errorBar.Value = error;
                var style = _errorBar.GetThemeStylebox("fill") as StyleBoxFlat;
                if (style != null)
                {
                    style.BgColor = GetErrorColor(error);
                }
            }
            
            if (_bufferLabel != null)
            {
                _bufferLabel.Text = $"Buffer: {bufferSize} inputs";
            }
            
            if (_reconLabel != null)
            {
                _reconLabel.Text = $"Recons: {reconCount}";
            }
            
            if (_rttLabel != null)
            {
                _rttLabel.Text = $"RTT: {rtt}ms";
                _rttLabel.Modulate = rtt < 100 ? NormalColor : (rtt < 200 ? WarningColor : ErrorColor);
            }
            
            if (_statusLabel != null)
            {
                var state = GameState.Instance.CurrentConnectionState;
                string stateStr = state.ToString();
                
                if (error > 2.0f)
                {
                    _statusLabel.Text = "Status: LARGE CORRECTION";
                    _statusLabel.Modulate = ErrorColor;
                }
                else if (error > 0.5f)
                {
                    _statusLabel.Text = "Status: DRIFTING";
                    _statusLabel.Modulate = WarningColor;
                }
                else
                {
                    _statusLabel.Text = $"Status: {stateStr}";
                    _statusLabel.Modulate = NormalColor;
                }
            }
        }

        private Color GetErrorColor(float error)
        {
            if (error >= ErrorThreshold) return ErrorColor;
            if (error >= WarningThreshold) return WarningColor;
            return NormalColor;
        }

        private void OnPredictionError(float error, bool isLarge)
        {
            if (isLarge)
            {
                GD.PrintErr($"[PredictionDebugUI] LARGE PREDICTION ERROR: {error:F2}m");
            }
        }

        private void OnReconciliation(int inputCount, float error)
        {
            GD.Print($"[PredictionDebugUI] Reconciliation: replayed {inputCount} inputs, error was {error:F3}m");
        }

        public override void _ExitTree()
        {
            if (_predictedPlayer != null)
            {
                _predictedPlayer.PredictionError -= OnPredictionError;
                _predictedPlayer.Reconciliation -= OnReconciliation;
            }
        }
    }
}
