# DarkAges MMO - Metrics Quick Reference

**One-page reference for all Prometheus metrics**

---

## Core Server Metrics

```promql
# Tick rate (should be ~60Hz)
1000 / darkages_tick_duration_ms

# Tick duration (should be <16ms)
darkages_tick_duration_ms

# Total ticks processed
darkages_ticks_total
```

## Player Metrics

```promql
# Total players across all zones
sum(darkages_player_count)

# Players by zone
darkages_player_count

# Zone capacity percentage
darkages_player_count / darkages_player_capacity * 100
```

## Memory Metrics

```promql
# Memory utilization %
darkages_memory_used_bytes / darkages_memory_total_bytes * 100

# Memory used in MB
darkages_memory_used_bytes / 1024 / 1024
```

## Network Metrics

```promql
# Current packet loss %
darkages_packet_loss_percent

# Packet loss rate over time
rate(darkages_packets_lost_total[1m]) / rate(darkages_packets_sent_total[1m]) * 100

# Replication bandwidth (KB/s)
darkages_replication_bandwidth_bps / 1024

# Total bandwidth used
rate(darkages_replication_bytes_total[5m])
```

## Security Metrics

```promql
# Anti-cheat violations by type
rate(darkages_anticheat_violations_total[5m])

# Violations by severity
sum(rate(darkages_anticheat_violations_total[5m])) by (severity)

# Error rate
rate(darkages_errors_total[1m])
```

## Database Metrics

```promql
# Database connection status (1=connected)
darkages_db_connected

# Redis latency (if available)
redis_command_duration_seconds{quantile="0.99"}
```

## Useful Queries

### Server Health Scorecard

```promql
# Tick performance (0-100)
clamp_max(100 - ((darkages_tick_duration_ms - 8) / 8 * 100), 100)

# Player capacity headroom
100 - (darkages_player_count / darkages_player_capacity * 100)

# Memory headroom %
100 - (darkages_memory_used_bytes / darkages_memory_total_bytes * 100)
```

### Alert Investigation

```promql
# Which zone has highest tick time?
topk(3, darkages_tick_duration_ms)

# Which zone has most players?
topk(3, darkages_player_count)

# Packet loss by zone
darkages_packet_loss_percent > 0
```

---

## Thresholds Summary

| Metric | Warning | Critical |
|--------|---------|----------|
| Tick Duration | >16ms | >20ms |
| Memory Usage | >70% | >85% |
| Player Capacity | >90% | >95% |
| Packet Loss | >1% | >5% |
| Replication BW | >128KB/s | >512KB/s |

---

## Grafana Dashboard URLs

- Main Dashboard: http://localhost:3000/d/darkages-server-health
- Prometheus: http://localhost:9090
- Alertmanager: http://localhost:9093

---

*Last updated: 2026-02-02*
