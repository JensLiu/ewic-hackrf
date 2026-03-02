// sine_table.c
#include "sine_table.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint8_t sine_table[SINE_TABLE_SIZE * 2];  // IQ samples

void init_sine_table(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        // Calculate phase angle for this sample
        float angle = 2.0f * M_PI * i / SINE_TABLE_SIZE;
        
        // Generate I/Q samples with proper amplitude scaling
        // Scale to -SINE_AMPLITUDE to +SINE_AMPLITUDE range
        int8_t i_sample = (int8_t)(SINE_AMPLITUDE * sinf(angle));
        int8_t q_sample = (int8_t)(SINE_AMPLITUDE * cosf(angle));
        
        // Convert to unsigned 8-bit by adding 128 (to center around 128)
        sine_table[i * 2] = (uint8_t)(i_sample + 128);      // I sample
        sine_table[i * 2 + 1] = (uint8_t)(q_sample + 128);  // Q sample
    }
}

void fill_sine_buffer(uint8_t* buffer, uint32_t len, uint32_t* _unused_phase, uint32_t SINE_FREQ, uint32_t SAMPLE_RATE) {
	const uint32_t BLOCK_SAMPLES    = 8192;              // samples per half-cycle
	const uint32_t CYCLE_SAMPLES    = BLOCK_SAMPLES * 2; // full sine+zero cycle
	const uint32_t BYTES_PER_SAMPLE = 2;                 // I + Q
	const uint8_t  MID_SCALE        = 0;               // "zero" for unsigned I/Q128

	// phase increment to step through your sine table at the right rate:
	const uint32_t phase_increment = (SINE_FREQ * SINE_TABLE_SIZE) / SAMPLE_RATE;

	// static so they survive across back-to-back fill_sine_buffer() calls:
	static uint32_t super_phase = 0;  // 0…CYCLE_SAMPLES−1
	static uint32_t table_phase = 0;  // 0…SINE_TABLE_SIZE−1

	uint32_t total_samples = len / BYTES_PER_SAMPLE;

	for (uint32_t s = 0; s < total_samples; s++) {
		uint32_t i = s * BYTES_PER_SAMPLE;

		if (super_phase < BLOCK_SAMPLES) {
			// --- sine part ---
			uint32_t idx = table_phase;
			buffer[i    ] = sine_table[2*idx];     // I
			buffer[i + 1] = sine_table[2*idx + 1]; // Q

			// advance through sine table
			table_phase = (table_phase + phase_increment) % SINE_TABLE_SIZE;
		} else {
			// --- zero part ---
			buffer[i    ] = MID_SCALE;
			buffer[i + 1] = MID_SCALE;
		}

		// advance through the 8192+8192 super-cycle
		super_phase++;
		if (super_phase >= CYCLE_SAMPLES) {
			super_phase = 0;
		}
	}
}