# TASK-002: 100-Bot Load Test

**Status:** 🔴 Open  
**Priority:** P0  
**Estimate:** 2 hours  
**Assignee:** TBD  
**PRP:** [PRP-002](../PRP/PRP-002-LOAD-TESTING.md)

---

## Description
Execute first load test with 100 simulated bot clients to validate basic server capacity.

## Prerequisites
- [ ] Server running at 60Hz
- [ ] Redis available (or stub)
- [ ] enhanced_bot_swarm.py ready

## Steps
1. [ ] Start server: `./darkages_server.exe --zone 1`
2. [ ] Run bot swarm:
   ```bash
   python tools/stress-test/enhanced_bot_swarm.py \
     --bots 100 --duration 300 --ramp-up 30
   ```
3. [ ] Monitor server metrics (tick time, memory)
4. [ ] Capture results JSON
5. [ ] Document findings

## Acceptance Criteria
- Server maintains <16ms tick with 100 bots
- No crashes or memory spikes
- Metrics captured for baseline

## Metrics to Capture
| Metric | Target |
|--------|--------|
| Avg Tick Time | <10ms |
| Max Tick Time | <16ms |
| Memory Usage | <200MB |
| Packet Loss | <1% |
