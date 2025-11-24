import pandas as pd
import matplotlib.pyplot as plt

# Load your CSV file (replace with your actual filename)
df = pd.read_csv("resultss.csv")

# Plot 1: Threads vs Throughput
plt.figure(figsize=(8, 5))
plt.plot(df["threads"], df["throughput_ops_sec"], marker='o')
plt.title("Threads vs Throughput")
plt.xlabel("Threads")
plt.ylabel("Throughput (ops/sec)")
plt.grid(True)
plt.tight_layout()
plt.show()

# Plot 2: Threads vs Average Latency
plt.figure(figsize=(8, 5))
plt.plot(df["threads"], df["avg_latency_ms"], marker='o', color='orange')
plt.title("Threads vs Average Latency")
plt.xlabel("Threads")
plt.ylabel("Average Latency (ms)")
plt.grid(True)
plt.tight_layout()
plt.show()
