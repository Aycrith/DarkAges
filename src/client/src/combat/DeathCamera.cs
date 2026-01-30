using Godot;
using System;
using DarkAges.Networking;

namespace DarkAges.Combat
{
    /// <summary>
    /// [CLIENT_AGENT] Death camera showing killer and respawn timer
    /// </summary>
    public partial class DeathCamera : Control
    {
        [Export] public float RespawnTime = 5.0f;
        
        private Label? killerLabel;
        private Label? timerLabel;
        private Button? respawnButton;
        private float remainingTime;
        private bool isActive = false;

        public override void _Ready()
        {
            killerLabel = GetNode<Label>("Panel/KillerLabel");
            timerLabel = GetNode<Label>("Panel/TimerLabel");
            respawnButton = GetNode<Button>("Panel/RespawnButton");
            respawnButton.Pressed += OnRespawnPressed;
            
            Hide();
        }

        public void Activate(string killerName)
        {
            isActive = true;
            remainingTime = RespawnTime;
            
            killerLabel!.Text = $"Killed by: {killerName}";
            respawnButton!.Disabled = true;
            
            Show();
            
            // Disable player input
            Input.MouseMode = Input.MouseModeEnum.Visible;
        }

        public void Deactivate()
        {
            isActive = false;
            Hide();
            
            // Re-enable player input
            Input.MouseMode = Input.MouseModeEnum.Captured;
        }

        public override void _Process(double delta)
        {
            if (!isActive) return;
            
            remainingTime -= (float)delta;
            if (remainingTime <= 0)
            {
                remainingTime = 0;
                respawnButton!.Disabled = false;
            }
            
            timerLabel!.Text = $"Respawn in: {remainingTime:F1}s";
        }

        private void OnRespawnPressed()
        {
            // Send respawn request to server
            NetworkManager.Instance?.SendRespawnRequest();
            Deactivate();
        }
    }
}
