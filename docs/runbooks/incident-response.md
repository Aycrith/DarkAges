# DarkAges MMO - Incident Response Runbook

## Overview

This runbook provides step-by-step procedures for responding to incidents in the DarkAges MMO production environment.

## Severity Levels

| Level | Description | Response Time | Examples |
|-------|-------------|---------------|----------|
| P0 | Critical - Service down | 15 minutes | All zones offline, data corruption |
| P1 | High - Major impact | 30 minutes | Single zone down, >50% performance degradation |
| P2 | Medium - Partial impact | 2 hours | High latency, intermittent errors |
| P3 | Low - Minor impact | 24 hours | Non-critical feature unavailable |

## Incident Response Process

### 1. Detection
- Alert fires in PagerDuty/Opsgenie
- Monitoring dashboard shows anomaly
- Customer reports issue
- Automated chaos test failure

### 2. Response
1. Acknowledge alert within SLA
2. Join incident response channel (Slack: #incidents)
3. Assess severity and impact
4. Begin documenting in incident log

### 3. Investigation
- Check monitoring dashboards
- Review recent deployments
- Check infrastructure status
- Analyze logs

### 4. Mitigation
- Apply temporary fixes
- Scale resources if needed
- Enable circuit breakers
- Redirect traffic if needed

### 5. Resolution
- Confirm fix is working
- Monitor for stability
- Update incident status
- Schedule post-mortem

---

## Specific Scenarios

### P0: All Zone Servers Offline

**Symptoms:**
- Player count drops to 0
- All health checks failing
- Connection timeouts

**Immediate Actions:**
```bash
# Check Kubernetes cluster status
kubectl get nodes
kubectl get pods -n darkages

# Check if nodes are down
kubectl describe nodes | grep -i "not ready"

# Emergency scale-up if needed
kubectl scale deployment zone-server --replicas=4 -n darkages
```

**Investigation:**
```bash
# Check recent events
kubectl get events -n darkages --sort-by='.lastTimestamp' | tail -20

# Check pod logs
kubectl logs -l app=zone-server -n darkages --tail=100

# Check resource usage
kubectl top pods -n darkages
```

**Common Causes:**
1. Kubernetes node failure → Auto-healing should recover
2. Database connection failure → Check Redis/ScyllaDB
3. Network partition → Check CNI status
4. Resource exhaustion → Scale up nodes

---

### P1: Single Zone Server Down

**Symptoms:**
- One zone showing 0 players
- Health check failing for specific zone
- Players in that zone disconnected

**Immediate Actions:**
```bash
# Check specific zone pod
kubectl get pod zone-server-X -n darkages
kubectl describe pod zone-server-X -n darkages

# Check logs
kubectl logs zone-server-X -n darkages --tail=100

# Restart if needed
kubectl delete pod zone-server-X -n darkages
```

**Player Migration:**
- Players should auto-migrate to adjacent zones
- Monitor migration success rate
- Check Aura Projection buffer handoffs

---

### P1: High Tick Time (>16ms)

**Symptoms:**
- Alert: "HighTickTime"
- Server FPS drops below 60
- Player lag reports

**Immediate Actions:**
```bash
# Check current tick times
kubectl exec -it zone-server-0 -n darkages -- curl localhost:8080/metrics | grep tick_duration

# Enable QoS degradation (emergency mode)
kubectl exec -it zone-server-0 -n darkages -- kill -USR1 1
```

**Investigation:**
```bash
# Profile the server
kubectl exec -it zone-server-0 -n darkages -- sh -c "gdb -p 1 -ex 'thread apply all bt' -ex quit"

# Check entity count
kubectl exec -it zone-server-0 -n darkages -- curl localhost:8080/metrics | grep entity_count
```

**Mitigation Options:**
1. Enable QoS degradation (reduce update rate for distant entities)
2. Split zone (if >400 players)
3. Scale up CPU allocation
4. Restart zone server (last resort)

---

### P1: Database Connection Lost

**Symptoms:**
- Alert: "DatabaseConnectionFailed"
- Players can't save progress
- Login failures

**Immediate Actions:**
```bash
# Check Redis status
kubectl get pods -n darkages -l app=redis
kubectl logs -l app=redis -n darkages --tail=50

# Check ScyllaDB status
kubectl get pods -n darkages -l app=scylla
kubectl logs -l app=scylla -n darkages --tail=50
```

**Recovery:**
```bash
# Restart Redis if needed
kubectl rollout restart statefulset/redis -n darkages

# Restart ScyllaDB if needed (rolling restart)
kubectl rollout restart statefulset/scylla -n darkages
```

---

### P2: High Memory Usage

**Symptoms:**
- Alert: "HighMemoryUsage"
- OOMKilled pods
- Performance degradation

**Immediate Actions:**
```bash
# Check memory usage
kubectl top pods -n darkages

# Check for memory leaks
kubectl logs zone-server-0 -n darkages | grep -i "memory\|leak"

# Scale up memory if needed
kubectl patch deployment zone-server -n darkages -p '{"spec":{"template":{"spec":{"containers":[{"name":"zone-server","resources":{"limits":{"memory":"8Gi"}}}]}}}}'
```

---

### P2: DDoS Attack

**Symptoms:**
- Sudden traffic spike
- High connection count
- Rate limiting triggered

**Immediate Actions:**
```bash
# Check connection counts
kubectl exec -it zone-server-0 -n darkages -- netstat -an | grep ESTABLISHED | wc -l

# Enable DDoS protection mode
kubectl patch configmap ddos-config -n darkages --type merge -p '{"data":{"mode":"strict"}}'

# Scale up WAF if using external protection
```

**Investigation:**
```bash
# Analyze traffic patterns
kubectl logs -l app=zone-server -n darkages | grep "connection" | sort | uniq -c | sort -rn | head -20
```

---

## Escalation Path

1. **On-call Engineer**
   - First responder
   - Initial triage
   - Basic remediation

2. **Senior SRE** (P1+, 30 min no progress)
   - Complex issues
   - Architecture decisions
   - Cross-team coordination

3. **Team Lead** (P0, immediate)
   - Business impact assessment
   - Customer communication
   - Executive updates

4. **Infrastructure Lead** (infrastructure issues)
   - Kubernetes cluster issues
   - Network problems
   - Cloud provider issues

## Communication Templates

### Status Page Update (P0/P1)
```
[Investigating] DarkAges MMO - Service Disruption

We are currently investigating issues affecting game connectivity.
Some players may experience disconnections or inability to log in.

Our team is actively working on resolution.
Updates will be posted every 30 minutes.
```

### Resolution Update
```
[Resolved] DarkAges MMO - Service Restored

The issue has been resolved. All systems are operational.
Root cause: [Brief description]
Post-mortem will be published within 48 hours.
```

## Post-Incident Actions

1. **Within 1 hour:**
   - Write incident summary
   - Update runbook if needed

2. **Within 24 hours:**
   - Schedule post-mortem meeting
   - Prepare timeline

3. **Within 1 week:**
   - Complete post-mortem document
   - Create action items
   - Implement preventive measures

## Useful Commands Reference

```bash
# Quick health check
curl http://zone-server.darkages.svc.cluster.local:8080/health

# Metrics
curl http://zone-server.darkages.svc.cluster.local:8080/metrics

# Force zone restart
kubectl rollout restart deployment/zone-server -n darkages

# Scale zones
kubectl scale deployment zone-server --replicas=6 -n darkages

# Get shell into pod
kubectl exec -it zone-server-0 -n darkages -- /bin/sh

# Port forward for debugging
kubectl port-forward pod/zone-server-0 8080:8080 -n darkages
```

## Contact Information

- **On-call:** oncall@darkages-mmo.com
- **Slack:** #incidents
- **PagerDuty:** https://darkages.pagerduty.com
- **Emergency Hotline:** +1-XXX-XXX-XXXX
