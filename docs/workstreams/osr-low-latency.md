# OSR latency and reconstruction experiments

Base: fork main, 0.2.45. Planning only; final comparisons depend on acceptance/integration of the separate reset correction.

Establish CPU/GPU and capture-to-submit baselines. Investigate pacing only where measurements identify avoidable waits; investigate reconstruction and motion contracts independently. Validate disocclusions, both eyes and tail latency.

Keep VRS and Streamline warning handling separate. Preserve negative and inconclusive results.
