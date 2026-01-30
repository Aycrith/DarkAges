# DarkAges MMO - Deployment Runbook

## Overview

This runbook covers deployment procedures for the DarkAges MMO production environment.

## Deployment Types

| Type | Description | Risk Level | Rollback Time |
|------|-------------|------------|---------------|
| Hotfix | Critical bug fix | High | 5 min |
| Patch | Minor update | Medium | 15 min |
| Minor | Feature release | Medium | 30 min |
| Major | Breaking changes | High | 1 hour |

## Pre-Deployment Checklist

- [ ] All tests passing in CI/CD
- [ ] Staging environment validated
- [ ] Database migrations tested
- [ ] Rollback plan documented
- [ ] Monitoring alerts reviewed
- [ ] On-call engineer notified
- [ ] Maintenance window scheduled (if needed)

## Deployment Procedures

### Standard Deployment (Blue-Green)

```bash
#!/bin/bash
# deploy.sh - Blue-Green Deployment

NAMESPACE="darkages"
VERSION="${1:-latest}"

echo "Starting deployment of version $VERSION"

# 1. Deploy to green environment
echo "Deploying to green environment..."
kubectl set image deployment/zone-server-green \
  zone-server=darkages/zone-server:$VERSION \
  -n $NAMESPACE

# 2. Wait for green to be ready
echo "Waiting for green to be ready..."
kubectl rollout status deployment/zone-server-green -n $NAMESPACE --timeout=300s

# 3. Run smoke tests
echo "Running smoke tests..."
./scripts/smoke-tests.sh green

if [ $? -ne 0 ]; then
    echo "Smoke tests failed! Aborting deployment."
    kubectl rollout undo deployment/zone-server-green -n $NAMESPACE
    exit 1
fi

# 4. Switch traffic to green
echo "Switching traffic to green..."
kubectl patch service zone-server -n $NAMESPACE -p '{"spec":{"selector":{"version":"green"}}}'

# 5. Monitor for 5 minutes
echo "Monitoring for 5 minutes..."
sleep 300

# 6. Check error rates
ERROR_RATE=$(curl -s http://prometheus:9090/api/v1/query?query='rate(errors_total[5m])' | jq '.data.result[0].value[1]')
if (( $(echo "$ERROR_RATE > 0.01" | bc -l) )); then
    echo "Error rate too high! Rolling back..."
    kubectl patch service zone-server -n $NAMESPACE -p '{"spec":{"selector":{"version":"blue"}}}'
    exit 1
fi

# 7. Update blue for next deployment
echo "Updating blue environment..."
kubectl set image deployment/zone-server-blue \
  zone-server=darkages/zone-server:$VERSION \
  -n $NAMESPACE

kubectl rollout status deployment/zone-server-blue -n $NAMESPACE --timeout=300s

echo "Deployment complete!"
```

### Canary Deployment

```bash
#!/bin/bash
# canary-deploy.sh - Canary Deployment

NAMESPACE="darkages"
VERSION="${1:-latest}"
CANARY_PERCENTAGE="${2:-10}"

echo "Starting canary deployment: $VERSION ($CANARY_PERCENTAGE%)"

# 1. Deploy canary with small replica count
kubectl set image deployment/zone-server-canary \
  zone-server=darkages/zone-server:$VERSION \
  -n $NAMESPACE

# Scale to desired percentage
kubectl scale deployment/zone-server-canary \
  --replicas=$CANARY_PERCENTAGE \
  -n $NAMESPACE

# 2. Wait for ready
kubectl rollout status deployment/zone-server-canary -n $NAMESPACE --timeout=300s

# 3. Start traffic split
echo "Starting traffic split..."
kubectl patch virtualservice zone-server -n $NAMESPACE --type merge -p "
spec:
  http:
  - route:
    - destination:
        host: zone-server-canary
      weight: $CANARY_PERCENTAGE
    - destination:
        host: zone-server-stable
      weight: $((100 - CANARY_PERCENTAGE))
"

# 4. Monitor for 15 minutes
echo "Monitoring canary for 15 minutes..."
sleep 900

# 5. Check metrics
CANARY_ERRORS=$(curl -s 'http://prometheus:9090/api/v1/query?query=rate(errors_total{version="canary"}[5m])' | jq '.data.result[0].value[1]')
STABLE_ERRORS=$(curl -s 'http://prometheus:9090/api/v1/query?query=rate(errors_total{version="stable"}[5m])' | jq '.data.result[0].value[1]')

if (( $(echo "$CANARY_ERRORS > $STABLE_ERRORS * 2" | bc -l) )); then
    echo "Canary error rate too high! Rolling back..."
    kubectl patch virtualservice zone-server -n $NAMESPACE --type merge -p '
spec:
  http:
  - route:
    - destination:
        host: zone-server-stable
      weight: 100
'
    kubectl scale deployment/zone-server-canary --replicas=0 -n $NAMESPACE
    exit 1
fi

# 6. Promote canary to full deployment
echo "Promoting canary to full deployment..."
kubectl set image deployment/zone-server-stable \
  zone-server=darkages/zone-server:$VERSION \
  -n $NAMESPACE

kubectl rollout status deployment/zone-server-stable -n $NAMESPACE --timeout=300s

# 7. Reset traffic split
kubectl patch virtualservice zone-server -n $NAMESPACE --type merge -p '
spec:
  http:
  - route:
    - destination:
        host: zone-server-stable
      weight: 100
'

# 8. Scale down canary
kubectl scale deployment/zone-server-canary --replicas=0 -n $NAMESPACE

echo "Canary deployment complete!"
```

### Database Migration

```bash
#!/bin/bash
# migrate-database.sh - Database Migrations

NAMESPACE="darkages"
MIGRATION_NAME="${1}"

echo "Running database migration: $MIGRATION_NAME"

# 1. Create backup first
echo "Creating backup..."
./tools/backup/backup-databases.sh

# 2. Run migration as job
cat <<EOF | kubectl apply -f -
apiVersion: batch/v1
kind: Job
metadata:
  name: db-migration-$MIGRATION_NAME
  namespace: $NAMESPACE
spec:
  template:
    spec:
      containers:
      - name: migration
        image: darkages/migrations:latest
        command: ["./migrate", "$MIGRATION_NAME"]
        env:
        - name: REDIS_HOST
          value: redis.darkages.svc.cluster.local
        - name: SCYLLA_HOST
          value: scylla.darkages.svc.cluster.local
      restartPolicy: Never
  backoffLimit: 0
EOF

# 3. Wait for completion
echo "Waiting for migration to complete..."
kubectl wait --for=condition=complete job/db-migration-$MIGRATION_NAME -n $NAMESPACE --timeout=600s

if [ $? -ne 0 ]; then
    echo "Migration failed! Check logs:"
    kubectl logs job/db-migration-$MIGRATION_NAME -n $NAMESPACE
    
    echo "Consider restoring from backup:"
    echo "  ./tools/backup/restore-databases.sh <backup-file>"
    exit 1
fi

echo "Migration complete!"

# 4. Cleanup
kubectl delete job db-migration-$MIGRATION_NAME -n $NAMESPACE
```

## Rollback Procedures

### Immediate Rollback (Emergency)

```bash
# Rollback Kubernetes deployment
kubectl rollout undo deployment/zone-server -n darkages

# Or rollback to specific revision
kubectl rollout undo deployment/zone-server -n darkages --to-revision=3
```

### Database Rollback

```bash
# Restore from backup
./tools/backup/restore-databases.sh /backups/redis_backup_20260130_120000.rdb.gz
./tools/backup/restore-databases.sh /backups/scylla_20260130_120000.tar.gz
```

### Traffic Rollback (Istio)

```bash
# Route all traffic back to stable
kubectl patch virtualservice zone-server -n darkages --type merge -p '
spec:
  http:
  - route:
    - destination:
        host: zone-server-stable
      weight: 100
'
```

## Deployment Verification

### Health Checks

```bash
# Check all pods ready
kubectl get pods -n darkages

# Check service endpoints
kubectl get endpoints zone-server -n darkages

# Test game connectivity
./scripts/test-connectivity.sh

# Check error rates
open http://grafana.darkages.local/d/server-health
```

### Post-Deployment Monitoring

Monitor for 30 minutes after deployment:

1. **Error Rate:** Should be <1%
2. **Latency:** P95 should be <50ms
3. **Player Count:** Should not drop significantly
4. **Tick Rate:** Should maintain 60Hz
5. **Memory Usage:** Should be stable

## Deployment Schedule

| Day | Time (UTC) | Type | Notes |
|-----|------------|------|-------|
| Tuesday | 14:00 | Standard | Low player count |
| Thursday | 14:00 | Standard | Low player count |
| Emergency | Any | Hotfix | On-call approval required |

## Emergency Contacts

- **SRE Lead:** sre-lead@darkages-mmo.com
- **On-Call:** oncall@darkages-mmo.com
- **Slack:** #deployments

## Related Documentation

- [Incident Response](./incident-response.md)
- [Troubleshooting](./troubleshooting.md)
- [Monitoring Playbooks](./monitoring.md)
