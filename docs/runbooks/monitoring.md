# DarkAges MMO - Monitoring & Alerting Runbook

**Version:** 1.0  
**Last Updated:** 2026-02-02  
**Owner:** DEVOPS_AGENT

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Metrics Reference](#metrics-reference)
4. [Alerting Rules](#alerting-rules)
5. [Dashboards](#dashboards)
6. [Troubleshooting](#troubleshooting)
7. [Operational Procedures](#operational-procedures)

---

## Overview

The DarkAges MMO monitoring stack provides real-time visibility into server health, performance, and operational metrics. It consists of three main components:

| Component | Purpose | Port |
|-----------|---------|------|
| **Prometheus** | Metrics collection & storage | 9090 |
| **Grafana** | Visualization & dashboards | 3000 |
| **Alertmanager** | Alert routing & notification | 9093 |

### Quick Links

- Prometheus: http://localhost:9090
- Grafana: http://localhost:3000 (admin/darkages2026)
- Alertmanager: http://localhost:9093

---

## Architecture

```
┌─────────────────┐     ┌──────────────┐     ┌─────────────┐
│  Zone Servers   │────▶│  Prometheus  │────▶│   Grafana   │
│  (Port 8080+)   │     │  (Port 9090) │     │ (Port 3000) │
└─────────────────┘     └──────────────┘     └─────────────┘
                               │
                               ▼
                        ┌──────────────┐
                        │ Alertmanager │
                        │ (Port 9093)  │
                        └──────────────┘
```

### Data Flow

1. **Zone Servers** expose metrics at `/metrics` endpoint
2. **Prometheus** scrapes metrics every 5 seconds
3. **Grafana** queries Prometheus for visualization
4. **Prometheus** evaluates alerting rules
5. **Alertmanager** routes firing alerts to appropriate channels

---

## Metrics Reference

### Server Performance Metrics

| Metric | Type | Description | Thresholds |
|--------|------|-------------|------------|
| `darkages_ticks_total` | Counter | Total game ticks processed | N/A |
| `darkages_tick_duration_ms` | Gauge | Last tick duration in milliseconds | >16ms = warning |
| `darkages_tick_duration_hist` | Histogram | Tick duration distribution | N/A |

### Player Metrics

| Metric | Type | Description | Thresholds |
|--------|------|-------------|------------|
| `darkages_player_count` | Gauge | Current connected players | >800 = warning, >950 = critical |
| `darkages_player_capacity` | Gauge | Maximum player capacity | 1000 default |

### Memory Metrics

| Metric | Type | Description | Thresholds |
|--------|------|-------------|------------|
| `darkages_memory_used_bytes` | Gauge | Current memory usage | N/A |
| `darkages_memory_total_bytes` | Gauge | Total available memory | N/A |
| `memory_utilization` | Calculated | `used / total * 100` | >70% = warning, >85% = critical |

### Network Metrics

| Metric | Type | Description | Thresholds |
|--------|------|-------------|------------|
| `darkages_packets_sent_total` | Counter | Total packets sent | N/A |
| `darkages_packets_lost_total` | Counter | Total packets lost | N/A |
| `darkages_packet_loss_percent` | Gauge | Current packet loss % | >1% = warning, >5% = critical |
| `darkages_replication_bandwidth_bps` | Gauge | Replication bandwidth (bytes/sec) | >128KB/s = warning, >512KB/s = critical |
| `darkages_replication_bytes_total` | Counter | Total replication bytes | N/A |

### Database Metrics

| Metric | Type | Description | Thresholds |
|--------|------|-------------|------------|
| `darkages_db_connected` | Gauge | Database connection status (1=connected) | 0 = critical |

### Security Metrics

| Metric | Type | Description | Labels |
|--------|------|-------------|--------|
| `darkages_anticheat_violations_total` | Counter | Anti-cheat violations detected | zone_id, cheat_type, severity |
| `darkages_errors_total` | Counter | Total errors | zone_id |

### Prometheus Built-in Metrics

| Metric | Description |
|--------|-------------|
| `up{job="darkages-zones"}` | Target availability (1=up, 0=down) |
| `scrape_duration_seconds` | Time to scrape target |

---

## Alerting Rules

### Critical Alerts (Immediate Response Required)

#### HighTickTime
- **Condition:** `darkages_tick_duration_ms > 16` for 30s
- **Severity:** Critical
- **Impact:** Server tick budget exceeded, performance degradation
- **Action:** Check CPU usage, reduce zone population, activate QoS degradation

#### HighMemoryUsage
- **Condition:** `memory_used / memory_total > 0.8` for 1m
- **Severity:** Critical
- **Impact:** Risk of OOM crash
- **Action:** Restart zone server, investigate memory leak

#### ServerDown
- **Condition:** `up{job="darkages-zones"} == 0` for 10s
- **Severity:** Critical
- **Impact:** Zone server unavailable
- **Action:** Check server process, investigate crash logs

#### DatabaseConnectionFailed
- **Condition:** `darkages_db_connected == 0` for 10s
- **Severity:** Critical
- **Impact:** Cannot persist player data
- **Action:** Check Redis/ScyllaDB connectivity

### Warning Alerts (Monitor Closely)

#### HighErrorRate
- **Condition:** `rate(errors_total[1m]) > 10` for 1m
- **Severity:** Warning
- **Action:** Check server logs for error patterns

#### ZoneNearCapacity
- **Condition:** `player_count / capacity > 0.9` for 5m
- **Severity:** Warning
- **Action:** Prepare for zone splitting or player queue

#### HighPacketLoss
- **Condition:** `packets_lost / packets_sent > 0.05` for 2m
- **Severity:** Warning
- **Action:** Check network infrastructure, consider QoS degradation

#### HighReplicationBandwidth
- **Condition:** `rate(replication_bytes_total[1m]) > 1MB/s` for 5m
- **Severity:** Warning
- **Action:** Review replication settings, check for anomalies

### Infrastructure Alerts

#### RedisHighLatency
- **Condition:** `redis_command_duration_seconds{quantile="0.99"} > 1ms` for 2m
- **Severity:** Warning
- **Action:** Check Redis server load, network latency

---

## Dashboards

### DarkAges Server Health

**URL:** http://localhost:3000/d/darkages-server-health

#### Row 1: Core Performance
- **Tick Rate** (Stat): Current server tick rate (target: 60Hz)
- **Tick Duration** (Graph): Tick duration over time (threshold: 16ms)
- **Connected Players** (Stat): Total players across all zones
- **Memory Usage** (Gauge): Memory utilization percentage

#### Row 2: Player Metrics
- **Players per Zone** (Graph): Player count by zone
- **Zone Capacity** (Gauge): Capacity utilization by zone

#### Row 3: Network Metrics
- **Current Packet Loss** (Stat): Real-time packet loss percentage
- **Replication Bandwidth** (Stat): Current bandwidth usage
- **Packet Loss Rate** (Graph): Packet loss over time
- **Replication Bandwidth** (Graph): Bandwidth over time

#### Row 4: Security & Errors
- **Anti-Cheat Violations** (Graph): Violation rate by type
- **Error Rate** (Graph): Error rate over time

### Accessing Dashboards

1. Navigate to http://localhost:3000
2. Login with credentials: `admin` / `darkages2026`
3. Click "Dashboards" in the left sidebar
4. Select "DarkAges Server Health"

---

## Troubleshooting

### Prometheus Not Scraping Metrics

**Symptoms:**
- Empty graphs in Grafana
- `up{job="darkages-zones"} == 0`

**Diagnosis:**
```bash
# Check if zone server is running
curl http://localhost:8080/metrics

# Check Prometheus targets
open http://localhost:9090/targets
```

**Resolution:**
1. Verify zone server is running: `./darkages_server --zone-id 1`
2. Check firewall rules for port 8080
3. Verify Prometheus config: `prometheus.yml`

### High Tick Duration Alerts

**Symptoms:**
- `HighTickTime` alert firing
- Game lag reported by players

**Diagnosis:**
```promql
# Check tick duration trend
darkages_tick_duration_ms

# Check player count
darkages_player_count

# Check memory usage
darkages_memory_used_bytes / darkages_memory_total_bytes
```

**Resolution:**
1. Check if player count is high
2. Enable QoS degradation if >20ms
3. Profile server with Perfetto
4. Consider zone splitting if sustained

### Database Connection Issues

**Symptoms:**
- `DatabaseConnectionFailed` alert
- Player data not persisting

**Diagnosis:**
```bash
# Check Redis connectivity
docker exec darkages-redis redis-cli ping

# Check ScyllaDB
docker exec darkages-scylla nodetool status
```

**Resolution:**
1. Restart Redis: `docker restart darkages-redis`
2. Check network connectivity
3. Verify credentials in zone server config

### Alertmanager Not Sending Notifications

**Symptoms:**
- Alerts firing in Prometheus but no notifications

**Diagnosis:**
```bash
# Check Alertmanager status
curl http://localhost:9093/api/v1/status

# View active alerts
curl http://localhost:9093/api/v1/alerts
```

**Resolution:**
1. Check Alertmanager configuration
2. Verify receiver configuration (Slack webhook, email SMTP)
3. Check Alertmanager logs: `docker logs darkages-alertmanager`

---

## Operational Procedures

### Starting the Monitoring Stack

```bash
cd infra
docker-compose -f docker-compose.monitoring.yml up -d
```

### Stopping the Monitoring Stack

```bash
cd infra
docker-compose -f docker-compose.monitoring.yml down
```

### Viewing Logs

```bash
# Prometheus logs
docker logs darkages-prometheus

# Grafana logs
docker logs darkages-grafana

# Alertmanager logs
docker logs darkages-alertmanager
```

### Adding a New Zone Server Target

Edit `infra/prometheus/prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'darkages-zones'
    static_configs:
      - targets:
        - host.docker.internal:8080  # Zone 1
        - host.docker.internal:8081  # Zone 2 (new)
```

Reload Prometheus:
```bash
curl -X POST http://localhost:9090/-/reload
```

### Silencing Alerts

1. Navigate to http://localhost:9093
2. Click "Silences"
3. Create new silence with matchers:
   - `alertname=HighTickTime`
   - `zone_id=1`
4. Set duration and comment

### Maintenance Mode

To prevent alerts during maintenance:

```bash
# Silence all alerts for 1 hour
curl -X POST http://localhost:9093/api/v1/silences \
  -H 'Content-Type: application/json' \
  -d '{
    "matchers": [
      {"name": "severity", "value": ".*", "isRegex": true}
    ],
    "startsAt": "'$(date -u +%Y-%m-%dT%H:%M:%S)'",
    "endsAt": "'$(date -u -d '+1 hour' +%Y-%m-%dT%H:%M:%S)'",
    "comment": "Scheduled maintenance",
    "createdBy": "ops@darkages-mmo.com"
  }'
```

---

## Contact

For monitoring issues, contact:
- **Primary:** DEVOPS_AGENT
- **Escalation:** Technical Lead

---

## Changelog

| Date | Change | Author |
|------|--------|--------|
| 2026-02-02 | Initial documentation | DEVOPS_AGENT |
