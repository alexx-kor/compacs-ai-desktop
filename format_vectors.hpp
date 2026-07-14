// COMPACS1 vector store binary format — shared by main.cpp reader and export_vectors writer.
#pragma once

#include <cstddef>
#include <cstdint>

namespace compacs::vectors {

// File header (24 bytes, little-endian):
//   magic[8]     "COMPACS1" (exactly 8 ASCII bytes, no NUL)
//   version u32  must be 1
//   count   u32  number of records
//   dim     u32  embedding dimension (768)
//   reserved u32 unused (writer sets 0)
inline constexpr char kMagic[] = "COMPACS1";
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kDefaultDim = 768;
inline constexpr std::size_t kHeaderSize = 24;
inline constexpr std::size_t kRecordHeaderSize = 14;

// Per-record layout (header 14 bytes + payload):
//   id         u32
//   page       u32
//   source_len u16
//   text_len   u32
//   source     source_len bytes UTF-8
//   text       text_len bytes UTF-8
//   embedding  dim × float32 LE

}  // namespace compacs::vectors
