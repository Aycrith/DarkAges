// Minimal FlatBuffers header for DarkAges MMO
// Based on FlatBuffers 23.5.26 - extracted essential definitions

#ifndef FLATBUFFERS_H_
#define FLATBUFFERS_H_

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <utility>
#include <cassert>

namespace flatbuffers {

// Basic types
typedef uint32_t uoffset_t;
typedef int32_t soffset_t;
typedef uint16_t voffset_t;

// Forward declarations
class FlatBufferBuilder;
class Verifier;
struct String;
template<typename T> struct Offset;
template<typename T> struct Vector;
template<typename T> class VectorIterator;

// Offset type for referencing objects in a buffer
template<typename T = void> struct Offset {
    uoffset_t o;
    Offset() : o(0) {}
    explicit Offset(uoffset_t _o) : o(_o) {}
    bool IsNull() const { return o == 0; }
    uoffset_t o_value() const { return o; }
};

// Endian conversion (assumes little-endian platform)
template<typename T> inline T EndianScalar(T t) {
    #if FLATBUFFERS_LITTLEENDIAN
        return t;
    #else
        // Big-endian swap
        T r;
        auto d = reinterpret_cast<char*>(&r);
        auto s = reinterpret_cast<const char*>(&t);
        for (size_t i = 0; i < sizeof(T); i++) d[i] = s[sizeof(T) - 1 - i];
        return r;
    #endif
}

// Vector iterator
template<typename T, typename IT> struct VectorIterator {
    const uoffset_t* data_;
    uoffset_t ind_;
    VectorIterator(const uoffset_t* data, uoffset_t ind) : data_(data), ind_(ind) {}
    VectorIterator& operator++() { ++ind_; return *this; }
    bool operator!=(const VectorIterator& other) const { return ind_ != other.ind_; }
    IT operator*() const;
};

// Vector of offsets
template<typename T> class Vector {
    const uoffset_t* Data() const {
        return reinterpret_cast<const uoffset_t*>(this);
    }
public:
    uoffset_t size() const { return EndianScalar(Data()[0]); }
    
    typedef VectorIterator<T, T> const_iterator;
    const_iterator begin() const { return const_iterator(Data(), 0); }
    const_iterator end() const { return const_iterator(Data(), size()); }
    
protected:
    const uoffset_t* GetValues() const { return Data() + 1; }
};

// Template specialization for primitive types
template<> inline const uint8_t* Vector<uint8_t>::GetValues() const { 
    return reinterpret_cast<const uint8_t*>(Data() + 1); 
}
template<> inline const uint32_t* Vector<uint32_t>::GetValues() const { 
    return reinterpret_cast<const uint32_t*>(Data() + 1); 
}
template<> inline const float* Vector<float>::GetValues() const { 
    return reinterpret_cast<const float*>(Data() + 1); 
}

// Template specialization for Vector<uint8_t>
template<> class Vector<uint8_t> {
    const uoffset_t* Data() const { return reinterpret_cast<const uoffset_t*>(this); }
public:
    uoffset_t size() const { return EndianScalar(Data()[0]); }
    const uint8_t* Get(uoffset_t i) const { return GetValues() + i; }
    const uint8_t* operator[](uoffset_t i) const { return GetValues() + i; }
    const uint8_t* begin() const { return GetValues(); }
    const uint8_t* end() const { return GetValues() + size(); }
    const uint8_t* data() const { return GetValues(); }
protected:
    const uint8_t* GetValues() const { return reinterpret_cast<const uint8_t*>(Data() + 1); }
};

// Template specialization for Vector<uint32_t>
template<> class Vector<uint32_t> {
    const uoffset_t* Data() const { return reinterpret_cast<const uoffset_t*>(this); }
public:
    uoffset_t size() const { return EndianScalar(Data()[0]); }
    const uint32_t* Get(uoffset_t i) const { return GetValues() + i; }
    uint32_t operator[](uoffset_t i) const { return GetValues()[i]; }
    const uint32_t* begin() const { return GetValues(); }
    const uint32_t* end() const { return GetValues() + size(); }
protected:
    const uint32_t* GetValues() const { return reinterpret_cast<const uint32_t*>(Data() + 1); }
};

// Template specialization for Vector<float>
template<> class Vector<float> {
    const uoffset_t* Data() const { return reinterpret_cast<const uoffset_t*>(this); }
public:
    uoffset_t size() const { return EndianScalar(Data()[0]); }
    const float* Get(uoffset_t i) const { return GetValues() + i; }
    float operator[](uoffset_t i) const { return GetValues()[i]; }
protected:
    const float* GetValues() const { return reinterpret_cast<const float*>(Data() + 1); }
};

// String type
struct String : public Vector<uint8_t> {
    const char* c_str() const { return reinterpret_cast<const char*>(data()); }
    size_t length() const { return size(); }
    std::string str() const { return std::string(c_str(), length()); }
};

// Basic alignment
inline size_t AlignOf(size_t byte_width) {
    return byte_width;
}

// Verifier for buffer integrity
class Verifier {
    const uint8_t* buf_;
    size_t size_;
    size_t depth_;
    size_t max_depth_;
    size_t num_tables_;
    size_t max_tables_;
    
public:
    Verifier(const uint8_t* buf, size_t size, size_t max_depth = 64, size_t max_tables = 1000000)
        : buf_(buf), size_(size), depth_(0), max_depth_(max_depth), num_tables_(0), max_tables_(max_tables) {}
    
    bool VerifyAlignment(const uint8_t* p, size_t align) const {
        return (reinterpret_cast<uintptr_t>(p) & (align - 1)) == 0;
    }
    
    bool Verify(const uint8_t* p, size_t len) const {
        return p >= buf_ && (p + len) <= (buf_ + size_);
    }
    
    bool VerifyString(const String* s) const {
        if (!s) return true;
        auto end = s->c_str() + s->length() + 1;  // +1 for null terminator
        return Verify(reinterpret_cast<const uint8_t*>(s), end - reinterpret_cast<const char*>(s));
    }
    
    bool VerifyVector(const void* vec) const {
        if (!vec) return true;
        auto v = reinterpret_cast<const Vector<uint8_t>*>(vec);
        return Verify(reinterpret_cast<const uint8_t*>(v), sizeof(uoffset_t) + v->size());
    }
    
    bool VerifyVectorOfTables(const void* vec) const {
        if (!vec) return true;
        return true;  // Simplified verification
    }
    
    bool EndTable() const { return true; }
    
    template<typename T> bool VerifyBuffer(const char* file_identifier) const {
        if (size_ < sizeof(uoffset_t)) return false;
        auto root = GetRoot<T>(buf_);
        if (!root) return false;
        return root->Verify(*this);
    }
};

// Table base class
class Table {
protected:
    soffset_t vtable_;
    
    const uint8_t* GetVTable() const {
        return reinterpret_cast<const uint8_t*>(this) - vtable_;
    }
    
    voffset_t GetOptionalFieldOffset(voffset_t field) const {
        auto vtable = GetVTable();
        auto vtsize = *reinterpret_cast<const voffset_t*>(vtable);
        vtsize = EndianScalar(vtsize);
        if (field < vtsize) {
            auto voffset = *reinterpret_cast<const voffset_t*>(vtable + field);
            return EndianScalar(voffset);
        }
        return 0;
    }
    
    template<typename T> T GetField(voffset_t field, T defaultval) const {
        auto field_offset = GetOptionalFieldOffset(field);
        return field_offset ? EndianScalar(*reinterpret_cast<const T*>(
            reinterpret_cast<const uint8_t*>(this) + field_offset)) : defaultval;
    }
    
    template<typename P> P GetPointer(voffset_t field) const {
        auto field_offset = GetOptionalFieldOffset(field);
        if (!field_offset) return nullptr;
        auto p = reinterpret_cast<const uint8_t*>(this) + field_offset;
        auto offset = *reinterpret_cast<const soffset_t*>(p);
        return reinterpret_cast<P>(p + EndianScalar(offset));
    }
    
    template<typename P> P GetStruct(voffset_t field) const {
        auto field_offset = GetOptionalFieldOffset(field);
        return field_offset ? reinterpret_cast<P>(reinterpret_cast<const uint8_t*>(this) + field_offset) : nullptr;
    }

public:
    bool VerifyTableStart(Verifier& verifier) const {
        return verifier.Verify(reinterpret_cast<const uint8_t*>(this), sizeof(soffset_t));
    }
    
    template<typename T> bool VerifyField(Verifier& verifier, voffset_t field, size_t align) const {
        if (GetOptionalFieldOffset(field)) {
            auto p = reinterpret_cast<const uint8_t*>(this) + field;
            return verifier.VerifyAlignment(p, align);
        }
        return true;
    }
    
    bool VerifyOffset(Verifier& /*verifier*/, voffset_t /*field*/) const {
        return true;  // Simplified - full implementation would check pointer validity
    }
    
    bool Verify(Verifier& verifier) const {
        return VerifyTableStart(verifier);
    }
};

// FlatBufferBuilder for creating buffers
class FlatBufferBuilder {
    std::vector<uint8_t> buf_;
    uoffset_t head_;
    std::vector<uoffset_t> offsets_;
    std::vector<uoffset_t> nested_bufs_;
    
    void Align(size_t req) {
        auto align = (buf_.size() + req - 1) & ~(req - 1);
        buf_.resize(align);
    }
    
public:
    explicit FlatBufferBuilder(size_t initial_size = 1024) : head_(initial_size) {
        buf_.resize(initial_size);
    }
    
    uoffset_t GetSize() const { return static_cast<uoffset_t>(buf_.size() - head_); }
    
    uint8_t* GetBufferPointer() const {
        return const_cast<uint8_t*>(buf_.data() + head_);
    }
    
    void Finish(uoffset_t root, const char* /*file_identifier*/ = nullptr) {
        // Align to 4 bytes and write root offset
        Align(sizeof(uoffset_t));
        auto offset = static_cast<uoffset_t>(buf_.size() - root);
        buf_.resize(buf_.size() + sizeof(uoffset_t));
        memcpy(buf_.data() + buf_.size() - sizeof(uoffset_t), &offset, sizeof(uoffset_t));
        head_ -= sizeof(uoffset_t);
    }
    
    uoffset_t StartTable() {
        offsets_.clear();
        return GetSize();
    }
    
    template<typename T> void AddElement(voffset_t /*field*/, T e, T def) {
        if (e == def) {
            offsets_.push_back(0);
            return;
        }
        auto align = AlignOf(sizeof(T));
        Align(align);
        auto off = GetSize();
        auto val = EndianScalar(e);
        buf_.resize(buf_.size() + sizeof(T));
        memcpy(buf_.data() + buf_.size() - sizeof(T), &val, sizeof(T));
        offsets_.push_back(off);
    }
    
    void AddOffset(voffset_t /*field*/, uoffset_t off) {
        if (off == 0) {
            offsets_.push_back(0);
            return;
        }
        Align(sizeof(uoffset_t));
        auto thisoff = GetSize();
        auto relative_off = static_cast<soffset_t>(buf_.size() - off + sizeof(soffset_t));
        auto val = EndianScalar(relative_off);
        buf_.resize(buf_.size() + sizeof(soffset_t));
        memcpy(buf_.data() + buf_.size() - sizeof(soffset_t), &val, sizeof(soffset_t));
        offsets_.push_back(thisoff);
    }
    
    template<typename T> void AddStruct(voffset_t /*field*/, const T* struct_ptr) {
        if (!struct_ptr) {
            offsets_.push_back(0);
            return;
        }
        auto align = AlignOf(alignof(T));
        Align(align);
        auto off = GetSize();
        buf_.resize(buf_.size() + sizeof(T));
        memcpy(buf_.data() + buf_.size() - sizeof(T), struct_ptr, sizeof(T));
        offsets_.push_back(off);
    }
    
    uoffset_t EndTable(uoffset_t start) {
        auto voffset = static_cast<voffset_t>(offsets_.size());
        auto vtable_size = static_cast<voffset_t>((voffset + 2) * sizeof(voffset_t));
        
        // Write vtable entries
        buf_.resize(buf_.size() + vtable_size);
        auto* vtable = reinterpret_cast<voffset_t*>(buf_.data() + buf_.size() - vtable_size);
        vtable[0] = EndianScalar(vtable_size);
        vtable[1] = EndianScalar(static_cast<voffset_t>(GetSize() - start));
        
        // Write field offsets
        for (voffset_t i = 0; i < voffset; i++) {
            auto field_off = offsets_[i];
            if (field_off == 0) {
                vtable[i + 2] = 0;
            } else {
                vtable[i + 2] = EndianScalar(static_cast<voffset_t>(GetSize() - field_off));
            }
        }
        
        // Patch table pointer to vtable
        auto table_start = GetSize();
        *reinterpret_cast<soffset_t*>(buf_.data() + start) = EndianScalar(static_cast<soffset_t>(vtable_size));
        
        return table_start;
    }
    
    template<typename T> Offset<Vector<T>> CreateVector(const T* data, size_t len) {
        Align(AlignOf(sizeof(T)));
        auto len_uoffset = static_cast<uoffset_t>(len);
        buf_.resize(buf_.size() + sizeof(uoffset_t) + len * sizeof(T));
        memcpy(buf_.data() + buf_.size() - len * sizeof(T) - sizeof(uoffset_t), &len_uoffset, sizeof(uoffset_t));
        memcpy(buf_.data() + buf_.size() - len * sizeof(T), data, len * sizeof(T));
        return Offset<Vector<T>>(GetSize());
    }
    
    template<typename T> Offset<Vector<T>> CreateVector(const std::vector<T>& data) {
        return CreateVector(data.data(), data.size());
    }
    
    Offset<String> CreateString(const char* str, size_t len) {
        Align(AlignOf(sizeof(uoffset_t)));
        auto len_uoffset = static_cast<uoffset_t>(len);
        buf_.resize(buf_.size() + sizeof(uoffset_t) + len + 1);
        memcpy(buf_.data() + buf_.size() - len - 1 - sizeof(uoffset_t), &len_uoffset, sizeof(uoffset_t));
        memcpy(buf_.data() + buf_.size() - len - 1, str, len);
        buf_[buf_.size() - 1] = 0;  // null terminator
        return Offset<String>(GetSize());
    }
    
    Offset<String> CreateString(const std::string& str) {
        return CreateString(str.c_str(), str.length());
    }
    
    template<typename T> Offset<Vector<Offset<T>>> CreateVector(const std::vector<Offset<T>>& data) {
        Align(AlignOf(sizeof(uoffset_t)));
        auto len = data.size();
        auto len_uoffset = static_cast<uoffset_t>(len);
        buf_.resize(buf_.size() + sizeof(uoffset_t) + len * sizeof(uoffset_t));
        memcpy(buf_.data() + buf_.size() - len * sizeof(uoffset_t) - sizeof(uoffset_t), &len_uoffset, sizeof(uoffset_t));
        for (size_t i = 0; i < len; i++) {
            auto val = EndianScalar(data[i].o_value());
            memcpy(buf_.data() + buf_.size() - (len - i) * sizeof(uoffset_t), &val, sizeof(uoffset_t));
        }
        return Offset<Vector<Offset<T>>>(GetSize());
    }
};

// Root table accessors
template<typename T> const T* GetRoot(const void* buf) {
    auto end = reinterpret_cast<const uint8_t*>(buf);
    auto o = EndianScalar(*reinterpret_cast<const uoffset_t*>(buf));
    return reinterpret_cast<const T*>(end + sizeof(uoffset_t) + o);
}

// Macros for struct alignment
#ifndef FLATBUFFERS_MANUALLY_ALIGNED_STRUCT
#define FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(align) \
    #pragma pack(push, 1)
#endif

#ifndef FLATBUFFERS_STRUCT_END
#define FLATBUFFERS_STRUCT_END(name, size) \
    static_assert(sizeof(name) == size, "compiler breaks packing rules"); \
    #pragma pack(pop)
#endif

// Template specialization for Vector<Offset<T>>
template<typename T> class Vector<Offset<T>> {
    const uoffset_t* Data() const { return reinterpret_cast<const uoffset_t*>(this); }
public:
    uoffset_t size() const { return EndianScalar(Data()[0]); }
    
    class const_iterator {
        const uoffset_t* data_;
        uoffset_t ind_;
    public:
        const_iterator(const uoffset_t* data, uoffset_t ind) : data_(data), ind_(ind) {}
        const_iterator& operator++() { ++ind_; return *this; }
        bool operator!=(const const_iterator& other) const { return ind_ != other.ind_; }
        const T* operator*() const {
            auto o = EndianScalar(data_[ind_]);
            return reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(data_) + o);
        }
    };
    
    const_iterator begin() const { return const_iterator(Data() + 1, 0); }
    const_iterator end() const { return const_iterator(Data() + 1, size()); }
    
    const T* operator[](uoffset_t i) const {
        auto o = EndianScalar(Data()[i + 1]);
        return reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(Data()) + o);
    }
};

} // namespace flatbuffers

// Default to little-endian
#ifndef FLATBUFFERS_LITTLEENDIAN
#define FLATBUFFERS_LITTLEENDIAN 1
#endif

#endif // FLATBUFFERS_H_
