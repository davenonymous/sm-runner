// Copyright 2026 davenonymous
//
// Ported from tier1/bitbuf.cpp and public/coordsize.h of the TF2 SDK
// (Valve's SDK license): the same encodings, written bit by bit.
#include "engine/bitbuf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace smrunner {

static const int kCoordIntegerBits = 14;
static const int kCoordFractionalBits = 5;
static const int kCoordDenominator = 1 << kCoordFractionalBits;
static const double kCoordResolution = 1.0 / kCoordDenominator;
static const int kNormalFractionalBits = 11;
static const int kNormalDenominator = (1 << kNormalFractionalBits) - 1;
static const double kNormalResolution = 1.0 / kNormalDenominator;

// ------------------------------------------------------------------- writer

void BitWriter::Reset() {
	bit_ = 0;
	overflowed_ = false;
	memset(data_.data(), 0, data_.size());
}

void BitWriter::WriteOneBit(int value) {
	if (bit_ >= data_.size() * 8) {
		overflowed_ = true;
		return;
	}
	uint8_t mask = static_cast<uint8_t>(1u << (bit_ & 7));
	if (value) {
		data_[bit_ >> 3] |= mask;
	} else {
		data_[bit_ >> 3] &= static_cast<uint8_t>(~mask);
	}
	bit_++;
}

void BitWriter::WriteUBitLong(uint32_t value, int bits) {
	if (data_.size() * 8 - bit_ < static_cast<size_t>(bits)) {
		bit_ = data_.size() * 8;
		overflowed_ = true;
		return;
	}
	for (int i = 0; i < bits; i++) {
		WriteOneBit((value >> i) & 1);
	}
}

// The sign extension is forced to be right even when the value overflows.
void BitWriter::WriteSBitLong(int32_t value, int bits) {
	int32_t preserve = static_cast<int32_t>(0x7FFFFFFFu >> (32 - bits));
	int32_t signExtension = (value >> 31) & ~preserve;
	WriteUBitLong(static_cast<uint32_t>((value & preserve) | signExtension), bits);
}

void BitWriter::WriteFloat(float value) {
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	WriteUBitLong(bits, 32);
}

void BitWriter::WriteString(const char* text) {
	if (!text) {
		WriteChar(0);
		return;
	}
	do {
		WriteChar(*text);
	} while (*text++ != '\0');
}

void BitWriter::WriteBitAngle(float angle, int bits) {
	uint32_t shift = 1u << bits;
	int value = static_cast<int>((angle / 360.0) * shift);
	WriteUBitLong(static_cast<uint32_t>(value) & (shift - 1), bits);
}

void BitWriter::WriteBitCoord(float value) {
	int sign = value <= -kCoordResolution;
	int integer = static_cast<int>(fabsf(value));
	int fraction = abs(static_cast<int>(value * kCoordDenominator)) & (kCoordDenominator - 1);
	WriteOneBit(integer);
	WriteOneBit(fraction);
	if (!integer && !fraction) {
		return;
	}
	WriteOneBit(sign);
	if (integer) {
		WriteUBitLong(static_cast<uint32_t>(integer - 1), kCoordIntegerBits);
	}
	if (fraction) {
		WriteUBitLong(static_cast<uint32_t>(fraction), kCoordFractionalBits);
	}
}

void BitWriter::WriteBitVec3Coord(const float vector[3]) {
	bool present[3];
	for (int i = 0; i < 3; i++) {
		present[i] = vector[i] >= kCoordResolution || vector[i] <= -kCoordResolution;
		WriteOneBit(present[i]);
	}
	for (int i = 0; i < 3; i++) {
		if (present[i]) {
			WriteBitCoord(vector[i]);
		}
	}
}

// +1 and -1 are valid normals, encoded as all ones.
void BitWriter::WriteBitNormal(float value) {
	int sign = value <= -kNormalResolution;
	uint32_t fraction = static_cast<uint32_t>(abs(static_cast<int>(value * kNormalDenominator)));
	if (fraction > static_cast<uint32_t>(kNormalDenominator)) {
		fraction = kNormalDenominator;
	}
	WriteOneBit(sign);
	WriteUBitLong(fraction, kNormalFractionalBits);
}

void BitWriter::WriteBitVec3Normal(const float vector[3]) {
	bool presentX = vector[0] >= kNormalResolution || vector[0] <= -kNormalResolution;
	bool presentY = vector[1] >= kNormalResolution || vector[1] <= -kNormalResolution;
	WriteOneBit(presentX);
	WriteOneBit(presentY);
	if (presentX) {
		WriteBitNormal(vector[0]);
	}
	if (presentY) {
		WriteBitNormal(vector[1]);
	}
	WriteOneBit(vector[2] <= -kNormalResolution);
}

// ------------------------------------------------------------------- reader

void BitReader::StartReading(const uint8_t* data, size_t bytes) {
	data_ = data;
	bits_ = bytes * 8;
	bit_ = 0;
	overflowed_ = false;
}

int BitReader::ReadOneBit() {
	if (bit_ >= bits_) {
		overflowed_ = true;
		return 0;
	}
	int value = (data_[bit_ >> 3] >> (bit_ & 7)) & 1;
	bit_++;
	return value;
}

uint32_t BitReader::ReadUBitLong(int bits) {
	if (BitsLeft() < static_cast<size_t>(bits)) {
		bit_ = bits_;
		overflowed_ = true;
		return 0;
	}
	uint32_t value = 0;
	for (int i = 0; i < bits; i++) {
		value |= static_cast<uint32_t>(ReadOneBit()) << i;
	}
	return value;
}

int32_t BitReader::ReadSBitLong(int bits) {
	uint32_t value = ReadUBitLong(bits);
	if (bits < 32 && (value & (1u << (bits - 1)))) {
		value |= ~((1u << bits) - 1);
	}
	return static_cast<int32_t>(value);
}

float BitReader::ReadFloat() {
	uint32_t bits = ReadUBitLong(32);
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

bool BitReader::ReadString(char* out, size_t maxlength, bool line, size_t* length) {
	size_t written = 0;
	bool tooSmall = false;
	for (;;) {
		char c = static_cast<char>(ReadChar());
		if (c == '\0' || (line && c == '\n')) {
			break;
		}
		if (written + 1 < maxlength) {
			out[written++] = c;
		} else {
			tooSmall = true;
		}
	}
	if (maxlength > 0) {
		out[written] = '\0';
	}
	if (length) {
		*length = written;
	}
	return !overflowed_ && !tooSmall;
}

std::string BitReader::ReadString() {
	std::string text;
	for (char c = static_cast<char>(ReadChar()); c != '\0'; c = static_cast<char>(ReadChar())) {
		text += c;
	}
	return text;
}

float BitReader::ReadBitAngle(int bits) {
	float shift = static_cast<float>(1u << bits);
	return static_cast<float>(ReadUBitLong(bits)) * (360.0f / shift);
}

float BitReader::ReadBitCoord() {
	int integer = ReadOneBit();
	int fraction = ReadOneBit();
	if (!integer && !fraction) {
		return 0.0f;
	}
	int sign = ReadOneBit();
	if (integer) {
		integer = static_cast<int>(ReadUBitLong(kCoordIntegerBits)) + 1;
	}
	if (fraction) {
		fraction = static_cast<int>(ReadUBitLong(kCoordFractionalBits));
	}
	float value = static_cast<float>(integer + fraction * kCoordResolution);
	return sign ? -value : value;
}

void BitReader::ReadBitVec3Coord(float out[3]) {
	bool present[3];
	for (int i = 0; i < 3; i++) {
		present[i] = ReadOneBit();
	}
	for (int i = 0; i < 3; i++) {
		out[i] = present[i] ? ReadBitCoord() : 0.0f;
	}
}

float BitReader::ReadBitNormal() {
	int sign = ReadOneBit();
	float value = static_cast<float>(ReadUBitLong(kNormalFractionalBits) * kNormalResolution);
	return sign ? -value : value;
}

// The first two components imply the third, but not its sign.
void BitReader::ReadBitVec3Normal(float out[3]) {
	bool presentX = ReadOneBit();
	bool presentY = ReadOneBit();
	out[0] = presentX ? ReadBitNormal() : 0.0f;
	out[1] = presentY ? ReadBitNormal() : 0.0f;
	bool negativeZ = ReadOneBit();
	float squared = out[0] * out[0] + out[1] * out[1];
	out[2] = squared < 1.0f ? sqrtf(1.0f - squared) : 0.0f;
	if (negativeZ) {
		out[2] = -out[2];
	}
}

}  // namespace smrunner
