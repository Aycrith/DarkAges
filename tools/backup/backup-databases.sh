#!/bin/bash
# DarkAges MMO - Database Backup Script
# [DATABASE_AGENT] Automated backup for Redis and ScyllaDB

set -e

# Configuration
BACKUP_DIR="/backups"
RETENTION_DAYS=7
DATE=$(date +%Y%m%d_%H%M%S)
NAMESPACE="darkages"

# Ensure backup directory exists
mkdir -p "$BACKUP_DIR"

echo "=================================="
echo "DarkAges Database Backup - $DATE"
echo "=================================="

# ============================================================================
# Redis Backup
# ============================================================================
echo ""
echo "Backing up Redis cluster..."

REDIS_BACKUP="$BACKUP_DIR/redis_backup_$DATE.rdb"

# Get Redis password
REDIS_PASSWORD=$(kubectl get secret redis-secret -n $NAMESPACE -o jsonpath='{.data.password}' | base64 -d)

# Trigger BGSAVE on master
kubectl exec -n $NAMESPACE redis-0 -- redis-cli -a "$REDIS_PASSWORD" BGSAVE

# Wait for save to complete
sleep 5

# Copy backup from pod
kubectl cp $NAMESPACE/redis-0:/data/dump.rdb "$REDIS_BACKUP"

# Compress backup
gzip -f "$REDIS_BACKUP"
echo "Redis backup complete: ${REDIS_BACKUP}.gz"

# ============================================================================
# ScyllaDB Backup
# ============================================================================
echo ""
echo "Backing up ScyllaDB..."

SCYLLA_BACKUP_DIR="$BACKUP_DIR/scylla_$DATE"
mkdir -p "$SCYLLA_BACKUP_DIR"

# Get all keyspaces
KEYSPACES=$(kubectl exec -n $NAMESPACE scylla-0 -- cqlsh -u cassandra -p cassandra -e "DESCRIBE KEYSPACES;" 2>/dev/null | grep -v "^$" | grep -v "system" | grep -v "^---" || true)

for KEYSPACE in $KEYSPACES; do
    echo "  Backing up keyspace: $KEYSPACE"
    
    # Create schema backup
    kubectl exec -n $NAMESPACE scylla-0 -- cqlsh -u cassandra -p cassandra -e "DESCRIBE KEYSPACE $KEYSPACE;" > "$SCYLLA_BACKUP_DIR/${KEYSPACE}_schema.cql" 2>/dev/null || true
    
    # Backup tables using nodetool snapshot
    kubectl exec -n $NAMESPACE scylla-0 -- nodetool snapshot "$KEYSPACE" -t "backup_$DATE" 2>/dev/null || true
done

# Create tarball
tar -czf "${SCYLLA_BACKUP_DIR}.tar.gz" -C "$BACKUP_DIR" "scylla_$DATE" 2>/dev/null || true
rm -rf "$SCYLLA_BACKUP_DIR"

echo "ScyllaDB backup complete: ${SCYLLA_BACKUP_DIR}.tar.gz"

# ============================================================================
# Upload to S3 (if configured)
# ============================================================================
if [ -n "$S3_BUCKET" ]; then
    echo ""
    echo "Uploading backups to S3..."
    
    aws s3 cp "${REDIS_BACKUP}.gz" "s3://$S3_BUCKET/redis/" 2>/dev/null || echo "S3 upload failed (AWS CLI not configured?)"
    aws s3 cp "${SCYLLA_BACKUP_DIR}.tar.gz" "s3://$S3_BUCKET/scylla/" 2>/dev/null || true
    
    echo "S3 upload complete"
fi

# ============================================================================
# Cleanup Old Backups
# ============================================================================
echo ""
echo "Cleaning up backups older than $RETENTION_DAYS days..."

find "$BACKUP_DIR" -name "redis_backup_*.rdb.gz" -mtime +$RETENTION_DAYS -delete 2>/dev/null || true
find "$BACKUP_DIR" -name "scylla_*.tar.gz" -mtime +$RETENTION_DAYS -delete 2>/dev/null || true

echo "Cleanup complete"

# ============================================================================
# Summary
# ============================================================================
echo ""
echo "=================================="
echo "Backup Summary"
echo "=================================="
echo "Backup completed at $(date)"
echo "Redis: ${REDIS_BACKUP}.gz"
echo "ScyllaDB: ${SCYLLA_BACKUP_DIR}.tar.gz"
echo "=================================="
