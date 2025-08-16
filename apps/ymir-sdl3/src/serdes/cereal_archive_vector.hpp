#pragma once

#include "cereal/cereal.hpp"

#include <algorithm>
#include <vector>

namespace cereal {

template <typename T>
    requires(sizeof(T) == sizeof(char))
class BinaryVectorOutputArchive : public OutputArchive<BinaryVectorOutputArchive<T>, AllowEmptyClassElision> {
public:
    BinaryVectorOutputArchive(std::vector<T> &vector)
        : OutputArchive<BinaryVectorOutputArchive<T>, AllowEmptyClassElision>(this)
        , itsVector(vector) {}

    ~BinaryVectorOutputArchive() CEREAL_NOEXCEPT = default;

    void saveBinary(const void *data, std::streamsize size) {
        itsVector.insert(itsVector.end(), (const T *)data, (const T *)data + size);
    }

private:
    std::vector<T> &itsVector;
};

template <typename T>
    requires(sizeof(T) == sizeof(char))
class BinaryVectorInputArchive : public InputArchive<BinaryVectorInputArchive<T>, AllowEmptyClassElision> {
public:
    BinaryVectorInputArchive(std::vector<T> &vector)
        : InputArchive<BinaryVectorInputArchive<T>, AllowEmptyClassElision>(this)
        , itsVector(vector) {}

    ~BinaryVectorInputArchive() CEREAL_NOEXCEPT = default;

    void loadBinary(void *const data, std::streamsize size) {
        auto const readSize = std::min<size_t>(size, itsVector.size() - pos);

        if (readSize != size)
            throw Exception("Failed to read " + std::to_string(size) + " bytes from input stream! Read " +
                            std::to_string(readSize));

        std::copy_n(itsVector.begin() + pos, readSize, (T *const)data);
        pos += readSize;
    }

private:
    std::vector<T> &itsVector;
    size_t pos = 0;
};

// ######################################################################
// Common BinaryArchive serialization functions

//! Saving for POD types to binary
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, void>::type
CEREAL_SAVE_FUNCTION_NAME(BinaryVectorOutputArchive<char> &ar, T const &t) {
    ar.saveBinary(std::addressof(t), sizeof(t));
}

//! Loading for POD types from binary
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, void>::type
CEREAL_LOAD_FUNCTION_NAME(BinaryVectorInputArchive<char> &ar, T &t) {
    ar.loadBinary(std::addressof(t), sizeof(t));
}

//! Serializing NVP types to binary
template <class Archive, class T>
inline CEREAL_ARCHIVE_RESTRICT(BinaryVectorInputArchive<char>, BinaryVectorOutputArchive<char>)
    CEREAL_SERIALIZE_FUNCTION_NAME(Archive &ar, NameValuePair<T> &t) {
    ar(t.value);
}

//! Serializing SizeTags to binary
template <class Archive, class T>
inline CEREAL_ARCHIVE_RESTRICT(BinaryVectorInputArchive<char>, BinaryVectorOutputArchive<char>)
    CEREAL_SERIALIZE_FUNCTION_NAME(Archive &ar, SizeTag<T> &t) {
    ar(t.size);
}

//! Saving binary data
template <class T>
inline void CEREAL_SAVE_FUNCTION_NAME(BinaryVectorOutputArchive<char> &ar, BinaryData<T> const &bd) {
    ar.saveBinary(bd.data, static_cast<std::streamsize>(bd.size));
}

//! Loading binary data
template <class T>
inline void CEREAL_LOAD_FUNCTION_NAME(BinaryVectorInputArchive<char> &ar, BinaryData<T> &bd) {
    ar.loadBinary(bd.data, static_cast<std::streamsize>(bd.size));
}

//! Saving for POD types to binary
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, void>::type
CEREAL_SAVE_FUNCTION_NAME(BinaryVectorOutputArchive<unsigned char> &ar, T const &t) {
    ar.saveBinary(std::addressof(t), sizeof(t));
}

//! Loading for POD types from binary
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, void>::type
CEREAL_LOAD_FUNCTION_NAME(BinaryVectorInputArchive<unsigned char> &ar, T &t) {
    ar.loadBinary(std::addressof(t), sizeof(t));
}

//! Serializing NVP types to binary
template <class Archive, class T>
inline CEREAL_ARCHIVE_RESTRICT(BinaryVectorInputArchive<unsigned char>, BinaryVectorOutputArchive<unsigned char>)
    CEREAL_SERIALIZE_FUNCTION_NAME(Archive &ar, NameValuePair<T> &t) {
    ar(t.value);
}

//! Serializing SizeTags to binary
template <class Archive, class T>
inline CEREAL_ARCHIVE_RESTRICT(BinaryVectorInputArchive<unsigned char>, BinaryVectorOutputArchive<unsigned char>)
    CEREAL_SERIALIZE_FUNCTION_NAME(Archive &ar, SizeTag<T> &t) {
    ar(t.size);
}

//! Saving binary data
template <class T>
inline void CEREAL_SAVE_FUNCTION_NAME(BinaryVectorOutputArchive<unsigned char> &ar, BinaryData<T> const &bd) {
    ar.saveBinary(bd.data, static_cast<std::streamsize>(bd.size));
}

//! Loading binary data
template <class T>
inline void CEREAL_LOAD_FUNCTION_NAME(BinaryVectorInputArchive<unsigned char> &ar, BinaryData<T> &bd) {
    ar.loadBinary(bd.data, static_cast<std::streamsize>(bd.size));
}

//! Saving for POD types to binary
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, void>::type
CEREAL_SAVE_FUNCTION_NAME(BinaryVectorOutputArchive<signed char> &ar, T const &t) {
    ar.saveBinary(std::addressof(t), sizeof(t));
}

//! Loading for POD types from binary
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, void>::type
CEREAL_LOAD_FUNCTION_NAME(BinaryVectorInputArchive<signed char> &ar, T &t) {
    ar.loadBinary(std::addressof(t), sizeof(t));
}

//! Serializing NVP types to binary
template <class Archive, class T>
inline CEREAL_ARCHIVE_RESTRICT(BinaryVectorInputArchive<signed char>, BinaryVectorOutputArchive<signed char>)
    CEREAL_SERIALIZE_FUNCTION_NAME(Archive &ar, NameValuePair<T> &t) {
    ar(t.value);
}

//! Serializing SizeTags to binary
template <class Archive, class T>
inline CEREAL_ARCHIVE_RESTRICT(BinaryVectorInputArchive<signed char>, BinaryVectorOutputArchive<signed char>)
    CEREAL_SERIALIZE_FUNCTION_NAME(Archive &ar, SizeTag<T> &t) {
    ar(t.size);
}

//! Saving binary data
template <class T>
inline void CEREAL_SAVE_FUNCTION_NAME(BinaryVectorOutputArchive<signed char> &ar, BinaryData<T> const &bd) {
    ar.saveBinary(bd.data, static_cast<std::streamsize>(bd.size));
}

//! Loading binary data
template <class T>
inline void CEREAL_LOAD_FUNCTION_NAME(BinaryVectorInputArchive<signed char> &ar, BinaryData<T> &bd) {
    ar.loadBinary(bd.data, static_cast<std::streamsize>(bd.size));
}

} // namespace cereal

// register archives for polymorphic support
CEREAL_REGISTER_ARCHIVE(cereal::BinaryVectorOutputArchive<char>)
CEREAL_REGISTER_ARCHIVE(cereal::BinaryVectorInputArchive<char>)
CEREAL_REGISTER_ARCHIVE(cereal::BinaryVectorOutputArchive<unsigned char>)
CEREAL_REGISTER_ARCHIVE(cereal::BinaryVectorInputArchive<unsigned char>)
CEREAL_REGISTER_ARCHIVE(cereal::BinaryVectorOutputArchive<signed char>)
CEREAL_REGISTER_ARCHIVE(cereal::BinaryVectorInputArchive<signed char>)

// tie input and output archives together
CEREAL_SETUP_ARCHIVE_TRAITS(cereal::BinaryVectorInputArchive<char>, cereal::BinaryVectorOutputArchive<char>)
CEREAL_SETUP_ARCHIVE_TRAITS(cereal::BinaryVectorInputArchive<unsigned char>,
                            cereal::BinaryVectorOutputArchive<unsigned char>)
CEREAL_SETUP_ARCHIVE_TRAITS(cereal::BinaryVectorInputArchive<signed char>,
                            cereal::BinaryVectorOutputArchive<signed char>)
