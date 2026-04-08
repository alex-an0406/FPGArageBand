import struct

with open("output.raw", "rb") as f:
    data = f.read()

samples = []
for i in range(0, len(data) - 3, 4):  # 4 bytes per 32-bit sample
    val = struct.unpack('<i', data[i:i+4])[0]  # little-endian signed 32-bit
    samples.append(val)

with open("audio_data.h", "w") as f:
    f.write("// Sampled at 8KHz, mono, 32b int per sample\n")
    f.write(f"#define AUDIO_NUM_SAMPLES {len(samples)}\n\n")
    f.write("const int audio_samples[] = {\n")
    for i in range(0, len(samples), 8):
        chunk = samples[i:i+8]
        line = ", ".join(f"0x{v & 0xFFFFFFFF:08X}" for v in chunk)
        f.write(f"    {line},\n")
    f.write("};\n")

print(f"Done! {len(samples)} samples written to audio_data.h")
