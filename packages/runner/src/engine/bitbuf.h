// Copyright 2026 davenonymous
//
// The bit buffers of user messages, in the engine's wire format: bits
// least significant first, the encodings of tier1/bitbuf.cpp of the TF2
// SDK (coords, normals, angles, strings). A writer has a fixed capacity and
// marks itself overflowed instead of growing, as bf_write does; a reader
// marks itself overflowed when it reads past the end and then returns
// zeros, as bf_read does.
#ifndef SMRUNNER_ENGINE_BITBUF_H
#define SMRUNNER_ENGINE_BITBUF_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace smrunner {

class BitWriter {
 public:
	explicit BitWriter(size_t capacityBytes) : data_(capacityBytes, 0) {}

	void Reset();
	bool IsOverflowed() const { return overflowed_; }
	size_t BitsWritten() const { return bit_; }
	size_t BytesWritten() const { return (bit_ + 7) >> 3; }
	const uint8_t* Data() const { return data_.data(); }

	void WriteOneBit(int value);
	void WriteUBitLong(uint32_t value, int bits);
	void WriteSBitLong(int32_t value, int bits);
	void WriteChar(int value) { WriteSBitLong(value, 8); }
	void WriteByte(int value) { WriteUBitLong(static_cast<uint32_t>(value), 8); }
	void WriteShort(int value) { WriteSBitLong(value, 16); }
	void WriteWord(int value) { WriteUBitLong(static_cast<uint32_t>(value), 16); }
	void WriteLong(int32_t value) { WriteSBitLong(value, 32); }
	void WriteFloat(float value);
	void WriteString(const char* text);
	void WriteBitAngle(float angle, int bits);
	void WriteBitCoord(float value);
	void WriteBitVec3Coord(const float vector[3]);
	void WriteBitNormal(float value);
	void WriteBitVec3Normal(const float vector[3]);
	void WriteBitAngles(const float angles[3]) { WriteBitVec3Coord(angles); }

 private:
	std::vector<uint8_t> data_;
	size_t bit_ = 0;
	bool overflowed_ = false;
};

class BitReader {
 public:
	BitReader() = default;
	BitReader(const uint8_t* data, size_t bytes) { StartReading(data, bytes); }

	void StartReading(const uint8_t* data, size_t bytes);
	bool IsOverflowed() const { return overflowed_; }
	size_t BitsLeft() const { return bit_ < bits_ ? bits_ - bit_ : 0; }

	int ReadOneBit();
	uint32_t ReadUBitLong(int bits);
	int32_t ReadSBitLong(int bits);
	int ReadChar() { return ReadSBitLong(8); }
	int ReadByte() { return static_cast<int>(ReadUBitLong(8)); }
	int ReadShort() { return ReadSBitLong(16); }
	int ReadWord() { return static_cast<int>(ReadUBitLong(16)); }
	int32_t ReadLong() { return ReadSBitLong(32); }
	float ReadFloat();
	// bf_read::ReadString: stops at NUL (or a line break when line is set),
	// keeps what fits; false when it did not fit or the buffer ran out.
	bool ReadString(char* out, size_t maxlength, bool line, size_t* length);
	// Every byte up to the NUL, for the runner's own decoding.
	std::string ReadString();
	float ReadBitAngle(int bits);
	float ReadBitCoord();
	void ReadBitVec3Coord(float out[3]);
	float ReadBitNormal();
	void ReadBitVec3Normal(float out[3]);
	void ReadBitAngles(float out[3]) { ReadBitVec3Coord(out); }

 private:
	const uint8_t* data_ = nullptr;
	size_t bits_ = 0;
	size_t bit_ = 0;
	bool overflowed_ = false;
};

}  // namespace smrunner

#endif
