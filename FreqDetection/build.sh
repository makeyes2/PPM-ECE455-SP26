#!/bin/bash
set -e  # Stop on any error

echo "=== Synthesizing with Yosys ==="
yosys -p "synth_ice40 -top FrequencyCounter -json FrequencyCounter.json" FrequencyCounter.sv

echo "=== Place and Route with nextpnr ==="
nextpnr-ice40 --up5k --package sg48 --json FrequencyCounter.json --pcf FrequencyCounter.pcf --asc FrequencyCounter.asc

echo "=== Packing bitstream ==="
icepack FrequencyCounter.asc FrequencyCounter.bin

echo "=== Programming the FPGA ==="
iceprog FrequencyCounter.bin
    
echo "=== Done! ==="