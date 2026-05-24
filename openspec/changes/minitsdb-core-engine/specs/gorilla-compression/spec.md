## ADDED Requirements

### Requirement: System SHALL compress timestamps using delta-of-delta encoding
The compressor SHALL encode consecutive timestamps by computing delta-of-delta values and using variable-length bit encoding per the Gorilla paper. Stable-interval data points (where delta-delta == 0) SHALL use 1 bit.

#### Scenario: Encode stable 1-second interval timestamps
- **WHEN** encoding timestamps at exact 1-second intervals (t=1000, 2000, 3000, ...)
- **THEN** each subsequent timestamp SHALL be encoded in 1 bit

#### Scenario: Encode timestamps with jitter
- **WHEN** encoding timestamps with small variations from the expected interval
- **THEN** the compressor SHALL correctly encode the delta-delta value using the appropriate number of bits (7, 9, or 32 bits depending on magnitude)

### Requirement: System SHALL compress double-precision values using XOR encoding
The compressor SHALL encode consecutive float64 values by XORing with the previous value. Identical values SHALL use 1 bit.

#### Scenario: Encode identical consecutive values
- **WHEN** encoding identical values (value=523.7, 523.7, 523.7)
- **THEN** each subsequent value SHALL be encoded in 1 bit

#### Scenario: Encode slightly changing values
- **WHEN** encoding values that differ only in lower bits
- **THEN** the compressor SHALL encode only the differing bits

### Requirement: System SHALL support compression of all four data types
The BlockCompressor SHALL handle analog (double), digital (int64), string, and accumulator (int64) types with appropriate encoding.

#### Scenario: Compress digital values that rarely change
- **WHEN** compressing digital values that remain at 0 then flip to 1
- **THEN** the compressor SHALL achieve compression ratio > 10:1 using run-length or XOR encoding

#### Scenario: Decompress yields identical data
- **WHEN** decompressing a CompressedBlock
- **THEN** all original DataPoint values SHALL be recovered with zero loss
