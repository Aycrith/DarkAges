# TASK-006: Chaos Test Execution

**Status:** 🔴 Open  
**Priority:** P2  
**Estimate:** 4 hours  
**Depends On:** TASK-002  
**PRD:** [PRD-006](../PRD/PRD-006-INFRASTRUCTURE.md)

---

## Description
Execute chaos engineering tests to validate resilience.

## Command
```bash
python tools/chaos/chaos_monkey.py \
  --duration 3600 --min-interval 60 --max-interval 180
```

## Fault Types to Test
- [ ] KILL_ZONE_SERVER
- [ ] NETWORK_PARTITION
- [ ] LATENCY_INJECTION
- [ ] PACKET_LOSS
- [ ] DATABASE_RESTART

## Acceptance Criteria
- Auto-recovery <30s
- No data loss
- 99.9% availability maintained
