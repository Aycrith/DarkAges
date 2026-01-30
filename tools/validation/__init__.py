#!/usr/bin/env python3
"""
DarkAges MMO - Validation Framework

Comprehensive local testing and validation framework for DarkAges MMO.
This package provides tools for automated environment setup, service orchestration,
and multi-client testing.

Usage:
    # Run complete validation
    .\tools\validation\run_validation_tests.ps1
    
    # Setup environment only
    .\tools\validation\setup_environment.ps1
    
    # Manage services
    .\tools\validation\orchestrate_services.ps1 -Start
    .\tools\validation\orchestrate_services.ps1 -Stop
    
    # Run tests manually
    python -m tools.validation.multi_client_test

Exit Codes:
    0 - All validations passed
    1 - One or more validations failed
    2 - Environment setup incomplete
"""

__version__ = "1.0.0"
__author__ = "DarkAges Dev Team"

# Import main classes for convenience
# These will be available when importing from the package
import sys
from pathlib import Path

# Add stress-test to path for shared test utilities
stress_test_path = Path(__file__).parent.parent / "stress-test"
if str(stress_test_path) not in sys.path:
    sys.path.insert(0, str(stress_test_path))

try:
    from integration_harness import IntegrationBot, IntegrationTestHarness
    from bot_swarm import GameBot, BotConfig
    __all__ = [
        'IntegrationBot',
        'IntegrationTestHarness',
        'GameBot',
        'BotConfig',
    ]
except ImportError:
    # Dependencies not available yet
    __all__ = []
