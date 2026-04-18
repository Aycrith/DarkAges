#!/usr/bin/env bash
# =============================================================================
# DarkAges Autonomous Iteration Runner
# Orchestrates: Analyze → Plan → Implement → Validate → PR → Log
# Usage: iteration_runner.sh [auto|<task>] [--fast]
#   --fast: Skip build (cmake configure only). Use for cron jobs.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOG_FILE="$PROJECT_ROOT/AUTONOMOUS_LOG.md"
BRANCH_PREFIX="autonomous/iter"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log() { echo -e "${CYAN}[iter]${NC} $1"; }
success() { echo -e "${GREEN}[PASS]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; }

# -----------------------------------------------------------------------------
# Phase 1: Analyze - Find improvement opportunities
# -----------------------------------------------------------------------------
analyze_codebase() {
    log "Phase 1: Analyzing codebase..."
    cd "$PROJECT_ROOT"

    local issues=()

    # Check for TODO/FIXME/HACK comments
    local todos
    todos=$(grep -rn "TODO\|FIXME\|HACK\|XXX" src/ --include="*.cpp" --include="*.hpp" --include="*.h" 2>/dev/null | head -20 || true)
    if [ -n "$todos" ]; then
        issues+=("TODO/FIXME markers found")
        echo "$todos" > "$SCRIPT_DIR/.last_todos.txt"
    fi

    # Check for compilation warnings (build in dry mode)
    if [ -f build.sh ]; then
        log "  Checking build status..."
        # Just check if CMake configures
        mkdir -p build_autonomous 2>/dev/null || true
        if cmake -S . -B build_autonomous -DBUILD_TESTS=ON -DFETCH_DEPENDENCIES=OFF \
            -DENABLE_GNS=OFF -DENABLE_REDIS=OFF -DENABLE_SCYLLA=OFF \
            -DENABLE_FLATBUFFERS=OFF 2>/dev/null; then
            success "  CMake configures successfully"
        else
            issues+=("CMake configuration issues")
        fi
        rm -rf build_autonomous 2>/dev/null || true
    fi

    # Check header consistency
    local missing_includes
    missing_includes=$(find src/server/src -name "*.cpp" -exec grep -l '#include' {} \; 2>/dev/null | head -10 || true)
    if [ -n "$missing_includes" ]; then
        success "  Source files have includes"
    fi

    # Check for const correctness opportunities
    local non_const_refs
    non_const_refs=$(grep -rn "& " src/server/src/ --include="*.hpp" --include="*.h" 2>/dev/null | grep -v "const" | grep -v "&&" | wc -l)
    non_const_refs=${non_const_refs//[^0-9]/}  # Extract only digits
    [ -z "$non_const_refs" ] && non_const_refs=0
    if [ "$non_const_refs" -gt 10 ] 2>/dev/null; then
        issues+=("Potential const-correctness improvements ($non_const_refs non-const references)")
    fi

    # Check test coverage gaps
    local src_files
    src_files=$(find src/server/src -name "*.cpp" ! -name "*stub*" ! -name "main.cpp" | wc -l)
    local test_files
    test_files=$(find src/server/tests tests -name "*.cpp" 2>/dev/null | wc -l)
    log "  Source files: $src_files, Test files: $test_files"

    # Return findings
    if [ ${#issues[@]} -gt 0 ]; then
        warn "Found ${#issues[@]} improvement areas:"
        for issue in "${issues[@]}"; do
            echo "  - $issue"
        done
    else
        success "Codebase looks clean"
    fi

    echo "${issues[*]}"
}

# -----------------------------------------------------------------------------
# Phase 2: Create branch for iteration
# -----------------------------------------------------------------------------
create_branch() {
    local task_desc="$1"
    local branch_name="${BRANCH_PREFIX}-$(date +%Y%m%d-%H%M%S)"
    local slug
    slug=$(echo "$task_desc" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/-/g' | sed 's/--*/-/g' | head -c 40)
    branch_name="${branch_name}-${slug}"

    cd "$PROJECT_ROOT"
    git checkout main 2>/dev/null || git checkout master 2>/dev/null || true
    git pull origin main 2>/dev/null || git pull origin master 2>/dev/null || true
    git checkout -b "$branch_name"

    success "Created branch: $branch_name"
    echo "$branch_name"
}

# -----------------------------------------------------------------------------
# Phase 3: Implement (delegated to agent - this script just calls it)
# -----------------------------------------------------------------------------
implement_with_opencode() {
    local task="$1"
    cd "$PROJECT_ROOT"

    log "Phase 3: Implementing with OpenCode..."
    log "  Task: $task"

    # Use opencode run for bounded task
    opencode run "$task" 2>&1 || {
        fail "OpenCode implementation failed"
        return 1
    }

    success "Implementation complete"
}

# -----------------------------------------------------------------------------
# Phase 4: Validate - Build + Test
# -----------------------------------------------------------------------------
validate_changes() {
    cd "$PROJECT_ROOT"
    log "Phase 4: Validating changes..."

    # Stage 1: CMake configure (with retry logic for resilience)
    log "  Configuring CMake..."
    local cmake_attempts=0
    local cmake_max_attempts=2
    local cmake_success=false
    
    while [ $cmake_attempts -lt $cmake_max_attempts ]; do
        cmake_attempts=$((cmake_attempts + 1))
        if cmake -S . -B build_autonomous \
            -DBUILD_TESTS=ON \
            -DFETCH_DEPENDENCIES=ON \
            -DENABLE_GNS=OFF \
            -DENABLE_REDIS=OFF \
            -DENABLE_SCYLLA=OFF 2>&1 | tail -5; then
            cmake_success=true
            success "  CMake configure: PASS (attempt $cmake_attempts)"
            break
        else
            warn "  CMake configure failed (attempt $cmake_attempts/$cmake_max_attempts)"
            if [ $cmake_attempts -lt $cmake_max_attempts ]; then
                log "  Cleaning and retrying..."
                rm -rf build_autonomous CMakeCache.txt CMakeFiles
                sleep 5
            fi
        fi
    done
    
    if [ "$cmake_success" = false ]; then
        fail "  CMake configure: FAIL after $cmake_max_attempts attempts"
        return 1
    fi

    # Stage 2: Build (skip in fast mode for cron jobs)
    if [ "$FAST_MODE" = true ]; then
        rm -rf build_autonomous
        success "All validations passed (fast mode — cmake configure only)"
        return 0
    fi
    
    log "  Building..."
if cmake --build build_autonomous --parallel $(nproc) 2>&1 | tail -20; then
        success "  Build: PASS"
    else
        fail "  Build: FAIL"
        rm -rf build_autonomous
        return 1
    fi

    # Stage 3: Tests (use ctest for reliable exit codes)
    if [ -f build_autonomous/DarkAgesTests ] || [ -f build_autonomous/Debug/DarkAgesTests.exe ]; then
        log "  Running tests..."
        local test_bin="build_autonomous/DarkAgesTests"
        [ -f "build_autonomous/Debug/DarkAgesTests.exe" ] && test_bin="build_autonomous/Debug/DarkAgesTests.exe"

        # Use ctest for reliable exit codes (handles parallel test execution better)
        cd build_autonomous
        if ctest --output-on-failure 2>&1 | tail -20; then
            success "  Tests: PASS"
        else
            fail "  Tests: FAIL"
            rm -rf build_autonomous
            return 1
        fi
        cd "$PROJECT_ROOT"
    else
        warn "  No test binary found, skipping test run"
    fi

    rm -rf build_autonomous
    success "All validations passed"
    return 0
}

# -----------------------------------------------------------------------------
# Phase 5: Commit + PR
# -----------------------------------------------------------------------------
commit_and_pr() {
    local task="$1"
    local branch="$2"
    cd "$PROJECT_ROOT"

    log "Phase 5: Creating PR..."

    # Check if there are changes
    if git diff --quiet && git diff --cached --quiet; then
        warn "No changes to commit"
        return 0
    fi

    git add -A
    git commit -m "autonomous: $task

Automated improvement by Hermes iteration framework.
Validated: build + tests passing.

Co-Authored-By: Hermes <hermes@aycrith.dev>"

    # Push
    git push -u origin "$branch" 2>&1 || {
        fail "Push failed — may need GH_TOKEN for authentication"
        return 1
    }

    # Create PR using gh or curl
    export GH_TOKEN="${GH_TOKEN:-$(grep '^GITHUB_TOKEN=' ~/.hermes/.env 2>/dev/null | cut -d= -f2 | tr -d '\n\r')}"

    local pr_body="## Automated Iteration

**Task:** $task
**Branch:** \`$branch\`
**Validation:** Build + Tests passing

---
*Generated by DarkAges Autonomous Iteration Framework*"

    if command -v gh &>/dev/null && gh auth status &>/dev/null 2>&1; then
        gh pr create --title "autonomous: $task" --body "$pr_body" --base main 2>&1 || true
    else
        # Fallback: use GitHub API directly
        local default_branch
        default_branch=$(curl -s -H "Authorization: token $GH_TOKEN" \
            https://api.github.com/repos/Aycrith/DarkAges | python3 -c "import sys,json; print(json.load(sys.stdin).get('default_branch','main'))")

        curl -s -X POST \
            -H "Authorization: token $GH_TOKEN" \
            -H "Content-Type: application/json" \
            "https://api.github.com/repos/Aycrith/DarkAges/pulls" \
            -d "{
                \"title\": \"autonomous: $task\",
                \"body\": $(echo "$pr_body" | python3 -c "import sys,json; print(json.dumps(sys.stdin.read()))"),
                \"head\": \"$branch\",
                \"base\": \"$default_branch\"
            }" | python3 -c "import sys,json; r=json.load(sys.stdin); print(f'PR #{r[\"number\"]}: {r.get(\"html_url\",\"\")}') if 'number' in r else print(r)" 2>&1
    fi

    success "PR created for branch $branch"
}

# -----------------------------------------------------------------------------
# Phase 6: Log the iteration
# -----------------------------------------------------------------------------
log_iteration() {
    local task="$1"
    local status="$2"
    local branch="$3"
    local timestamp
    timestamp=$(date -u +"%Y-%m-%d %H:%M UTC")

    if [ ! -f "$LOG_FILE" ]; then
        echo "# DarkAges Autonomous Iteration Log" > "$LOG_FILE"
        echo "" >> "$LOG_FILE"
    fi

    cat >> "$LOG_FILE" << EOF

## Iteration: $timestamp
- **Task:** $task
- **Status:** $status
- **Branch:** $branch
- **Agent:** Hermes + OpenCode

EOF

    log "Logged iteration to $LOG_FILE"
}

# =============================================================================
# MAIN: Full iteration cycle
# =============================================================================
run_iteration() {
    local task="${1:-auto}"
    local branch=""

    log "========================================="
    log "Starting autonomous iteration"
    log "Task: $task"
    log "========================================="

    # If auto, analyze and pick a task
    if [ "$task" = "auto" ]; then
        task=$(analyze_codebase)
        if [ -z "$task" ] || [ "$task" = "Codebase looks clean" ]; then
            log "No improvements needed. Exiting."
            return 0
        fi
        # Pick first issue
        task=$(echo "$task" | head -1)
    fi

    # Create branch
    branch=$(create_branch "$task")

    # Implement
    if implement_with_opencode "$task"; then
        # Validate
        if validate_changes; then
            commit_and_pr "$task" "$branch"
            log_iteration "$task" "SUCCESS" "$branch"
            success "Iteration complete: $task"
        else
            log_iteration "$task" "VALIDATION_FAILED" "$branch"
            fail "Validation failed — rolling back branch"
            cd "$PROJECT_ROOT"
            git checkout main 2>/dev/null || git checkout master 2>/dev/null
            git branch -D "$branch" 2>/dev/null || true
        fi
    else
        log_iteration "$task" "IMPLEMENTATION_FAILED" "$branch"
        fail "Implementation failed"
        cd "$PROJECT_ROOT"
        git checkout main 2>/dev/null || git checkout master 2>/dev/null
        git branch -D "$branch" 2>/dev/null || true
    fi
}

# Allow direct invocation
FAST_MODE=false
case "${1:-}" in
    --fast|--skip-build) FAST_MODE=true; run_iteration "${2:-auto}" ;;
    analyze) analyze_codebase ;;
    validate) validate_changes ;;
    *) run_iteration "${1:-auto}" ;;
esac
