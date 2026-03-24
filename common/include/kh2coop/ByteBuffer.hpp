#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace kh2coop {

// Appends primitive values to a byte vector in little-endian wire order.
class ByteWriter {
public:
    void writeU8(std::uint8_t v) { buf_.push_back(v); }

    void writeU16(std::uint16_t v) { append(&v, sizeof(v)); }

    void writeU32(std::uint32_t v) { append(&v, sizeof(v)); }

    void writeU64(std::uint64_t v) { append(&v, sizeof(v)); }

    void writeI32(std::int32_t v) { append(&v, sizeof(v)); }

    void writeF32(float v) { append(&v, sizeof(v)); }

    void writeBool(bool v) { writeU8(v ? 1 : 0); }

    void writeString(const std::string& s) {
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
    void append(const void* src, std::size_t n) {
        const auto* p = static_cast<const std::uint8_t*>(src);
        buf_.insert(buf_.end(), p, p + n);
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

    std::uint8_t readU8() { return read<std::uint8_t>(); }

    std::uint16_t readU16() { return read<std::uint16_t>(); }

    std::uint32_t readU32() { return read<std::uint32_t>(); }

    std::uint64_t readU64() { return read<std::uint64_t>(); }

    std::int32_t readI32() { return read<std::int32_t>(); }

    float readF32() { return read<float>(); }

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
    template <typename T>
    T read() {
        check(sizeof(T));
        T v;
        std::memcpy(&v, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
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
