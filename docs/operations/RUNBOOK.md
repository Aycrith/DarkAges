# DarkAges MMO - Operational Runbook

**Version**: 1.0  
**Last Updated**: 2026-01-29  
**Environment**: Production

---

## Emergency Contacts

| Role | Contact | Method |
|------|---------|--------|
| On-call Engineer | +1-XXX-XXX-XXXX | Phone/SMS |
| Infrastructure Team | +1-XXX-XXX-XXXX | Phone/SMS |
| Engineering Lead | +1-XXX-XXX-XXXX | Phone/SMS |
| Discord Alerts | #ops-alerts | Chat |
| PagerDuty | DarkAges-Critical | Page |

---

## Quick Diagnostics

### System Status Overview

```bash
# Check all service health
docker-compose -f infra/docker-compose.prod.yml ps

# Quick health check all zones
for port in 7778 7780 7782 7784; do
  echo "Zone on port $port: $(curl -s http://localhost:$port/health || echo 'DOWN')"
done

# Total players online
python tools/admin/manage_zones.py status

# Check resource usage
docker stats --no-stream --format "table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t{{.PIDs}}"
```

### Log Access

```bash
# View all zone logs
docker-compose -f infra/docker-compose.prod.yml logs -f --tail=100

# View specific zone logs
docker-compose -f infra/docker-compose.prod.yml logs -f zone-1 --tail=500

# Search for errors in logs
docker-compose -f infra/docker-compose.prod.yml logs --tail=1000 | grep ERROR

# Grafana Loki log queries
# Open http://localhost:3000/explore
# Query: {job="zone-logs"} |= "ERROR"
```

---

## Common Issues

### Zone Offline

**Symptoms**:
- Grafana alert: "Zone X is offline"
- Players disconnected from zone
- Health check fails

**Diagnostic Steps**:
```bash
# Check container status
docker ps -a | grep zone-X

# Check logs for crash
docker logs --tail 500 darkages-zone-X

# Check if port is listening
netstat -tlnp | grep ZONE_PORT
```

**Resolution**:
```bash
# Quick restart
python tools/admin/manage_zones.py restart --zone X

# If restart fails, check infrastructure:
# 1. Verify Redis is healthy
docker-compose -f infra/docker-compose.prod.yml ps redis

# 2. Verify Scylla is healthy
docker-compose -f infra/docker-compose.prod.yml ps scylla

# 3. Full restart with logs
docker-compose -f infra/docker-compose.prod.yml restart zone-X && \
  docker-compose -f infra/docker-compose.prod.yml logs -f zone-X
```

**Post-Incident**:
1. Verify zone health: `curl http://localhost:PORT/health`
2. Check player reconnection rates
3. Review logs for root cause
4. Document in incident log

---

### High Tick Time

**Symptoms**:
- Alert: "High tick time" (>20ms warning, >50ms critical)
- Player reports of lag/rubber-banding
- FPS drops below 60

**Diagnostic Steps**:
```bash
# Check current tick times
curl -s http://localhost:7778/metrics | grep zone_tick_duration_ms

# Check player counts (high load indicator)
python tools/admin/manage_zones.py status

# Download profiling trace
curl -o trace_zoneX.json http://localhost:7778/debug/pprof/trace

# Check system resources
docker stats darkages-zone-X --no-stream
```

**Resolution**:

1. **Immediate (QoS Degradation)**:
   - System automatically activates QoS at >20ms
   - Reduces snapshot rate for distant entities
   - Culls non-essential updates

2. **Short-term (Capacity Management)**:
   ```bash
   # Check if specific zone is overloaded
   # Consider manual player redistribution
   # Enable maintenance mode to prevent new joins:
   curl -X POST http://localhost:7778/admin/maintenance \
     -H "Authorization: Bearer $ADMIN_TOKEN"
   ```

3. **Long-term (Scaling)**:
   - Add new zones to grid
   - Increase container resources (CPU/memory)
   - Optimize hot paths based on profiling data

**Prevention**:
- Monitor player density alerts (>350 players/zone)
- Regular performance profiling
- Load testing before major events

---

### DDoS Attack

**Symptoms**:
- Alert: "DDoS Detected" (>10 blocked connections/sec)
- Sudden spike in connection attempts
- Legitimate players experiencing lag

**Diagnostic Steps**:
```bash
# Check blocked connections metric
curl -s http://localhost:7778/metrics | grep zone_blocked_connections_total

# View logs for blocked IPs
docker logs darkages-zone-1 --tail 1000 | grep BLOCKED | tail -20

# Check connection rates
for port in 7777 7779 7781 7783; do
  ss -tan | grep :$port | wc -l
done
```

**Resolution**:

1. **Automatic Protection** (already active):
   - Rate limiting per IP
   - Connection throttling
   - Block list auto-updates

2. **Manual Intervention**:
   ```bash
   # Enable emergency mode (stricter limits)
   curl -X POST http://localhost:7778/admin/emergency-mode \
     -H "Authorization: Bearer $ADMIN_TOKEN"
   
   # Block specific IP range
   curl -X POST http://localhost:7778/admin/block-ip \
     -H "Authorization: Bearer $ADMIN_TOKEN" \
     -d '{"ip": "x.x.x.x", "duration": 3600}'
   ```

3. **Infrastructure Level**:
   - Contact hosting provider for upstream filtering
   - Enable Cloudflare or similar if configured
   - Consider geographic blocking if attack is region-specific

**Post-Incident**:
- Document attack patterns
- Update firewall rules
- Review and tune thresholds

---

### Memory Issues

**Symptoms**:
- Alert: "High Memory Usage" (>85%)
- Container OOM kills
- Performance degradation

**Diagnostic Steps**:
```bash
# Check memory metrics
curl -s http://localhost:7778/metrics | grep zone_memory_usage_bytes

# Check for memory leaks (increasing over time)
# View in Grafana: Memory Usage panel over 24h

# Check container limits
docker inspect darkages-zone-1 | grep -A5 Memory
```

**Resolution**:

1. **Immediate**:
   ```bash
   # Restart affected zone (clears memory)
   python tools/admin/manage_zones.py restart --zone X
   ```

2. **Investigation**:
   ```bash
   # Enable memory profiling
   curl -X POST http://localhost:7778/admin/profile/memory \
     -H "Authorization: Bearer $ADMIN_TOKEN"
   
   # Download heap profile
curl -o heap.pprof http://localhost:7778/debug/pprof/heap
   ```

3. **Long-term**:
   - Increase memory limits if legitimate growth
   - Fix memory leaks in code
   - Implement memory quotas per feature

---

### Database Issues

#### Redis Down

**Symptoms**:
- Alert: "Redis is unreachable"
- Zones unable to sync state
- Player sessions may fail

**Resolution**:
```bash
# Check Redis status
docker-compose -f infra/docker-compose.prod.yml ps redis
docker logs darkages-redis --tail 100

# Restart Redis
docker-compose -f infra/docker-compose.prod.yml restart redis

# Verify zones reconnect
sleep 10
python tools/admin/manage_zones.py status
```

#### ScyllaDB Issues

**Symptoms**:
- Alert: "ScyllaDB is unreachable"
- Combat logging fails
- Async write errors in logs

**Resolution**:
```bash
# Check Scylla status
docker-compose -f infra/docker-compose.prod.yml ps scylla
docker logs darkages-scylla --tail 200

# Check disk space
docker exec darkages-scylla df -h

# Restart if needed
docker-compose -f infra/docker-compose.prod.yml restart scylla
# Note: Restart may take 60+ seconds for health check
```

---

## Deployment Procedures

### Standard Deployment (Rolling)

```bash
# 1. Pre-deployment checks
python tools/admin/manage_zones.py status
python tools/stress-test/test_multiplayer.py --quick

# 2. Set maintenance banner (optional)
# Notify players of brief maintenance

# 3. Deploy using script
tools/deploy/deploy.sh production latest

# 4. Post-deployment verification
python tools/admin/manage_zones.py status
python tools/stress-test/test_multiplayer.py --quick
```

### Emergency Rollback

```bash
# Quick rollback to previous version
docker-compose -f infra/docker-compose.prod.yml down

# Restore from backup if needed
# (See Backup & Recovery section)

# Start with previous image
docker-compose -f infra/docker-compose.prod.yml up -d

# Verify
python tools/admin/manage_zones.py status
```

### Hotfix Deployment

```bash
# Deploy to single zone first (canary)
tools/deploy/deploy.sh production hotfix-v1.2.1 --zone 1

# Monitor for 10 minutes
# If healthy, deploy to remaining zones
tools/deploy/deploy.sh production hotfix-v1.2.1
```

---

## Monitoring

### Grafana Dashboards

| Dashboard | URL | Purpose |
|-----------|-----|---------|
| Main Dashboard | http://localhost:3000/d/darkages-main | Overall health |
| Zone Details | http://localhost:3000/d/zones | Per-zone metrics |
| Network | http://localhost:3000/d/network | Connection quality |
| Infrastructure | http://localhost:3000/d/infra | Redis, Scylla, host |

### Key Metrics Reference

| Metric | Normal | Warning | Critical | Action |
|--------|--------|---------|----------|--------|
| Tick Time | <16ms | 16-20ms | >20ms | Profile, QoS, Scale |
| FPS | 60 | 55-59 | <55 | Investigate CPU |
| Memory | <70% | 70-85% | >85% | Restart, increase limit |
| Players/Zone | <300 | 300-350 | >350 | Add zones, redistribute |
| Latency (p99) | <50ms | 50-100ms | >100ms | Network investigation |
| Migration Failures | 0 | <5/min | >5/min | Check cross-zone connectivity |

---

## Backup & Recovery

### Automated Backups

Backups run automatically via the backup container:
- **Frequency**: Daily at 2:00 AM
- **Retention**: 30 days
- **Location**: Local + S3 (if configured)

### Manual Backup

```bash
# Redis backup (creates RDB)
docker exec darkages-redis redis-cli BGSAVE
docker cp darkages-redis:/data/dump.rdb ./backups/redis-$(date +%Y%m%d).rdb

# Scylla backup (snapshot)
docker exec darkages-scylla nodetool snapshot darkages
# Copy snapshot from /var/lib/scylla/data
```

### Disaster Recovery

```bash
# 1. Stop all services
docker-compose -f infra/docker-compose.prod.yml down

# 2. Restore Redis from RDB
cp ./backups/redis-YYYYMMDD.rdb redis_data/dump.rdb

# 3. Restore Scylla (if available)
# Scylla snapshots in scylla_data/

# 4. Start infrastructure first
docker-compose -f infra/docker-compose.prod.yml up -d redis scylla
sleep 30

# 5. Start zones
docker-compose -f infra/docker-compose.prod.yml up -d zone-1 zone-2 zone-3 zone-4

# 6. Verify
python tools/admin/manage_zones.py status
```

---

## Escalation Matrix

| Severity | Criteria | Response Time | Action |
|----------|----------|---------------|--------|
| P1 (Critical) | All zones down, data loss, security breach | 15 min | Page on-call, all-hands, war room |
| P2 (High) | Single zone down, major feature broken | 1 hour | Notify team, start investigation |
| P3 (Medium) | Performance degradation, minor features affected | 4 hours | Create ticket, schedule fix |
| P4 (Low) | Cosmetic issues, monitoring gaps | 24 hours | Backlog for next sprint |

### Severity Determination

**P1 Examples**:
- All 4 zones offline
- Database corruption
- Active exploit in progress
- >50% players unable to connect

**P2 Examples**:
- Single zone offline >5 min
- Combat system broken
- Migration system failing
- Significant memory leak

**P3 Examples**:
- Tick time >20ms for >10 min
- Metrics missing
- Backup failures
- Non-critical alerts firing

---

## Troubleshooting Commands

### Docker

```bash
# Resource usage by container
docker stats --no-stream

# Container logs
docker logs -f --tail 500 CONTAINER_NAME

# Execute command in container
docker exec -it CONTAINER_NAME /bin/sh

# Inspect container
docker inspect CONTAINER_NAME

# Network inspection
docker network inspect darkages-network
```

### Redis

```bash
# Connect to Redis
docker exec -it darkages-redis redis-cli

# Check connected clients
INFO clients

# Check memory
INFO memory

# Monitor commands (debug)
MONITOR

# Check zone sets
SMEMBERS zone:1:players
```

### ScyllaDB

```bash
# Connect to Scylla
docker exec -it darkages-scylla cqlsh

# Check keyspaces
DESCRIBE KEYSPACES;

# Check tables in darkages keyspace
USE darkages;
DESCRIBE TABLES;

# Check node status
docker exec darkages-scylla nodetool status
```

### Networking

```bash
# Check listening ports
ss -tlnp

# Check connections to zones
for port in 7777 7779 7781 7783; do
  echo "=== Port $port ==="
  ss -tan | grep :$port | head -5
done

# Check inter-container connectivity
docker exec -it darkages-zone-1 ping redis
docker exec -it darkages-zone-1 ping scylla
```

---

## Maintenance Windows

### Scheduled Maintenance

1. **Announce**: 24 hours in advance on Discord/status page
2. **Prepare**: Run pre-checks, ensure backups current
3. **Execute**: During low-traffic period (typically 4-6 AM UTC)
4. **Verify**: Full test suite post-maintenance
5. **Communicate**: Update status page when complete

### Maintenance Checklist

```bash
# Pre-maintenance
python tools/admin/manage_zones.py status > pre_maintenance_status.txt
docker-compose -f infra/docker-compose.prod.yml ps > pre_maintenance_ps.txt

# During maintenance
# [Perform maintenance tasks]

# Post-maintenance
python tools/admin/manage_zones.py status
python tools/stress-test/test_multiplayer.py --quick
curl -s http://localhost:7778/health
# Verify all zones healthy
```

---

**Document End**
