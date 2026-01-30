# DarkAges MMO - Production Deployment Guide

**Version**: 1.0  
**Date**: 2026-01-29

---

## Overview

This guide covers the production deployment of the DarkAges MMO server infrastructure, including:

- 4 Zone Servers (2x2 grid, 1000x1000 units each)
- Redis (hot state caching)
- ScyllaDB (persistent storage)
- Monitoring Stack (Prometheus, Grafana, Alertmanager)
- Logging Stack (Loki, Promtail)
- Load Balancer (Nginx)
- Automated Backups

---

## Quick Start

### Prerequisites

- Docker Engine 20.10+
- Docker Compose 2.0+
- 16GB+ RAM
- 4+ CPU cores
- 50GB+ free disk space

### 1. Environment Setup

```bash
# Copy and configure environment variables
cp .env.example .env

# Edit .env with your production values
nano .env
```

**Required environment variables:**
- `REDIS_PASSWORD` - Secure password for Redis
- `SCYLLA_PASSWORD` - Secure password for ScyllaDB
- `GRAFANA_PASSWORD` - Admin password for Grafana
- `ADMIN_TOKEN` - API token for admin endpoints

### 2. Deploy

```bash
# Deploy all services
docker-compose -f infra/docker-compose.prod.yml up -d

# Check status
docker-compose -f infra/docker-compose.prod.yml ps

# View logs
docker-compose -f infra/docker-compose.prod.yml logs -f
```

### 3. Verify Deployment

```bash
# Check all zones are healthy
for port in 7778 7780 7782 7784; do
  curl -s http://localhost:$port/health
done

# Check monitoring
curl http://localhost:9090/api/v1/status/targets  # Prometheus
curl http://localhost:3000/api/health             # Grafana

# Run quick test
python tools/stress-test/test_multiplayer.py --quick
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Nginx Load Balancer                      │
│                     (HTTP/HTTPS + UDP Proxy)                     │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   Zone 1     │◄──►│    Redis     │    │  Prometheus  │
│ (-1000,-1000)│    │  (Hot State) │    │ (Monitoring) │
└──────────────┘    └──────────────┘    └──────────────┘
        │                     │                     │
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   Zone 2     │◄──►│   ScyllaDB   │    │   Grafana    │
│  (0,-1000)   │    │(Persistence) │    │(Dashboards)  │
└──────────────┘    └──────────────┘    └──────────────┘
        │                                         │
┌──────────────┐                         ┌──────────────┐
│   Zone 3     │                         │ Alertmanager │
│   (-1000,0)  │                         │  (Alerts)    │
└──────────────┘                         └──────────────┘
        │                                         │
┌──────────────┐                         ┌──────────────┐
│   Zone 4     │                         │     Loki     │
│    (0,0)     │                         │   (Logs)     │
└──────────────┘                         └──────────────┘
```

---

## Service Details

### Zone Servers

| Zone | Position | Game Port | Monitor Port | Adjacent |
|------|----------|-----------|--------------|----------|
| Zone 1 | (-1000,-1000) to (0,0) | 7777 | 7778 | 2, 3 |
| Zone 2 | (0,-1000) to (1000,0) | 7779 | 7780 | 1, 4 |
| Zone 3 | (-1000,0) to (0,1000) | 7781 | 7782 | 1, 4 |
| Zone 4 | (0,0) to (1000,1000) | 7783 | 7784 | 2, 3 |

**Resource Limits:**
- CPU: 2 cores (limit), 1 core (reserved)
- Memory: 2GB (limit), 1GB (reserved)
- Max Players: 400 per zone

### Redis

- **Image**: redis:7-alpine
- **Memory Limit**: 2GB
- **Persistence**: AOF enabled
- **Password Protected**: Yes (set in .env)

### ScyllaDB

- **Image**: scylladb/scylla:5.2
- **CPU**: 4 cores
- **Memory**: 4GB
- **Authentication**: Enabled

### Monitoring Stack

| Service | Port | Purpose |
|---------|------|---------|
| Prometheus | 9090 | Metrics collection |
| Grafana | 3000 | Dashboards |
| Alertmanager | 9093 | Alert routing |
| Loki | 3100 | Log aggregation |
| Node Exporter | - | Host metrics |

---

## Monitoring

### Grafana Dashboards

Access: http://localhost:3000 (or your configured URL)

| Dashboard | Description |
|-----------|-------------|
| DarkAges Main | Overview of all zones and key metrics |
| DarkAges Zones | Detailed per-zone metrics |

### Key Metrics

| Metric | Normal | Warning | Critical |
|--------|--------|---------|----------|
| Tick Time | <16ms | 16-20ms | >20ms |
| FPS | 60 | 55-59 | <55 |
| Memory Usage | <70% | 70-85% | >85% |
| Player Count | <300 | 300-350 | >350 |
| Latency (p99) | <50ms | 50-100ms | >100ms |

### Alerts

Alerts are configured in `infra/monitoring/alerts.yml`:

- **Critical**: Zone offline, DDoS detected, database down
- **Warning**: High tick time, low FPS, high memory, migration failures
- **Info**: QoS degradation active

---

## Operations

### Deployment

```bash
# Using the deployment script
tools/deploy/deploy.sh production latest

# Or manual rolling deployment
docker-compose -f infra/docker-compose.prod.yml pull
docker-compose -f infra/docker-compose.prod.yml up -d
```

### Zone Management

```bash
# Check status
python tools/admin/manage_zones.py status

# Restart a zone
python tools/admin/manage_zones.py restart --zone 1

# View logs
python tools/admin/manage_zones.py logs --zone 1 --follow
```

### Backup & Restore

```bash
# Automated backups run daily at 2 AM
# Manual backup:
docker exec darkages-redis redis-cli BGSAVE
docker exec darkages-scylla nodetool snapshot darkages

# Restore from backup:
# 1. Stop services
# 2. Copy backup files to volumes
# 3. Start services
```

---

## Troubleshooting

### Check Service Health

```bash
# All services
docker-compose -f infra/docker-compose.prod.yml ps

# Specific zone
curl http://localhost:7778/health

# Metrics endpoint
curl http://localhost:7778/metrics
```

### View Logs

```bash
# All services
docker-compose -f infra/docker-compose.prod.yml logs

# Specific zone
docker-compose -f infra/docker-compose.prod.yml logs -f zone-1

# Via Loki (Grafana)
# Open http://localhost:3000/explore
```

### Common Issues

See [docs/operations/RUNBOOK.md](../docs/operations/RUNBOOK.md) for detailed troubleshooting procedures.

---

## Security

### Default Security Measures

- Redis password authentication
- ScyllaDB password authentication
- Nginx basic auth for monitoring endpoints
- Resource limits on all containers
- Health checks on all services

### Additional Hardening

1. **SSL/TLS**: Configure certificates in `infra/nginx/ssl/`
2. **Firewall**: Restrict port access (only 80/443/7777/7779/7781/7783 needed externally)
3. **VPN**: Consider VPN access for monitoring endpoints
4. **Secrets**: Use Docker secrets or external vault for production

---

## Performance Tuning

### Zone Server

Edit `docker-compose.prod.yml` environment variables:

```yaml
environment:
  - MAX_PLAYERS_PER_ZONE=400
  - TICK_RATE=60
  - ENABLE_PROFILING=false
  - DDOS_PROTECTION=true
```

### Resource Scaling

```yaml
deploy:
  resources:
    limits:
      cpus: '2.0'      # Increase for more players
      memory: 2G       # Increase if OOM
```

---

## Maintenance

### Updates

```bash
# Pull latest images
docker-compose -f infra/docker-compose.prod.yml pull

# Rolling restart
tools/deploy/deploy.sh production latest
```

### Cleanup

```bash
# Remove old images
docker image prune -a

# Clean logs (if not using Loki)
docker-compose -f infra/docker-compose.prod.yml logs --tail 1000

# Clean stopped containers
docker container prune
```

---

## Support

- **Runbook**: [docs/operations/RUNBOOK.md](../docs/operations/RUNBOOK.md)
- **Monitoring**: http://localhost:3000
- **Emergency**: Follow escalation matrix in runbook

---

**Document End**
