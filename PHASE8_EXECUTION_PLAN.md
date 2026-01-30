# Phase 8 Execution Plan
## DarkAges MMO - Production Hardening

**Version:** 1.0  
**Date:** 2026-01-30  
**Status:** READY FOR EXECUTION  
**Duration:** 8 weeks (2026-01-30 to 2026-03-27)

---

## Executive Summary

This document provides the comprehensive execution plan for Phase 8: Production Hardening. Based on the successful remediation (all critical issues resolved, server stable at 60Hz), we are ready to transform the validated system into a production-ready, scalable, monitored, and resilient platform.

### Current State (Validated)
| Component | Status | Details |
|-----------|--------|---------|
| Server Core | ✅ Stable | 60Hz tick, no crashes |
| Build System | ✅ Working | CMake + FetchContent |
| Database | ✅ Connected | Redis/ScyllaDB stubs ready |
| Stack Overflow | ✅ Fixed | Heap allocation conversion |
| Validation | ✅ Passed | All tests green |

---

## Work Package Execution Order

### Phase 8A: Foundation (Weeks 1-2)
**Parallel Workstreams:**

#### WP-8-1: Production Monitoring & Alerting
**Agent:** DEVOPS  
**Priority:** CRITICAL (Blocks other work)  
**Duration:** 2 weeks  

**Tasks:**
1. **Day 1-2:** Set up Prometheus locally
   - Install Prometheus server
   - Configure `prometheus.yml` with scrape targets
   - Test metrics collection endpoint

2. **Day 3-4:** Implement metrics export endpoint (`/metrics`)
   - Add `MetricsExporter` class to server
   - Implement Prometheus text format output
   - Expose basic metrics: tick time, memory, CCU

3. **Day 5-7:** Set up Grafana
   - Install Grafana server
   - Create data source for Prometheus
   - Build server health dashboard

4. **Day 8-10:** Custom metrics implementation
   - Zone population metrics
   - Packet loss tracking
   - Replication bandwidth metrics
   - Anti-cheat detection counts

5. **Day 11-14:** Alerting rules
   - Tick time >16ms alert
   - Memory >80% alert
   - Error rate >10/min alert
   - Configure alert notifications (email/Slack)

**Deliverables:**
- `src/server/src/monitoring/MetricsExporter.cpp`
- `infra/prometheus/prometheus.yml`
- `infra/grafana/dashboards/server-health.json`
- `infra/alerting/rules.yml`

**Success Criteria:**
- [ ] `/metrics` endpoint returns valid Prometheus format
- [ ] Grafana dashboard shows real-time data
- [ ] Alerts trigger within 5s of threshold breach

---

#### WP-8-6: GameNetworkingSockets Integration
**Agent:** NETWORK  
**Priority:** CRITICAL  
**Duration:** 2 weeks  
**Note:** Can run parallel with WP-8-1

**Tasks:**
1. **Day 1-3:** Protobuf schema definitions
   - Define `Protocol.proto` with all message types
   - Generate C++ headers from .proto files
   - Generate C# classes for client

2. **Day 4-7:** GNS connection layer
   - Replace stub NetworkManager with GNS implementation
   - Implement connection handshake
   - Add encryption (SRV)

3. **Day 8-10:** Reliable channels
   - Implement ordered reliable channel for critical data
   - Implement unreliable channel for position updates
   - Test with 100+ concurrent connections

4. **Day 11-14:** NAT traversal
   - Implement relay server support
   - Test P2P connectivity
   - Fallback to relay when direct fails

**Deliverables:**
- `src/shared/proto/Protocol.proto`
- `src/server/src/netcode/GNSNetworkManager.cpp`
- `src/client/src/networking/GNSClient.cs`

**Success Criteria:**
- [ ] <10ms connection establishment
- [ ] 99.99% packet delivery
- [ ] 10000 concurrent connections supported

---

### Phase 8B: Security & Testing (Weeks 3-4)

#### WP-8-2: Security Audit & Hardening
**Agent:** SECURITY  
**Priority:** HIGH  
**Duration:** 2 weeks  

**Tasks:**
1. **Day 1-3:** Code security audit
   - Static analysis with CodeQL
   - Review all input validation
   - Check for buffer overflows, injection risks

2. **Day 4-6:** GNS encryption
   - Enable SRV encryption
   - Certificate management
   - Key rotation procedure

3. **Day 7-9:** Packet integrity
   - Implement HMAC for all packets
   - Replay attack prevention
   - Sequence number validation

4. **Day 10-12:** Anti-cheat improvements
   - Speed hack detection (server-side validation)
   - Teleport validation with position history
   - Statistical anomaly detection

5. **Day 13-14:** Rate limiting & DDoS
   - Per-IP connection limiting
   - Per-player input rate limiting (60Hz max)
   - DDoS protection thresholds

**Deliverables:**
- `src/server/src/security/PacketValidator.cpp`
- `src/server/src/security/RateLimiter.cpp`
- Security audit report
- Penetration test documentation

**Success Criteria:**
- [ ] Zero critical vulnerabilities
- [ ] <0.1% false positive anti-cheat
- [ ] DDoS resistance to 10 Gbps

---

#### WP-8-5: Load Testing & Capacity Planning
**Agent:** DEVOPS  
**Priority:** HIGH  
**Duration:** 1 week  
**Dependencies:** WP-8-1 (monitoring must be in place)

**Tasks:**
1. **Day 1-2:** Bot swarm enhancement
   - Enhance Python bot for realistic behavior
   - Add movement patterns, combat simulation
   - Support 1000+ concurrent bots

2. **Day 3-4:** Capacity testing
   - Test single-zone capacity (target: 500 players)
   - Identify tick time bottlenecks
   - Memory profiling at scale

3. **Day 5-6:** Multi-zone testing
   - Test with 4 zones (2000 total players)
   - Zone handoff performance
   - Cross-zone messaging overhead

4. **Day 7:** Documentation
   - Capacity limits per zone
   - Scaling recommendations
   - Cloud deployment cost estimation

**Deliverables:**
- `tools/stress-test/enhanced_bot_swarm.py`
- Capacity planning report
- Cost estimation spreadsheet

**Success Criteria:**
- [ ] 1000 concurrent player simulation
- [ ] 60Hz tick maintained at 1000 players
- [ ] Latency <50ms for 95th percentile

---

### Phase 8C: Resilience & Scale (Weeks 5-6)

#### WP-8-3: Chaos Engineering Framework
**Agent:** DEVOPS  
**Priority:** MEDIUM  
**Duration:** 2 weeks  
**Dependencies:** WP-8-1

**Tasks:**
1. **Day 1-4:** Chaos monkey implementation
   - Random zone server termination
   - Network partition simulation
   - Database failover testing

2. **Day 5-8:** Automated recovery
   - Health check automation
   - Auto-restart procedures
   - Data consistency validation

3. **Day 9-12:** Chaos dashboard
   - Real-time experiment status
   - Recovery time metrics
   - Historical experiment results

4. **Day 13-14:** Runbook development
   - Incident response procedures
   - Rollback procedures
   - Escalation paths

**Deliverables:**
- `tools/chaos/chaos_monkey.py`
- `tools/chaos/network_partition.sh`
- Chaos dashboard

**Success Criteria:**
- [ ] <30s auto-recovery time
- [ ] 99.9% uptime during chaos
- [ ] Zero data loss during failover

---

#### WP-8-4: Auto-Scaling Infrastructure
**Agent:** DEVOPS  
**Priority:** MEDIUM  
**Duration:** 2 weeks  
**Dependencies:** WP-8-1, WP-8-3

**Tasks:**
1. **Day 1-5:** Kubernetes operator
   - Zone server CRD definition
   - Operator implementation
   - Zone state management

2. **Day 6-10:** Horizontal Pod Autoscaling
   - CPU-based scaling
   - CCU-based scaling
   - Scale-down protection

3. **Day 11-12:** Zone splitting
   - Algorithm for zone subdivision
   - Player migration during split
   - State synchronization

4. **Day 13-14:** Connection draining
   - Graceful player migration
   - Connection handoff
   - Zero-downtime deployments

**Deliverables:**
- `infra/k8s/zone-operator/`
- `infra/k8s/hpa/`
- Auto-scaling runbook

**Success Criteria:**
- [ ] <60s auto-scaling response
- [ ] Zero-downtime zone splits
- [ ] <5s player migration time

---

### Phase 8D: Hardening & Documentation (Weeks 7-8)

#### WP-8-7: Database Production Hardening
**Agent:** DATABASE  
**Priority:** MEDIUM  
**Duration:** 1 week

**Tasks:**
1. **Day 1-2:** Redis cluster
   - 3-node cluster setup
   - Sentinel for failover
   - Persistence configuration

2. **Day 3-4:** ScyllaDB cluster
   - 5-node cluster setup
   - Replication factor configuration
   - Performance tuning

3. **Day 5-6:** Write-through optimization
   - Cache warming
   - Eviction policies
   - Hot key handling

4. **Day 7:** Backup procedures
   - Automated backup scripts
   - Point-in-time recovery
   - Disaster recovery testing

**Deliverables:**
- `infra/k8s/redis-cluster/`
- `infra/k8s/scylla-cluster/`
- Backup/restore procedures

**Success Criteria:**
- [ ] Redis <1ms latency (99th percentile)
- [ ] ScyllaDB >100k writes/sec
- [ ] <1s RPO (Recovery Point Objective)

---

#### WP-8-8: Documentation & Runbooks
**Agent:** DEVOPS  
**Priority:** LOW  
**Duration:** 1 week  
**Dependencies:** All other WPs

**Tasks:**
1. **Day 1-3:** Operations runbooks
   - Incident response procedures
   - Severity classification
   - On-call rotation guide

2. **Day 4-5:** Deployment procedures
   - Blue-green deployment
   - Canary releases
   - Rollback procedures

3. **Day 6-7:** Troubleshooting guides
   - Common issues and resolutions
   - Debug command reference
   - Log analysis guide

**Deliverables:**
- `docs/runbooks/incident-response.md`
- `docs/runbooks/deployment.md`
- `docs/runbooks/troubleshooting.md`

**Success Criteria:**
- [ ] <15min MTTR (Mean Time To Recovery)
- [ ] 100% procedure documentation coverage

---

## Timeline Visualization

```
Week:  1      2      3      4      5      6      7      8
       ├──────┼──────┼──────┼──────┼──────┼──────┼──────┤
WP-8-1 ████████       [DEVOPS]  Monitoring
WP-8-6 ████████       [NETWORK] GNS Integration
WP-8-2        ████████[SECURITY]Security Audit
WP-8-5        ███     [DEVOPS]  Load Testing
WP-8-3               ████████   [DEVOPS]  Chaos Engineering
WP-8-4               ████████   [DEVOPS]  Auto-Scaling
WP-8-7                      ███ [DATABASE]DB Hardening
WP-8-8                      ███ [DEVOPS]  Documentation
Buffer                         ██ Integration/Buffer
```

---

## Resource Requirements

### Personnel
| Role | Count | Weeks | Responsibilities |
|------|-------|-------|------------------|
| DEVOPS Lead | 1 | 1-8 | Coordination, monitoring, infrastructure |
| Network Engineer | 1 | 1-2 | GNS integration |
| Security Engineer | 1 | 3-4 | Security audit, hardening |
| Database Engineer | 1 | 7 | Cluster setup |

### Infrastructure
| Resource | Count | Purpose |
|----------|-------|---------|
| Kubernetes cluster | 1 | Container orchestration |
| Zone server nodes | 3-10 | Game servers |
| Redis nodes | 3 | Session cache cluster |
| ScyllaDB nodes | 5 | Combat logging cluster |
| Monitoring stack | 1 | Prometheus/Grafana |
| Load test clients | 50 | 20 bots per client |

---

## Risk Register

| Risk | Impact | Likelihood | Mitigation | Owner |
|------|--------|------------|------------|-------|
| GNS integration delays | HIGH | MEDIUM | Parallel stub improvements; phased rollout | NETWORK |
| Kubernetes complexity | MEDIUM | MEDIUM | Start with Docker Compose; gradual migration | DEVOPS |
| Cloud costs exceed budget | MEDIUM | LOW | Local testing first; cost monitoring | DEVOPS |
| Database cluster issues | HIGH | LOW | Extensive local testing; backup procedures | DATABASE |
| Security audit finds critical issues | HIGH | LOW | Timeboxed remediation; phased fixes | SECURITY |

---

## Integration Milestones

### Milestone 1: Monitoring (End Week 2)
- [ ] Prometheus collecting metrics
- [ ] Grafana dashboards operational
- [ ] GNS integration complete
- [ ] All tests passing

### Milestone 2: Security & Load (End Week 4)
- [ ] Security audit complete
- [ ] 1000-player load test passed
- [ ] Rate limiting operational
- [ ] Anti-cheat hardened

### Milestone 3: Resilience (End Week 6)
- [ ] Chaos tests passing
- [ ] Auto-scaling demonstrated
- [ ] <30s recovery time achieved
- [ ] Zone splitting tested

### Milestone 4: Production Ready (End Week 8)
- [ ] Database clusters operational
- [ ] All documentation complete
- [ ] Final integration tests passed
- [ ] Sign-off from all agents

---

## Success Criteria Summary

| Metric | Target | Verification |
|--------|--------|--------------|
| Uptime | 99.9% | 7-day stress test |
| Tick Rate | 60Hz @ 1000 players | Profiling |
| Latency | <50ms (95th percentile) | Network test |
| Auto-recovery | <30s | Chaos testing |
| Auto-scaling | <60s | Load test |
| DDoS mitigation | 10 Gbps | Penetration test |
| MTTR | <15min | Incident simulation |

---

## Immediate Actions (Week 1, Day 1)

1. **DEVOPS:** Set up Prometheus + Grafana locally
2. **NETWORK:** Create Protobuf schema definitions
3. **SECURITY:** Begin code security audit
4. **DEVOPS:** Enhance Python bot for 100+ player simulation
5. **ALL:** Review this execution plan

---

## Communication Plan

- **Daily Standups:** 9 AM UTC
- **Weekly Review:** Fridays 3 PM UTC
- **Milestone Reviews:** End of weeks 2, 4, 6, 8
- **Blocker Escalation:** Immediate Slack ping
- **Documentation Updates:** With each PR

---

## Approval

| Role | Name | Signature | Date |
|------|------|-----------|------|
| Project Lead | AGENT-DEVOPS | _________ | _________ |
| Technical Lead | AGENT-NETWORK | _________ | _________ |
| Security Lead | AGENT-SECURITY | _________ | _________ |

---

**Plan Version:** 1.0  
**Created:** 2026-01-30  
**Status:** READY FOR EXECUTION  

**Next Step:** Begin WP-8-1 and WP-8-6 in parallel
