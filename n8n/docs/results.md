# Pilot results

These values were supplied from the user's completed batch CSVs for executions 32 and 33. They were not newly simulated during repository preparation. Values match between those two executions at the recorded CSV precision.

| Distance (m) | IP throughput (Mbps) | Application goodput (Mbps) | Mean delay (ms) | Jitter (ms) | Loss fraction | TX packets | RX packets |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 50 | 20.3998368 | 19.99984 | 3.198898745 | 0.536402386 | 0 | 17857 | 17857 |
| 100 | 20.3998368 | 19.99984 | 3.199162116 | 0.603563918 | 0 | 17857 | 17857 |
| 150 | 20.3998368 | 19.99984 | 4.238605124 | 0.754345683 | 0 | 17857 | 17857 |

All runs used 10 dBm UE power, 20 Mbps offered load, seed 42 and RNG run 1. See the baseline JSON for other settings.

An earlier individual 150 m run had different results (mean delay 5.394786434 ms and six unreceived packets). The cause remains unresolved. That run is not combined with this pilot batch. Agreement of two batches is a limited repeatability observation, not final model validation or proof of physical accuracy.
