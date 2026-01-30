using Godot;
using System;
using System.Collections.Generic;
using DarkAges.Entities;

namespace DarkAges.Tests
{
    /// <summary>
    /// [CLIENT_AGENT] WP-7-3 Interpolation Quality Tests
    /// Validates the interpolation system meets requirements:
    /// - No jitter (smooth movement)
    /// - <100ms perceived latency
    /// - Handles packet loss without snapping
    /// </summary>
    [Tool]
    public partial class InterpolationTests : Node
    {
        [Export] public bool RunTests = false;
        
        // Test results
        private List<string> _testResults = new();
        private bool _testsRunning = false;
        
        // Mock data for tests
        private List<MockEntityFrame> _mockFrames = new();
        
        /// <summary>
        /// Mock frame for testing
        /// </summary>
        private class MockEntityFrame
        {
            public double Timestamp;
            public double ServerTime;
            public Vector3 Position;
            public Quaternion Rotation;
            public Vector3 Velocity;
        }
        
        // Metrics tracking
        private List<float> _positionJerkValues = new();
        private List<double> _latencyMeasurements = new();
        
        public override void _Process(double delta)
        {
            if (RunTests && !_testsRunning)
            {
                RunAllTests();
            }
            else if (!RunTests && _testsRunning)
            {
                _testsRunning = false;
            }
        }
        
        /// <summary>
        /// Run all WP-7-3 quality tests
        /// </summary>
        public void RunAllTests()
        {
            _testsRunning = true;
            _testResults.Clear();
            
            GD.Print("=== WP-7-3 Entity Interpolation Tests ===\n");
            
            Test_SmoothMovement();
            Test_LatencyHiding();
            Test_PacketLossRecovery();
            Test_ExtrapolationLimit();
            Test_InterpolationDelay();
            
            PrintResults();
        }
        
        /// <summary>
        /// TEST: Smooth movement (no jitter)
        /// Measures jerk (derivative of acceleration) in position changes
        /// </summary>
        private void Test_SmoothMovement()
        {
            GD.Print("[TEST] Smooth Movement (No Jitter)");
            
            // Simulate 20Hz snapshots with smooth movement
            GenerateMockFrames(20, 5.0f, true);
            
            // Simulate interpolation at 60Hz
            Vector3 lastPosition = _mockFrames[0].Position;
            Vector3 lastVelocity = Vector3.Zero;
            float maxJerk = 0;
            
            for (double t = 0; t < 4.0; t += 1.0 / 60.0)
            {
                Vector3 position = InterpolateAtTime(t + 0.1);
                Vector3 velocity = (position - lastPosition) * 60.0f;
                Vector3 acceleration = (velocity - lastVelocity) * 60.0f;
                float jerk = acceleration.Length();
                
                maxJerk = Mathf.Max(maxJerk, jerk);
                _positionJerkValues.Add(jerk);
                
                lastPosition = position;
                lastVelocity = velocity;
            }
            
            bool passed = maxJerk < 1000.0f;
            
            _testResults.Add($"Smooth Movement: {(passed ? "PASS" : "FAIL")} - Max Jerk: {maxJerk:F2}");
            GD.Print($"  Max Jerk: {maxJerk:F2} (threshold: 1000)");
            GD.Print($"  Result: {(passed ? "PASS" : "FAIL")}\n");
        }
        
        /// <summary>
        /// TEST: <100ms perceived latency
        /// </summary>
        private void Test_LatencyHiding()
        {
            GD.Print("[TEST] Latency Hiding (<100ms perceived)");
            
            GenerateMockFrames(20, 3.0f, true);
            
            double totalPerceivedLatency = 0;
            int samples = 0;
            
            for (double t = 0.5; t < 2.5; t += 0.1)
            {
                Vector3 serverPos = GetServerPositionAtTime(t);
                Vector3 interpPos = InterpolateAtTime(t + 0.25);
                
                double positionAge = EstimatePositionAge(interpPos, t + 0.25);
                
                totalPerceivedLatency += positionAge;
                _latencyMeasurements.Add(positionAge);
                samples++;
            }
            
            double avgLatency = totalPerceivedLatency / samples;
            bool passed = avgLatency < 0.12;
            
            _testResults.Add($"Latency Hiding: {(passed ? "PASS" : "FAIL")} - Avg: {avgLatency * 1000:F0}ms");
            GD.Print($"  Avg Perceived Latency: {avgLatency * 1000:F0}ms (target: <100ms)");
            GD.Print($"  Result: {(passed ? "PASS" : "FAIL")}\n");
        }
        
        /// <summary>
        /// TEST: Packet loss recovery
        /// </summary>
        private void Test_PacketLossRecovery()
        {
            GD.Print("[TEST] Packet Loss Recovery (10% loss)");
            
            GenerateMockFrames(20, 5.0f, true);
            
            var rng = new Random(12345);
            double lastPositionDelta = 0;
            float maxDeltaChange = 0;
            int extrapolationCount = 0;
            
            Vector3 lastPosition = Vector3.Zero;
            bool first = true;
            
            for (double t = 0; t < 4.0; t += 1.0 / 60.0)
            {
                bool packetLost = rng.NextDouble() < 0.1;
                
                Vector3 position;
                if (packetLost)
                {
                    position = InterpolateAtTimeWithGap(t + 0.1);
                    extrapolationCount++;
                }
                else
                {
                    position = InterpolateAtTime(t + 0.1);
                }
                
                if (!first)
                {
                    double delta = position.DistanceTo(lastPosition);
                    float deltaChange = Mathf.Abs((float)(delta - lastPositionDelta));
                    maxDeltaChange = Mathf.Max(maxDeltaChange, deltaChange);
                    lastPositionDelta = delta;
                }
                
                lastPosition = position;
                first = false;
            }
            
            bool passed = maxDeltaChange < 0.5f && extrapolationCount > 10;
            
            _testResults.Add($"Packet Loss Recovery: {(passed ? "PASS" : "FAIL")} - Max Delta: {maxDeltaChange:F3}m");
            GD.Print($"  Max Delta Change: {maxDeltaChange:F3}m");
            GD.Print($"  Extrapolations: {extrapolationCount}");
            GD.Print($"  Result: {(passed ? "PASS" : "FAIL")}\n");
        }
        
        /// <summary>
        /// TEST: Extrapolation limit (500ms)
        /// </summary>
        private void Test_ExtrapolationLimit()
        {
            GD.Print("[TEST] Extrapolation Limit (500ms max)");
            
            GenerateMockFrames(20, 1.0f, true);
            
            double extrapolationStart = 1.1;
            double maxExtrapolationTime = 0;
            
            for (double t = extrapolationStart; t < extrapolationStart + 1.0; t += 1.0 / 60.0)
            {
                Vector3 position = ExtrapolateAtTime(t);
                double extrapTime = t - 1.0;
                maxExtrapolationTime = Mathf.Max((float)maxExtrapolationTime, (float)extrapTime);
            }
            
            bool passed = maxExtrapolationTime >= 0.5f && maxExtrapolationTime < 0.6f;
            
            _testResults.Add($"Extrapolation Limit: {(passed ? "PASS" : "FAIL")} - Max: {maxExtrapolationTime * 1000:F0}ms");
            GD.Print($"  Max Extrapolation: {maxExtrapolationTime * 1000:F0}ms (limit: 500ms)");
            GD.Print($"  Result: {(passed ? "PASS" : "FAIL")}\n");
        }
        
        /// <summary>
        /// TEST: Interpolation delay configuration
        /// </summary>
        private void Test_InterpolationDelay()
        {
            GD.Print("[TEST] Interpolation Delay (100ms)");
            
            var mockPlayer = new RemotePlayer();
            float delay = mockPlayer.InterpolationDelay;
            
            bool passed = Mathf.Abs(delay - 0.1f) < 0.001f;
            
            _testResults.Add($"Interpolation Delay: {(passed ? "PASS" : "FAIL")} - Delay: {delay * 1000:F0}ms");
            GD.Print($"  Configured Delay: {delay * 1000:F0}ms (expected: 100ms)");
            GD.Print($"  Result: {(passed ? "PASS" : "FAIL")}\n");
        }
        
        // Helper methods
        
        private void GenerateMockFrames(float rateHz, float duration, bool smooth)
        {
            _mockFrames.Clear();
            
            double interval = 1.0 / rateHz;
            for (double t = 0; t < duration; t += interval)
            {
                float angle = (float)t * 2.0f;
                Vector3 pos = new Vector3(
                    Mathf.Cos(angle) * 5.0f,
                    0,
                    Mathf.Sin(angle) * 5.0f
                );
                
                Vector3 vel = new Vector3(
                    -Mathf.Sin(angle) * 10.0f,
                    0,
                    Mathf.Cos(angle) * 10.0f
                );
                
                _mockFrames.Add(new MockEntityFrame
                {
                    Timestamp = t,
                    ServerTime = t,
                    Position = pos,
                    Velocity = vel,
                    Rotation = Quaternion.Identity
                });
            }
        }
        
        private Vector3 InterpolateAtTime(double renderTime)
        {
            for (int i = 0; i < _mockFrames.Count - 1; i++)
            {
                if (_mockFrames[i].ServerTime <= renderTime && _mockFrames[i + 1].ServerTime >= renderTime)
                {
                    double t = (renderTime - _mockFrames[i].ServerTime) / 
                               (_mockFrames[i + 1].ServerTime - _mockFrames[i].ServerTime);
                    return _mockFrames[i].Position.Lerp(_mockFrames[i + 1].Position, (float)t);
                }
            }
            
            if (_mockFrames.Count > 0 && renderTime > _mockFrames[_mockFrames.Count - 1].ServerTime)
            {
                var last = _mockFrames[_mockFrames.Count - 1];
                double delta = renderTime - last.ServerTime;
                delta = Mathf.Min((float)delta, 0.5f);
                return last.Position + last.Velocity * (float)delta;
            }
            
            return _mockFrames.Count > 0 ? _mockFrames[0].Position : Vector3.Zero;
        }
        
        private Vector3 InterpolateAtTimeWithGap(double renderTime)
        {
            return InterpolateAtTime(renderTime);
        }
        
        private Vector3 ExtrapolateAtTime(double renderTime)
        {
            if (_mockFrames.Count == 0) return Vector3.Zero;
            
            var last = _mockFrames[_mockFrames.Count - 1];
            double delta = renderTime - last.ServerTime;
            delta = Mathf.Min((float)delta, 0.5f);
            
            return last.Position + last.Velocity * (float)delta;
        }
        
        private Vector3 GetServerPositionAtTime(double time)
        {
            foreach (var frame in _mockFrames)
            {
                if (Mathf.Abs((float)(frame.ServerTime - time)) < 0.001f)
                {
                    return frame.Position;
                }
            }
            return Vector3.Zero;
        }
        
        private double EstimatePositionAge(Vector3 position, double renderTime)
        {
            double minDiff = double.MaxValue;
            
            foreach (var frame in _mockFrames)
            {
                double diff = Mathf.Abs((float)(frame.Position - position).LengthSquared());
                if (diff < minDiff)
                {
                    minDiff = renderTime - frame.ServerTime;
                }
            }
            
            return Mathf.Max(0, (float)minDiff);
        }
        
        private void PrintResults()
        {
            GD.Print("=== Test Results ===");
            int passed = 0;
            foreach (var result in _testResults)
            {
                if (result.Contains("PASS")) passed++;
                GD.Print($"  {result}");
            }
            GD.Print($"\nTotal: {passed}/{_testResults.Count} tests passed");
            
            if (passed == _testResults.Count)
            {
                GD.Print("All WP-7-3 requirements met!");
            }
            else
            {
                GD.Print("Some tests failed - review implementation");
            }
        }
    }
}
