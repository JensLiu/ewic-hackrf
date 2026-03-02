#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate HackRF IQ file with OOK bit patterns for custom_transceiver_receive."
    )
    parser.add_argument("--out", default="tools/custom_tx_bits.iq", help="Output IQ file path")
    parser.add_argument("--sample-rate", type=int, default=10_000_000, help="Sample rate (Hz)")
    parser.add_argument("--sine-freq", type=int, default=200_000, help="Sine frequency for bit=1 (Hz)")
    parser.add_argument("--center-freq", type=int, default=915_000_000, help="Center frequency (Hz)")
    parser.add_argument("--amplitude", type=int, default=80, help="IQ amplitude (0-127)")
    parser.add_argument(
        "--bit-samples",
        type=int,
        default=5_000_000,
        help="Samples per bit (match BIT_SAMPLES)",
    )
    parser.add_argument("--seconds", type=int, default=10, help="Total duration (seconds)")
    parser.add_argument(
        "--pattern",
        default="10101010 11001100 11110000 00001111",
        help="Bit pattern, spaces ignored (e.g., '1010 1100')",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not (0 <= args.amplitude <= 127):
        raise SystemExit("Amplitude must be in [0, 127]")

    pattern_bits = args.pattern.replace(" ", "")
    if not pattern_bits or any(ch not in "01" for ch in pattern_bits):
        raise SystemExit("Pattern must contain only 0/1 (spaces allowed)")

    pattern = [int(ch) for ch in pattern_bits]
    total_samples = args.sample_rate * args.seconds
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    phase_inc = 2.0 * math.pi * args.sine_freq / args.sample_rate

    with out_path.open("wb") as f:
        phase = 0.0
        sample_in_bit = 0
        bit_idx = 0

        for _ in range(total_samples):
            bit = pattern[bit_idx]
            if bit:
                i_val = int(args.amplitude * math.sin(phase))
                q_val = int(args.amplitude * math.cos(phase))
                phase += phase_inc
                if phase >= 2.0 * math.pi:
                    phase -= 2.0 * math.pi
            else:
                i_val = 0
                q_val = 0

            # Signed int8 samples, written as bytes
            f.write(bytes((i_val & 0xFF, q_val & 0xFF)))

            sample_in_bit += 1
            if sample_in_bit >= args.bit_samples:
                sample_in_bit = 0
                bit_idx = (bit_idx + 1) % len(pattern)

    print(f"Wrote {out_path} ({out_path.stat().st_size} bytes)")
    print("hackrf_transfer params:")
    print(
        f"  -f {args.center_freq} -s {args.sample_rate} -x 10 -t {out_path}"
    )


if __name__ == "__main__":
    main()
