#!/usr/bin/env python3
"""
Analysis script for CS744 KV Server Load Testing Results
Generates plots and identifies performance bottlenecks
"""

import pandas as pd
import matplotlib.pyplot as plt
import sys
import os
from pathlib import Path

def load_data(results_dir):
    """Load results from CSV file"""
    csv_path = Path(results_dir) / "results_combined.csv"
    
    if not csv_path.exists():
        print(f"Error: {csv_path} not found!")
        sys.exit(1)
    
    df = pd.read_csv(csv_path)
    
    # Remove any duplicate header rows
    df = df[df['timestamp'] != 'timestamp']
    
    # Convert to numeric
    numeric_cols = ['timestamp', 'threads', 'num_keys', 'duration', 'requests', 
                   'get_requests', 'throughput', 'avg_latency_ms', 'p99_latency_ms', 
                   'hit_rate', 'server_threads', 'cache_capacity', 'db_pool_size']
    
    for col in numeric_cols:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors='coerce')
    
    # Drop rows with NaN values
    df = df.dropna()
    
    return df

def plot_throughput(df, output_dir):
    """Plot throughput vs load for different workloads"""
    plt.figure(figsize=(12, 6))
    
    workloads = df['workload'].unique()
    
    for workload in workloads:
        data = df[df['workload'] == workload].sort_values('threads')
        plt.plot(data['threads'], data['throughput'], 
                marker='o', linewidth=2, markersize=8, label=workload)
    
    plt.xlabel('Number of Client Threads', fontsize=12)
    plt.ylabel('Throughput (ops/sec)', fontsize=12)
    plt.title('Throughput vs Load Level', fontsize=14, fontweight='bold')
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = Path(output_dir) / "throughput_vs_load.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {output_path}")
    plt.close()

def plot_latency(df, output_dir):
    """Plot average latency vs load for different workloads"""
    plt.figure(figsize=(12, 6))
    
    workloads = df['workload'].unique()
    
    for workload in workloads:
        data = df[df['workload'] == workload].sort_values('threads')
        plt.plot(data['threads'], data['avg_latency_ms'], 
                marker='s', linewidth=2, markersize=8, label=workload)
    
    plt.xlabel('Number of Client Threads', fontsize=12)
    plt.ylabel('Average Latency (ms)', fontsize=12)
    plt.title('Average Latency vs Load Level', fontsize=14, fontweight='bold')
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = Path(output_dir) / "latency_vs_load.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {output_path}")
    plt.close()

def plot_p99_latency(df, output_dir):
    """Plot P99 latency vs load for different workloads"""
    plt.figure(figsize=(12, 6))
    
    workloads = df['workload'].unique()
    
    for workload in workloads:
        data = df[df['workload'] == workload].sort_values('threads')
        plt.plot(data['threads'], data['p99_latency_ms'], 
                marker='^', linewidth=2, markersize=8, label=workload)
    
    plt.xlabel('Number of Client Threads', fontsize=12)
    plt.ylabel('P99 Latency (ms)', fontsize=12)
    plt.title('P99 Latency vs Load Level', fontsize=14, fontweight='bold')
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = Path(output_dir) / "p99_latency_vs_load.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {output_path}")
    plt.close()

def plot_cache_hit_rate(df, output_dir):
    """Plot cache hit rate for GET workloads"""
    get_data = df[df['get_requests'] > 0].copy()
    
    if get_data.empty:
        print("⚠ No GET workloads found, skipping cache hit rate plot")
        return
    
    plt.figure(figsize=(12, 6))
    
    workloads = get_data['workload'].unique()
    
    for workload in workloads:
        data = get_data[get_data['workload'] == workload].sort_values('threads')
        plt.plot(data['threads'], data['hit_rate'], 
                marker='D', linewidth=2, markersize=8, label=workload)
    
    plt.xlabel('Number of Client Threads', fontsize=12)
    plt.ylabel('Cache Hit Rate (%)', fontsize=12)
    plt.title('Cache Hit Rate vs Load Level', fontsize=14, fontweight='bold')
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.ylim(0, 105)
    plt.tight_layout()
    
    output_path = Path(output_dir) / "cache_hit_rate.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {output_path}")
    plt.close()

def generate_summary(df, output_dir):
    """Generate summary statistics"""
    summary_path = Path(output_dir) / "summary.txt"
    
    with open(summary_path, 'w') as f:
        f.write("="*70 + "\n")
        f.write("PERFORMANCE TEST SUMMARY\n")
        f.write("="*70 + "\n\n")
        
        for workload in df['workload'].unique():
            data = df[df['workload'] == workload].sort_values('threads')
            
            f.write(f"\n{workload.upper()} Workload:\n")
            f.write("-" * 70 + "\n")
            
            # Peak throughput
            max_throughput = data['throughput'].max()
            max_threads = data.loc[data['throughput'].idxmax(), 'threads']
            f.write(f"  Peak Throughput:     {max_throughput:,.0f} ops/sec @ {max_threads:.0f} threads\n")
            
            # Saturation point (where throughput plateaus)
            if len(data) > 3:
                # Find where throughput increase is < 10%
                for i in range(1, len(data)):
                    prev_tput = data.iloc[i-1]['throughput']
                    curr_tput = data.iloc[i]['throughput']
                    increase = ((curr_tput - prev_tput) / prev_tput) * 100
                    if increase < 10:
                        sat_threads = data.iloc[i]['threads']
                        f.write(f"  Saturation Point:    ~{sat_threads:.0f} threads (throughput increase < 10%)\n")
                        break
            
            # Latency at peak throughput
            peak_latency = data.loc[data['throughput'].idxmax(), 'avg_latency_ms']
            peak_p99 = data.loc[data['throughput'].idxmax(), 'p99_latency_ms']
            f.write(f"  Latency @ Peak:      Avg={peak_latency:.2f}ms, P99={peak_p99:.0f}ms\n")
            
            # Cache hit rate (if applicable)
            if data['get_requests'].sum() > 0:
                avg_hit_rate = data['hit_rate'].mean()
                f.write(f"  Avg Cache Hit Rate:  {avg_hit_rate:.1f}%\n")
            
            f.write("\n")
        
        f.write("\n" + "="*70 + "\n")
        f.write("BOTTLENECK ANALYSIS\n")
        f.write("="*70 + "\n\n")
        
        for workload in df['workload'].unique():
            data = df[df['workload'] == workload].sort_values('threads')
            max_throughput = data['throughput'].max()
            
            f.write(f"{workload.upper()}:\n")
            
            if 'get_popular' in workload:
                f.write("  Expected Bottleneck: CPU/Memory (cache hits)\n")
                f.write("  Characteristics:\n")
                f.write("    - High cache hit rate (>95%)\n")
                f.write("    - Low latency (<1ms typically)\n")
                f.write("    - Throughput saturates when server CPU is maxed\n")
            elif 'put_all' in workload or 'get_all' in workload:
                f.write("  Expected Bottleneck: Disk I/O (database access)\n")
                f.write("  Characteristics:\n")
                f.write("    - Every request hits database\n")
                f.write("    - Higher latency (depends on disk speed)\n")
                f.write("    - Throughput limited by PostgreSQL disk I/O\n")
            elif 'mixed' in workload:
                f.write("  Expected Bottleneck: Mixed (CPU + Disk)\n")
                f.write("  Characteristics:\n")
                f.write("    - Combination of cache hits and DB access\n")
                f.write("    - Performance depends on read/write ratio\n")
            
            f.write(f"  Measured Peak Throughput: {max_throughput:,.0f} ops/sec\n\n")
    
    print(f"✓ Saved: {summary_path}")
    
    # Also print to console
    with open(summary_path, 'r') as f:
        print("\n" + f.read())

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_results.py <experiment_directory>")
        print("Example: python3 analyze_results.py experiment_results/20251123_143022")
        sys.exit(1)
    
    results_dir = sys.argv[1]
    
    if not Path(results_dir).exists():
        print(f"Error: Directory {results_dir} does not exist!")
        sys.exit(1)
    
    print(f"\n{'='*70}")
    print("Loading and analyzing results...")
    print(f"{'='*70}\n")
    
    # Load data
    df = load_data(results_dir)
    print(f"✓ Loaded {len(df)} data points from {len(df['workload'].unique())} workload(s)")
    
    # Create plots directory
    plots_dir = Path(results_dir) / "plots"
    plots_dir.mkdir(exist_ok=True)
    
    print(f"\nGenerating plots in {plots_dir}/...\n")
    
    # Generate plots
    plot_throughput(df, plots_dir)
    plot_latency(df, plots_dir)
    plot_p99_latency(df, plots_dir)
    plot_cache_hit_rate(df, plots_dir)
    
    # Generate summary
    print("\nGenerating summary report...\n")
    generate_summary(df, results_dir)
    
    print(f"\n{'='*70}")
    print("Analysis complete!")
    print(f"{'='*70}\n")
    print(f"Results directory: {results_dir}")
    print(f"Plots directory:   {plots_dir}")
    print(f"\nNext steps:")
    print(f"  1. Review plots in {plots_dir}/")
    print(f"  2. Read summary.txt for bottleneck analysis")
    print(f"  3. Check monitor_*.csv files for resource utilization")

if __name__ == "__main__":
    main()