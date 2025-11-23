#!/bin/bash

# Config-based experiment runner for CS744 Project
# Reads all parameters from experiment.conf

set -e

# ======================== LOAD CONFIGURATION ========================

# Default config file
CONFIG_FILE="experiment.conf"

# Check if custom config file specified
if [ "$1" == "--config" ] && [ -n "$2" ]; then
    CONFIG_FILE="$2"
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Config file '$CONFIG_FILE' not found!"
    echo ""
    echo "Usage: $0 [--config <config_file>]"
    echo ""
    echo "Create experiment.conf with your settings, or use --config to specify a different file"
    exit 1
fi

# Load configuration
echo "Loading configuration from: $CONFIG_FILE"
source "$CONFIG_FILE"

# ======================== SETUP ========================

# Results
RESULTS_DIR="experiment_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
EXPERIMENT_DIR="${RESULTS_DIR}/${TIMESTAMP}"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

echo_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
echo_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
echo_error() { echo -e "${RED}[ERROR]${NC} $1"; }
echo_title() { echo -e "${BLUE}[====]${NC} $1"; }

# ======================== FUNCTIONS ========================

pin_postgres() {
    echo_info "Pinning PostgreSQL to cores ${POSTGRES_CORES}..."
    
    # Find all postgres processes
    POSTGRES_PIDS=$(pgrep -f "postgres" || true)
    
    
    # Pin each process
    for PID in $POSTGRES_PIDS; do
        taskset -cp $POSTGRES_CORES $PID > /dev/null 2>&1 || true
    done
    
    # Verify pinning on main process
    MAIN_PID=$(pgrep -f "postgres.*kv_db" | head -1 || echo "")
    if [ ! -z "$MAIN_PID" ]; then
        AFFINITY=$(taskset -cp $MAIN_PID 2>/dev/null | grep -oP '(?<=list: ).*' || echo "unknown")
        echo_info "PostgreSQL main process (PID $MAIN_PID) pinned to cores: $AFFINITY"
    else
        echo_warn "Could not verify PostgreSQL pinning"
    fi
}

start_server() {
    local LOG_SUFFIX=$1
    
    echo_info "Starting server on cores ${SERVER_CORES}..."
    
    # Kill existing server
    pkill -f "kv_server" 2>/dev/null || true
    sleep 2
    
    # Start server with taskset
    taskset -c $SERVER_CORES ./build/kv_server $SERVER_PORT $SERVER_THREADS $CACHE_SIZE \
        > "${EXPERIMENT_DIR}/server_${LOG_SUFFIX}.log" 2>&1 &
    SERVER_PID=$!
    
    echo_info "Server started with PID: $SERVER_PID"
    sleep 3
    
    # Verify server is running
    if ! ps -p $SERVER_PID > /dev/null; then
        echo_error "Server failed to start!"
        cat "${EXPERIMENT_DIR}/server_${LOG_SUFFIX}.log"
        exit 1
    fi
    
    # Verify CPU affinity
    AFFINITY=$(taskset -cp $SERVER_PID 2>/dev/null | grep -oP '(?<=list: ).*' || echo "unknown")
    echo_info "Server CPU affinity verified: $AFFINITY"
}

stop_server() {
    echo_info "Stopping server..."
    if [ ! -z "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    pkill -f "kv_server" 2>/dev/null || true
    sleep 2
}

prepare_database() {
    echo_info "Preparing database with ${NUM_KEYS} keys..."
    
    # Insert initial data using load generator
    taskset -c $CLIENT_CORES ./build/load_generator \
        --workload put_all \
        --threads 4 \
        --keys $NUM_KEYS \
        --duration 0 \
        --server-threads $SERVER_THREADS \
        --cache-size $CACHE_SIZE \
        --db-pool $DB_POOL_SIZE \
        > "${EXPERIMENT_DIR}/prep.log" 2>&1
    
    # Delete prep results.csv 
    rm -f results.csv
    
    echo_info "Database prepared with $NUM_KEYS keys"
}

run_experiment() {
    local WORKLOAD=$1
    local THREADS=$2
    local RUN=$3
    local TEST_DURATION=$4
    
    echo_title "Experiment: Workload=$WORKLOAD, Threads=$THREADS, Duration=${TEST_DURATION}s"
    
    # Remove old results.csv to get clean data
    rm -f results.csv
    
    # Run load generator with taskset
    taskset -c $CLIENT_CORES ./build/load_generator \
        --workload $WORKLOAD \
        --threads $THREADS \
        --duration $TEST_DURATION \
        --keys $NUM_KEYS \
        --server-threads $SERVER_THREADS \
        --cache-size $CACHE_SIZE \
        --db-pool $DB_POOL_SIZE \
        > "${EXPERIMENT_DIR}/loadgen_${WORKLOAD}_${THREADS}.log" 2>&1
    
    # Copy results to combined CSV (skip header if not first)
    if [ -f "results.csv" ]; then
        if [ ! -f "${EXPERIMENT_DIR}/results_combined.csv" ]; then
            # First file: include header
            cat results.csv > "${EXPERIMENT_DIR}/results_combined.csv"
        else
            # Subsequent files: skip header
            tail -n +2 results.csv >> "${EXPERIMENT_DIR}/results_combined.csv"
        fi
    fi
    
    echo_info "Experiment completed"
}

show_summary() {
    echo ""
    echo_title "Experiment Summary"
    echo_info "Results directory: ${EXPERIMENT_DIR}"
    echo ""
    echo "Files generated:"
    echo "  - results_combined.csv     : Performance metrics"
    echo "  - server_*.log             : Server logs"
    echo "  - loadgen_*.log            : Load generator logs"
    echo ""
    echo_info "CPU Pinning Summary:"
    echo "  PostgreSQL: cores ${POSTGRES_CORES}"
    echo "  Server:     cores ${SERVER_CORES}"
    echo "  Client:     cores ${CLIENT_CORES}"
}

# ======================== MAIN EXECUTION ========================

main() {
    echo_title "CS744 Load Testing Framework"
    echo_info "Config file: $CONFIG_FILE"
    echo ""
    echo_info "CPU Configuration:"
    echo "  PostgreSQL: cores ${POSTGRES_CORES}"
    echo "  Server:     cores ${SERVER_CORES} (${SERVER_THREADS} threads)"
    echo "  Client:     cores ${CLIENT_CORES}"
    echo ""
    echo_info "Server Configuration:"
    echo "  Port:           ${SERVER_PORT}"
    echo "  Worker Threads: ${SERVER_THREADS}"
    echo "  Cache Size:     ${CACHE_SIZE}"
    echo "  DB Pool Size:   ${DB_POOL_SIZE}"
    echo ""
    echo_info "Experiment Configuration:"
    echo "  Workloads:      ${WORKLOADS[@]}"
    echo "  Load Levels:    ${LOAD_LEVELS[@]}"
    echo "  Num Keys:       ${NUM_KEYS}"
    echo "  Duration:       ${DURATION}s per test"
    echo "  Cooldown:       ${COOLDOWN}s between tests"
    echo ""
    
    # Create experiment directory
    mkdir -p "${EXPERIMENT_DIR}"
    
    # Copy config file to experiment directory
    cp "$CONFIG_FILE" "${EXPERIMENT_DIR}/experiment.conf"
    
    # Save detailed configuration
    cat > "${EXPERIMENT_DIR}/config.txt" <<EOF
Experiment Configuration
========================
Timestamp: ${TIMESTAMP}
Config File: ${CONFIG_FILE}

CPU Pinning:
  PostgreSQL: ${POSTGRES_CORES}
  Server:     ${SERVER_CORES} (${SERVER_THREADS} threads)
  Client:     ${CLIENT_CORES}

Server Configuration:
  Port:           ${SERVER_PORT}
  Worker Threads: ${SERVER_THREADS}
  Cache Size:     ${CACHE_SIZE}
  DB Pool Size:   ${DB_POOL_SIZE}

Experiment Parameters:
  Workloads:      ${WORKLOADS[@]}
  Load Levels:    ${LOAD_LEVELS[@]}
  Keys:           ${NUM_KEYS}
  Duration:       ${DURATION}s per test
  Cooldown:       ${COOLDOWN}s between tests
EOF
    
    # Pin PostgreSQL
    pin_postgres
    
    # Start server
    start_server "initial"
    
    # Prepare database
    prepare_database
    
    # Delete any existing results_combined.csv to start fresh
    rm -f "${EXPERIMENT_DIR}/results_combined.csv"
    
    # Run experiments
    TOTAL_EXPERIMENTS=$((${#WORKLOADS[@]} * ${#LOAD_LEVELS[@]}))
    CURRENT=0
    
    for WORKLOAD in "${WORKLOADS[@]}"; do
        echo_title "Testing Workload: $WORKLOAD"
        
        for THREADS in "${LOAD_LEVELS[@]}"; do
            CURRENT=$((CURRENT + 1))
            echo_info "Progress: $CURRENT / $TOTAL_EXPERIMENTS"
            
            run_experiment $WORKLOAD $THREADS 1 $DURATION
            
            # Cooldown
            if [ $CURRENT -lt $TOTAL_EXPERIMENTS ]; then
                echo_info "Cooling down for ${COOLDOWN}s..."
                sleep $COOLDOWN
            fi
        done
    done
    
    # Cleanup
    stop_server
    
    # Show summary
    show_summary
}

# Trap for cleanup
trap stop_server EXIT

# Run main
main