# TASK-004: 1000-Bot Load Test

**Status:** 🔴 Open  
**Priority:** P1  
**Estimate:** 4 hours  
**Depends On:** TASK-003  
**PRP:** [PRP-002](../PRP/PRP-002-LOAD-TESTING.md)

---

## Description
Full capacity test with 1000 concurrent bot clients.

## Command
```bash
python tools/stress-test/enhanced_bot_swarm.py \
  --bots 1000 --duration 3600 --ramp-up 120
```

## Acceptance Criteria
- Server maintains <16ms tick @ 1000 bots
- 1-hour sustained without crash
