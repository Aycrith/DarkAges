#!/bin/bash
# DarkAges Production Deployment Script
# Usage: deploy.sh [staging|production] [version|latest] [--zone N]

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
COMPOSE_FILE="$PROJECT_ROOT/infra/docker-compose.prod.yml"
PROJECT_NAME="darkages"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
ENVIRONMENT=${1:-staging}
VERSION=${2:-latest}
SPECIFIC_ZONE=""

# Parse optional flags
shift 2 || true
while [[ $# -gt 0 ]]; do
    case $1 in
        --zone)
            SPECIFIC_ZONE="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Validate environment
if [[ "$ENVIRONMENT" != "staging" && "$ENVIRONMENT" != "production" ]]; then
    log_error "Environment must be 'staging' or 'production'"
    echo "Usage: $0 [staging|production] [version] [--zone N]"
    exit 1
fi

log_info "Deploying DarkAges to $ENVIRONMENT (version: $VERSION)"

# Confirm production deployment
if [[ "$ENVIRONMENT" == "production" && -z "$SPECIFIC_ZONE" ]]; then
    echo ""
    log_warning "You are about to deploy to PRODUCTION"
    read -p "Are you sure? Type 'yes' to continue: " confirm
    if [[ "$confirm" != "yes" ]]; then
        log_info "Deployment cancelled"
        exit 0
    fi
fi

# Check if docker-compose file exists
if [[ ! -f "$COMPOSE_FILE" ]]; then
    log_error "Docker compose file not found: $COMPOSE_FILE"
    exit 1
fi

# Check if zone management tool is available
MANAGE_ZONES="$PROJECT_ROOT/tools/admin/manage_zones.py"
if [[ ! -f "$MANAGE_ZONES" ]]; then
    log_warning "Zone management tool not found at $MANAGE_ZONES"
    MANAGE_ZONES=""
fi

# Pre-deployment checks
log_info "Running pre-deployment checks..."

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    log_error "Docker is not running"
    exit 1
fi

# Check disk space
DISK_USAGE=$(df / | tail -1 | awk '{print $5}' | sed 's/%//')
if [[ $DISK_USAGE -gt 85 ]]; then
    log_error "Disk usage is at ${DISK_USAGE}%. Free up space before deploying."
    exit 1
fi
log_info "Disk usage: ${DISK_USAGE}%"

# Pre-deployment health check
if [[ -n "$MANAGE_ZONES" && -z "$SPECIFIC_ZONE" ]]; then
    log_info "Checking current zone status..."
    python3 "$MANAGE_ZONES" status || log_warning "Could not get zone status"
fi

# Pull latest images
log_info "Pulling images..."
docker-compose -f "$COMPOSE_FILE" -p "$PROJECT_NAME" pull || {
    log_error "Failed to pull images"
    exit 1
}

# Set image version for zones
export DARKAGES_IMAGE_VERSION="$VERSION"

# Determine deployment strategy
if [[ -n "$SPECIFIC_ZONE" ]]; then
    # Deploy to specific zone only (canary)
    ZONES=("$SPECIFIC_ZONE")
    log_info "Canary deployment to Zone $SPECIFIC_ZONE"
else
    # Deploy to all zones
    ZONES=(1 2 3 4)
    log_info "Full deployment to all zones"
fi

# Rolling deployment
log_info "Starting rolling deployment..."

for zone_id in "${ZONES[@]}"; do
    ZONE_CONTAINER="${PROJECT_NAME}-zone-${zone_id}"
    ZONE_PORT=$((7777 + (zone_id-1)*2))
    MONITOR_PORT=$((7777 + (zone_id-1)*2 + 1))
    
    log_info "Deploying zone-$zone_id..."
    
    # Check if zone exists
    if docker ps -a --format '{{.Names}}' | grep -q "^${ZONE_CONTAINER}$"; then
        # Drain connections (if supported by server)
        log_info "  Draining connections from zone-$zone_id..."
        if curl -sf "http://localhost:${MONITOR_PORT}/admin/drain" -X POST -H "Authorization: Bearer ${ADMIN_TOKEN:-}" 2>/dev/null; then
            log_info "  Drain initiated, waiting 10s..."
            sleep 10
        else
            log_warning "  Could not drain connections (endpoint may not be available)"
        fi
        
        # Graceful stop
        log_info "  Stopping zone-$zone_id..."
        docker-compose -f "$COMPOSE_FILE" -p "$PROJECT_NAME" stop "zone-${zone_id}" || {
            log_warning "  Stop failed, forcing..."
            docker-compose -f "$COMPOSE_FILE" -p "$PROJECT_NAME" kill "zone-${zone_id}" || true
        }
    fi
    
    # Start new version
    log_info "  Starting zone-$zone_id (version: $VERSION)..."
    docker-compose -f "$COMPOSE_FILE" -p "$PROJECT_NAME" up -d "zone-${zone_id}" || {
        log_error "  Failed to start zone-$zone_id"
        exit 1
    }
    
    # Wait for health check
    log_info "  Waiting for zone-$zone_id to be healthy..."
    HEALTHY=false
    for i in {1..60}; do
        if curl -sf "http://localhost:${MONITOR_PORT}/health" > /dev/null 2>&1; then
            HEALTHY=true
            break
        fi
        sleep 2
    done
    
    if [[ "$HEALTHY" != "true" ]]; then
        log_error "  Zone $zone_id failed health check after 120s"
        log_error "  Check logs: docker logs $ZONE_CONTAINER"
        exit 1
    fi
    
    log_success "  Zone $zone_id is healthy"
    
    # Verify no errors in logs
    sleep 5
    ERROR_COUNT=$(docker logs "$ZONE_CONTAINER" --since 30s 2>&1 | grep -c "ERROR\|FATAL" || true)
    if [[ "$ERROR_COUNT" -gt 0 ]]; then
        log_warning "  $ERROR_COUNT errors detected in zone $zone_id logs"
        docker logs "$ZONE_CONTAINER" --since 30s 2>&1 | grep "ERROR\|FATAL" | tail -5
    fi
    
    # Wait between zones for rolling deployment
    if [[ "$zone_id" != "${ZONES[-1]}" ]]; then
        log_info "  Waiting 30s before next zone..."
        sleep 30
    fi
done

log_success "Rolling deployment complete"

# Post-deployment verification
log_info "Running post-deployment verification..."

# Health check all zones
ALL_HEALTHY=true
for zone_id in 1 2 3 4; do
    MONITOR_PORT=$((7777 + (zone_id-1)*2 + 1))
    if curl -sf "http://localhost:${MONITOR_PORT}/health" > /dev/null 2>&1; then
        log_success "  Zone $zone_id: HEALTHY"
    else
        log_error "  Zone $zone_id: UNHEALTHY"
        ALL_HEALTHY=false
    fi
done

# Run quick stress test if available
if [[ -f "$PROJECT_ROOT/tools/stress-test/test_multiplayer.py" ]]; then
    log_info "Running quick multiplayer test..."
    python3 "$PROJECT_ROOT/tools/stress-test/test_multiplayer.py" --quick || {
        log_warning "Quick test failed - manual verification recommended"
    }
fi

# Final status
if [[ "$ALL_HEALTHY" == "true" ]]; then
    log_success "Deployment to $ENVIRONMENT completed successfully!"
    echo ""
    echo "Access points:"
    echo "  Grafana:    http://localhost:3000"
    echo "  Prometheus: http://localhost:9090"
    echo "  Alertmanager: http://localhost:9093"
    echo ""
    echo "Zone ports:"
    for zone_id in 1 2 3 4; do
        echo "  Zone $zone_id: localhost:$((7777 + (zone_id-1)*2))"
    done
else
    log_error "Some zones are unhealthy. Check logs and investigate."
    exit 1
fi

# Record deployment
DEPLOY_LOG="$PROJECT_ROOT/deployments.log"
echo "$(date -Iseconds) - $ENVIRONMENT - $VERSION - $(whoami)" >> "$DEPLOY_LOG"

exit 0
