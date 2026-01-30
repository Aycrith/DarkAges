# DarkAges MMO - Infrastructure

**Version:** 1.0  
**Last Updated:** 2026-01-30

---

## Overview

This directory contains all infrastructure configurations for the DarkAges MMO project, including:
- Docker Compose stacks for development and production
- Database schemas and configurations
- Monitoring and observability (Prometheus, Grafana, Loki)
- CI/CD configurations

---

## Quick Start

### Prerequisites

- Docker Desktop 4.0+ or Docker Engine 20.10+
- Docker Compose 2.0+
- 8GB+ RAM available for containers

### Start Development Environment

```bash
# Basic stack (Redis + ScyllaDB)
docker-compose up -d

# Full stack with monitoring (WP-8-1)
docker-compose -f docker-compose.phase6.yml --profile full up -d

# Check status
docker-compose ps

# View logs
docker-compose logs -f

# Stop everything
docker-compose down
```

---

## Docker Compose Stacks

### 1. Basic Stack (`docker-compose.yml`)

**Services:**
| Service | Port | Purpose |
|---------|------|---------|
| Redis | 6379 | Hot state cache (WP-6-2) |
| ScyllaDB | 9042 | Persistence (WP-6-3) |
| Redis Commander | 8081 | Redis UI (debug) |

**Usage:**
```bash
docker-compose up -d
```

### 2. Phase 6 Stack (`docker-compose.phase6.yml`)

**Services:**

#### Core Services (profile: `core`)
| Service | Port | Purpose |
|---------|------|---------|
| Redis | 6379 | Hot state cache (WP-6-2) |
| ScyllaDB | 9042 | Persistence (WP-6-3) |
| Scylla Init | - | Schema initialization |

#### Monitoring Services (profile: `monitoring`)
| Service | Port | Purpose |
|---------|------|---------|
| Prometheus | 9090 | Metrics collection (WP-8-1) |
| Grafana | 3000 | Visualization (WP-8-1) |
| Loki | 3100 | Log aggregation (WP-8-1) |
| Promtail | - | Log shipping |

#### Debug Services (profile: `debug`)
| Service | Port | Purpose |
|---------|------|---------|
| Redis Commander | 8081 | Redis UI |
| Scylla Manager | 5080 | ScyllaDB management |
| Mailhog | 8025 | Email testing |

**Usage:**
```bash
# Core services only
docker-compose -f docker-compose.phase6.yml --profile core up -d

# Core + monitoring
docker-compose -f docker-compose.phase6.yml --profile core --profile monitoring up -d

# Everything (including debug tools)
docker-compose -f docker-compose.phase6.yml --profile full up -d
```

### 3. Multi-Zone Stack (`docker-compose.multi-zone.yml`)

For testing zone sharding with multiple zone servers.

**Usage:**
```bash
docker-compose -f docker-compose.multi-zone.yml up -d
```

---

## Database Management

### Redis

**Connect:**
```bash
# Via CLI
docker exec -it darkages-redis redis-cli

# Via Redis Commander (if running)
open http://localhost:8081
```

**Common Commands:**
```bash
# Check connection
redis-cli ping

# View all keys
redis-cli KEYS '*'

# View session
redis-cli GET session:12345

# Monitor in real-time
redis-cli MONITOR
```

**Configuration:**
- Config file: `redis/redis.conf`
- Max memory: 256MB (configurable)
- Persistence: AOF enabled

### ScyllaDB

**Connect:**
```bash
# Via CQLSH
docker exec -it darkages-scylla cqlsh

# Via Scylla Manager (if running)
open http://localhost:5080
```

**Common Commands:**
```bash
# List keyspaces
cqlsh -e "DESCRIBE KEYSPACES"

# Use darkages keyspace
cqlsh -e "USE darkages; DESCRIBE TABLES"

# Query combat events
cqlsh -e "SELECT * FROM darkages.combat_events LIMIT 10"

# Check node status
docker exec darkages-scylla nodetool status
```

**Schema:**
- Initialization: `scylla/init.cql`
- Schema documentation: `../docs/DATABASE_SCHEMA.md`

**Performance Tuning:**
```yaml
# In docker-compose.yml
command: --developer-mode=1 --smp=2 --memory=2G --overprovisioned
```
- `--smp`: Number of CPU cores
- `--memory`: Memory limit
- `--overprovisioned`: Optimize for shared environments

---

## Monitoring (WP-8-1)

### Access Dashboards

| Service | URL | Default Credentials |
|---------|-----|---------------------|
| Prometheus | http://localhost:9090 | - |
| Grafana | http://localhost:3000 | admin/admin |
| Loki | http://localhost:3100 | - |

### Grafana

**First-time Setup:**
1. Open http://localhost:3000
2. Login with admin/admin
3. Change password when prompted
4. Prometheus datasource is pre-configured

**Import Dashboards:**
1. Go to + → Import
2. Upload JSON from `monitoring/grafana/dashboards/`
3. Select Prometheus as datasource

### Prometheus

**Query Examples:**
```promql
# Player count
players_online

# Tick duration (p99)
histogram_quantile(0.99, tick_duration_seconds_bucket)

# Packet rate
rate(packets_sent_total[1m])

# Error rate
rate(errors_total[1m])
```

### Loki

**Query Examples:**
```
# All server logs
{job="darkages-server"}

# Errors only
{job="darkages-server"} |= "ERROR"

# Specific player
{job="darkages-server"} |= "player_id=12345"

# Slow ticks
{job="darkages-server"} |= "tick_overrun"
```

---

## Troubleshooting

### Containers Won't Start

```bash
# Check logs
docker-compose logs <service-name>

# Common issues:
# 1. Port conflicts - change ports in docker-compose.yml
# 2. Insufficient memory - increase Docker Desktop memory limit
# 3. Volume permissions - docker-compose down -v to reset
```

### ScyllaDB Won't Initialize

```bash
# Check if ScyllaDB is ready
docker exec darkages-scylla nodetool status

# Manual schema initialization
docker exec -i darkages-scylla cqlsh < scylla/init.cql

# Reset ScyllaDB (WARNING: loses all data)
docker-compose down -v
docker-compose up -d scylla
sleep 60  # Wait for startup
docker-compose up -d scylla-init
```

### Redis Connection Issues

```bash
# Test connection
docker exec darkages-redis redis-cli ping

# Should return: PONG

# If not, check logs
docker-compose logs redis
```

### Monitoring Stack Issues

```bash
# Check Prometheus targets
open http://localhost:9090/targets

# Check Grafana datasources
# Configuration → Datasources → Test

# Reset Grafana (loses dashboards)
docker-compose down -v grafana
```

---

## Production Considerations

### Resource Requirements

| Service | CPU | Memory | Storage |
|---------|-----|--------|---------|
| Redis | 1 core | 256MB+ | 1GB+ |
| ScyllaDB | 2+ cores | 4GB+ | 50GB+ |
| Prometheus | 1 core | 2GB+ | 20GB+ |
| Grafana | 0.5 core | 512MB | 1GB |

### Security

1. **Change default passwords**
   - Grafana: Change admin password immediately
   - ScyllaDB: Enable authentication in production
   - Redis: Enable AUTH and use TLS

2. **Network isolation**
   - Use internal Docker networks
   - Don't expose database ports publicly
   - Use reverse proxy (nginx/traefik) for Grafana

3. **Encryption**
   - Enable TLS for Redis
   - Enable SSL for ScyllaDB client connections
   - Use HTTPS for Grafana (via reverse proxy)

### Backup Strategy

```bash
# Redis backup
docker exec darkages-redis redis-cli BGSAVE
docker cp darkages-redis:/data/dump.rdb ./backups/redis-$(date +%Y%m%d).rdb

# ScyllaDB backup
docker exec darkages-scylla nodetool snapshot darkages
# Copy snapshot files from /var/lib/scylla/data
```

---

## Development Workflow

### Daily Development

```bash
# 1. Start infrastructure
docker-compose up -d

# 2. Build server
../build.ps1 -Test

# 3. Run server
../build/bin/darkages-server --port 7777 --zone 1

# 4. Monitor
# Open Grafana at http://localhost:3000
```

### Testing Changes

```bash
# Reset databases to clean state
docker-compose down -v
docker-compose up -d

# Or just reset ScyllaDB schema
docker-compose restart scylla-init
```

### Adding New Services

1. Add service to `docker-compose.phase6.yml`
2. Add profile (`core`, `monitoring`, or `debug`)
3. Update this README
4. Add to monitoring if applicable

---

## Reference

### Ports Used

| Port | Service | Purpose |
|------|---------|---------|
| 3000 | Grafana | Web UI |
| 3100 | Loki | Log API |
| 5080 | Scylla Manager | Management UI |
| 6379 | Redis | Cache |
| 7777 | DarkAges Server | Game server |
| 8025 | Mailhog | Email UI |
| 8081 | Redis Commander | Redis UI |
| 9042 | ScyllaDB | CQL |
| 9090 | Prometheus | Metrics API |
| 9100 | DarkAges Metrics | Prometheus exporter |
| 9180 | ScyllaDB | Prometheus metrics |
| 1025 | Mailhog | SMTP |

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `REDIS_HOST` | localhost | Redis hostname |
| `REDIS_PORT` | 6379 | Redis port |
| `SCYLLA_HOST` | localhost | ScyllaDB hostname |
| `SCYLLA_PORT` | 9042 | ScyllaDB CQL port |
| `PROMETHEUS_PORT` | 9100 | Server metrics port |

---

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-30 | AI Coordinator | Initial infrastructure documentation |
