#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace kh2coop {

// Appends primitive values to a byte vector in little-endian wire order.
class ByteWriter {
public:
    void writeU8(std::uint8_t v) { buf_.push_back(v); }

    void writeU16(std::uint16_t v) { writeLittleEndian(v); }

    void writeU32(std::uint32_t v) { writeLittleEndian(v); }

    void writeU64(std::uint64_t v) { writeLittleEndian(v); }

    void writeI32(std::int32_t v) { writeU32(bitwiseCast<std::uint32_t>(v)); }

    void writeF32(float v) {
        writeU32(bitwiseCast<std::uint32_t>(v));
    }

    void writeBool(bool v) { writeU8(v ? 1 : 0); }

    void writeString(const std::string& s) {
        if (s.size() > std::numeric_limits<std::uint16_t>::max()) {
            throw std::runtime_error("ByteWriter: string exceeds 65535-byte limit");
        }
        auto len = static_cast<std::uint16_t>(s.size());
        writeU16(len);
        if (!s.empty()) {
            buf_.insert(buf_.end(), s.begin(), s.end());
        }
    }

    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return buf_; }

    [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(buf_); }

    [[nodiscard]] std::size_t size() const { return buf_.size(); }

private:
    template <typename To, typename From>
    static To bitwiseCast(const From& value) {
        static_assert(sizeof(To) == sizeof(From));
        To out;
        std::memcpy(&out, &value, sizeof(To));
        return out;
    }

    template <typename UInt>
    void writeLittleEndian(UInt value) {
        for (std::size_t i = 0; i < sizeof(UInt); ++i) {
            buf_.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
        }
    }

    std::vector<std::uint8_t> buf_;
};

// Reads primitive values from a contiguous byte range.
// Throws std::runtime_error on underflow.
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size), pos_(0) {}

    explicit ByteReader(const std::vector<std::uint8_t>& v)
        : data_(v.data()), size_(v.size()), pos_(0) {}

    std::uint8_t readU8() {
        check(sizeof(std::uint8_t));
        return data_[pos_++];
    }

    std::uint16_t readU16() { return readLittleEndian<std::uint16_t>(); }

    std::uint32_t readU32() { return readLittleEndian<std::uint32_t>(); }

    std::uint64_t readU64() { return readLittleEndian<std::uint64_t>(); }

    std::int32_t readI32() {
        return bitwiseCast<std::int32_t>(readU32());
    }

    float readF32() {
        return bitwiseCast<float>(readU32());
    }

    bool readBool() { return readU8() != 0; }

    std::string readString() {
        auto len = readU16();
        check(len);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return s;
    }

    [[nodiscard]] bool atEnd() const { return pos_ >= size_; }

    [[nodiscard]] std::size_t remaining() const { return pos_ < size_ ? size_ - pos_ : 0; }

private:
    template <typename To, typename From>
    static To bitwiseCast(const From& value) {
        static_assert(sizeof(To) == sizeof(From));
        To out;
        std::memcpy(&out, &value, sizeof(To));
        return out;
    }

    template <typename T>
    T readLittleEndian() {
        check(sizeof(T));
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<T>(data_[pos_ + i]) << (i * 8U);
        }
        pos_ += sizeof(T);
        return value;
    }

    void check(std::size_t n) const {
        if (pos_ + n > size_) {
            throw std::runtime_error("ByteReader: read past end of buffer");
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_;
};

} // namespace kh2coop
