# DarkAges MMO - Automated QA System

## Overview

This is a comprehensive end-to-end automated testing system for the DarkAges MMO. It launches real services and clients, executes automated input sequences, captures screenshots for vision analysis, and escalates to human operators when necessary.

## Key Features

- **Process Orchestration**: Automatically spawns and manages server processes and game clients
- **Input Injection**: Executes scripted input sequences (movement, combat, abilities)
- **Vision Analysis**: Captures and analyzes screenshots for validation
- **Human-in-the-Loop**: Escalates critical issues to human operators for decision making
- **Comprehensive Logging**: Captures and analyzes all process output
- **Health Monitoring**: Continuously monitors service health
- **Automated Teardown**: Cleans up all processes after test completion

## Architecture

```
automated-qa/
├── AutomatedQA.py           # Main test harness
├── scenarios/               # Test scenarios and input scripts
│   ├── movement_inputs.json
│   ├── combat_inputs.json
│   └── zone_transition.json
├── agents/                  # Specialized testing agents
├── vision/                  # Screenshot capture and analysis
└── reports/                 # Test reports and artifacts
```

## Usage

### Basic Connectivity Test

```bash
# Test basic client-server connection
python AutomatedQA.py --scenario basic_connectivity --human-oversight
```

### Movement Synchronization Test

```bash
# Test client prediction and server reconciliation
python AutomatedQA.py --scenario movement_sync --screenshot-dir ./screenshots
```

### With Human Oversight

```bash
# Enable human oversight with emergency pause
python AutomatedQA.py --scenario combat_test \
    --human-oversight \
    --emergency-pause
```

### CI/CD Mode (Fully Automated)

```bash
# Run without human interaction for CI/CD
python AutomatedQA.py --scenario regression_suite \
    --report test_report.json
```

### List Available Scenarios

```bash
python AutomatedQA.py --list-scenarios
```

## Test Scenarios

### basic_connectivity
Tests basic client-server connection and initial spawn.

**Services**: 1 zone server  
**Clients**: 1 game client  
**Duration**: ~20 seconds  
**Validations**: Client connects, no errors in logs

### movement_sync
Tests client prediction vs server reconciliation for movement.

**Services**: 1 zone server with profiling  
**Clients**: 1 client with scripted movement  
**Duration**: ~40 seconds  
**Validations**: Prediction error < 100ms P95, no rubber-banding

### combat_sync
Tests combat hit detection and synchronization.

**Services**: 1 zone server  
**Clients**: 2 clients (attacker and target)  
**Duration**: ~30 seconds  
**Validations**: Hit registration, damage synchronization

### zone_transition
Tests seamless zone handoffs.

**Services**: 2 zone servers  
**Clients**: 1 client  
**Duration**: ~60 seconds  
**Validations**: Handoff < 100ms, no disconnection

## Input Script Format

Input scripts are JSON files defining sequences of actions:

```json
[
  {
    "type": "move",
    "forward": true,
    "backward": false,
    "left": false,
    "right": false,
    "delay": 0.0
  },
  {
    "type": "key",
    "key": "space",
    "state": "press",
    "delay": 0.5
  },
  {
    "type": "mouse",
    "x": 100,
    "y": 200,
    "button": "left",
    "state": "click"
  },
  {
    "type": "wait",
    "duration": 2.0
  }
]
```

### Action Types

- **move**: WASD-style movement
  - `forward`, `backward`, `left`, `right`: boolean
  
- **key**: Keyboard input
  - `key`: Key name (e.g., "space", "1", "w")
  - `state`: "press", "release", or "hold"
  - `duration`: How long to hold (for "hold" state)
  
- **mouse**: Mouse input
  - `x`, `y`: Screen coordinates
  - `button`: "left", "right", "middle"
  - `state`: "click", "press", "release"
  
- **wait**: Pause execution
  - `duration`: Seconds to wait

## Human-in-the-Loop

When `--human-oversight` is enabled, the system will:

1. **Escalate on Critical Issues**: Emergency-level log messages trigger immediate human notification
2. **Request Approval**: Destructive actions require human approval
3. **Ask Questions**: Vision validation or ambiguous situations prompt for human insight
4. **Emergency Pause**: With `--emergency-pause`, tests stop on critical issues for investigation

### Human Interface Options

When escalated, humans can:
- `[c]` Continue - Proceed with test
- `[p]` Pause - Pause for investigation
- `[s]` Skip - Skip current test
- `[r]` Retry - Retry current step
- `[a]` Abort - Abort entire test suite
- `[f]` Feedback - Provide context/insight

## Vision Analysis

The system captures screenshots at key moments for validation:

1. **After Connection**: Verify client spawned correctly
2. **During Movement**: Check for visual anomalies
3. **After Combat**: Validate hit effects and UI updates
4. **On Error**: Capture state when issues occur

### Screenshot Analysis

Screenshots are analyzed for:
- Visual anomalies (glitches, corruption)
- UI element presence (health bars, minimap)
- Entity visibility (players, NPCs)
- Error states (crash dialogs, black screens)

## Test Results

Results include:
- Test status (PASSED/FAILED/ERROR)
- Duration
- All observations (logs, screenshots, metrics)
- Human feedback (if applicable)

### Example Report

```json
{
  "test_name": "movement_sync",
  "status": "PASSED",
  "duration": 42.5,
  "observations": [
    {
      "timestamp": 1706634567.123,
      "phase": "SERVICE_START",
      "level": "INFO",
      "source": "zone-server",
      "message": "Server started on port 7777"
    },
    {
      "timestamp": 1706634589.456,
      "phase": "EXECUTION",
      "level": "INFO",
      "source": "moving_client",
      "message": "Screenshot captured: screenshots/screenshot_1706634589456.png"
    }
  ],
  "screenshots": [
    "screenshots/screenshot_1706634589456.png",
    "screenshots/screenshot_1706634600123.png"
  ]
}
```

## Integration with CI/CD

### GitHub Actions Example

```yaml
name: Automated QA

on: [push, pull_request]

jobs:
  automated-qa:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Build Server
        run: ./build.ps1
      
      - name: Run Automated QA
        run: python tools/automated-qa/AutomatedQA.py \
          --scenario regression_suite \
          --report qa_report.json
      
      - name: Upload Results
        uses: actions/upload-artifact@v2
        with:
          name: qa-results
          path: |
            qa_report.json
            screenshots/
```

## Requirements

### System Requirements
- Windows 10/11 or Linux
- Python 3.8+
- Sufficient RAM for multiple clients (8GB+ recommended)

### Python Dependencies
```bash
pip install Pillow pywin32
```

### Game Client Requirements
- Godot 4.x built executable
- Windowed mode enabled for screenshot capture

## Troubleshooting

### Client Not Spawning
- Verify executable path is correct
- Check that client can run standalone
- Ensure windowed mode is enabled

### Screenshots Not Capturing
- Check screenshot directory permissions
- Verify PIL/Pillow is installed
- Ensure client window is visible

### Human Escalation Not Working
- Must use `--human-oversight` flag
- Check stdin is available (not in non-interactive mode)

## Extending the System

### Adding New Scenarios

1. Create scenario configuration:

```python
"my_scenario": {
    "description": "Description of test",
    "services": [
        {
            "name": "service-name",
            "command": ["./path/to/executable", "--arg"],
            "health_check_url": "http://localhost:8080/health"
        }
    ],
    "clients": [
        {
            "name": "client-name",
            "executable": "./path/to/client.exe",
            "input_script": "./path/to/inputs.json"
        }
    ],
    "actions": [
        {"type": "wait", "duration": 5.0}
    ],
    "validations": [
        {"type": "custom_validation"}
    ]
}
```

2. Add to `SCENARIOS` dictionary in `AutomatedQA.py`

### Adding New Validations

Extend the `_validate_results` method in `AutomatedQA` class.

## Safety Considerations

- Always use `--human-oversight` for destructive tests
- Tests clean up processes on exit (normal or crash)
- Screenshot directory can grow large - clean periodically
- Network conditions are simulated safely (no actual packet loss)

## License

Part of DarkAges MMO project
