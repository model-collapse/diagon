# Lucene 90 Stored Fields Format - Complete Wire Format Specification

**Date**: 2026-03-18
**Source**: Apache Lucene 9.0+ source code analysis
**Files Analyzed**:
- `Lucene90StoredFieldsFormat.java`
- `Lucene90CompressingStoredFieldsWriter.java`
- `Lucene90CompressingStoredFieldsReader.java`
- `FieldsIndexWriter.java`
- `FieldsIndexReader.java`
- `StoredFieldsInts.java`

## Executive Summary

Lucene 90 stores documents in **3 files** with **block-based compression**:
- `.fdt` (stored fields data) - compressed document blocks with headers
- `.fdx` (stored fields index) - DirectMonotonic arrays mapping docID→filePointer
- `.fdm` (stored fields metadata) - DirectMonotonic metadata + statistics

**Key Design**:
- Documents grouped into **chunks** (configurable 80KB for BEST_SPEED, 480KB for BEST_COMPRESSION)
- Each chunk compressed separately (LZ4 or DEFLATE)
- Block-based index using DirectMonotonicReader for O(log n) lookup
- Stored field values encoded with type tags + compression-friendly formats (ZFloat, ZDouble, TLong)

---

## File Structure Overview

```
.fdt (Stored Fields Data):
├─ CodecUtil Header
├─ Chunk 1 Header (docBase, flags, numStoredFields[], lengths[])
├─ Chunk 1 Compressed Data (LZ4 or DEFLATE)
├─ Chunk 2 Header
├─ Chunk 2 Compressed Data
├─ ... (more chunks)
└─ CodecUtil Footer

.fdx (Stored Fields Index):
├─ CodecUtil Header
├─ DirectMonotonicReader docs array (numChunks+1 entries)
├─ DirectMonotonicReader startPointers array (numChunks+1 entries)
└─ CodecUtil Footer

.fdm (Stored Fields Metadata):
├─ CodecUtil Header
├─ chunkSize (VInt)
├─ DirectMonotonic metadata for .fdx (docs array, file pointers array)
├─ numChunks (VLong)
├─ numDirtyChunks (VLong)
├─ numDirtyDocs (VLong)
└─ CodecUtil Footer
```

---

## 1. File Format Details: `.fdt` (Stored Fields Data)

### Header

```
BYTES | VALUE
------|-------
16    | CodecUtil header with:
      |   - magic: 0x3FD76C17 (4 bytes)
      |   - codec name: "Lucene90StoredFieldsFastData" or "Lucene90StoredFieldsHighData"
      |   - version: 1 (for both BEST_SPEED and BEST_COMPRESSION)
      |   - segment ID (16 bytes)
      |   - segment suffix (variable)
```

### Chunk Header (one per block of documents)

Written sequentially for each chunk flush:

```
VInt  | docBase                      -- first document ID in this chunk
VInt  | (numBufferedDocs << 2) | flags
      |   - bits [31:2]: numBufferedDocs (how many docs in chunk)
      |   - bit 1: sliced flag (=1 if chunk > 2*chunkSize)
      |   - bit 0: dirtyChunk flag (=1 if incomplete flush)
```

### Number of Stored Fields Per Document

- **If 1 document**: `VInt` single value
- **If multiple**: `StoredFieldsInts::writeInts()` - optimized encoding

### Document Lengths Array

Array of byte lengths for each document in the chunk (after decompression):

- **If 1 document**: `VInt` single length
- **If multiple**: `StoredFieldsInts::writeInts()` - stored as length[i], not offsets

The reader converts lengths to offsets for decompression.

### StoredFieldsInts Encoding (for multi-value arrays)

Adaptive per-chunk encoding based on value range:

```
BYTE  | TYPE       | ENCODING
------|------------|----------
0x00  | All equal  | VInt value (all elements are same)
0x08  | Bytes      | 128-value blocks, 8-bit packing
0x10  | Shorts     | 128-value blocks, 16-bit packing
0x20  | Ints       | 128-value blocks, 32-bit packing
```

**Block structure (for 0x08, 0x10, 0x20)**:
- Process in chunks of 128 values
- Pack using technique matching bit width (transpose bytes/words/ints within blocks)
- Final incomplete block written individually

### Compressed Document Data

After header and metadata:

```
COMPRESSED_DATA:
if (sliced):
    // Chunk > 2*chunkSize, split into 8KB/48KB sub-blocks
    for each 8KB/48KB:
        compressed_block (LZ4 or DEFLATE)
else:
    // Single compressed block
    compressed_block (LZ4 or DEFLATE)
```

**Decompressor state during reading**:
- Total decompressed length known from offsets array
- For sliced chunks: decompressor.decompress(in, chunkSize, offset, length, out)
  - `chunkSize`: block size in compressed stream (8KB/48KB)
  - `offset`: byte offset within decompressed document
  - `length`: bytes to decompress starting at offset

### Document Data Format (inside decompressed block)

Each stored field encoded as:

```
VLong  | (fieldNumber << TYPE_BITS) | type
       |   - top bits: field number (0-65535 typical)
       |   - bottom 3 bits: field type (0-5)

VALUE  | Depends on type:
       |   0x00 = STRING       → writeString() (VInt length + UTF-8 bytes)
       |   0x01 = BYTE_ARR     → VInt length + raw bytes
       |   0x02 = NUMERIC_INT  → ZInt (variable-length int)
       |   0x03 = NUMERIC_FLOAT→ ZFloat (1-5 bytes)
       |   0x04 = NUMERIC_LONG → TLong (1-10 bytes)
       |   0x05 = NUMERIC_DOUBLE → ZDouble (1-9 bytes)
```

**TYPE_BITS = 3** (PackedInts.bitsRequired(5) = 3)

### Footer

```
BYTES | VALUE
------|-------
16    | CodecUtil footer with checksum
```

---

## 2. Specialized Numeric Formats

### ZInt (variable-length signed int)

Uses zigzag encoding for small integers (handled by writeZInt/readZInt in DataOutput).

### ZFloat (1-5 bytes)

```
BYTE [0]  | MEANING
----------|--------
0xFF      | Negative value → read 4 more bytes as IEEE 754 int bits
0x80-0xFE | Small integer: ((byte & 0x7F) - 1) in range [-1, 125]
0x00-0x7F | Positive float: this byte + 3 more bytes (IEEE 754)
```

### ZDouble (1-9 bytes)

```
BYTE [0]  | MEANING
----------|--------
0xFF      | Negative value → read 8 more bytes as IEEE 754 long bits
0xFE      | Float value → read 4 more bytes as float bits, convert to double
0x80-0xFD | Small integer: ((byte & 0x7F) - 1) in range [-1, 124]
0x00-0x7F | Positive double: this byte + 7 more bytes (IEEE 754)
```

### TLong (1-10 bytes for timestamps)

Optimized for timestamp values with day/hour/second precision:

```
BYTE [0]  | BITS      | MEANING
----------|-----------|--------
[1:0]     | 00        | Uncompressed long
          | 01        | Multiple of 1000 (second precision)
          | 10        | Multiple of 3600000 (hour precision)
          | 11        | Multiple of 86400000 (day precision)
[5]       |           | Continuation bit (1 = more bytes follow)
[4:0]     |           | Lower 5 bits of encoded value

FOLLOWING | VLong (if continuation bit set)
BYTES     | Upper bits of zigzag-encoded value
```

---

## 3. File Format Details: `.fdx` (Stored Fields Index)

### Header

```
BYTES | VALUE
------|-------
16    | CodecUtil header
      |   - codec name: "Lucene90FieldsIndex"
      |   - version: 1
```

### Content (generated by FieldsIndexWriter.finish())

Two DirectMonotonicReader arrays packed into single file:

```
SECTION 1: Document IDs Array
  - DirectMonotonicWriter.getInstance() generates monotonic doc IDs
  - Format: packed array (64-bit values, DirectMonotonic)
  - Entries: [0, docs_in_chunk_0, docs_in_chunk_0 + docs_in_chunk_1, ...]
  - totalChunks+1 entries

SECTION 2: File Pointers Array
  - DirectMonotonicWriter.getInstance() generates file pointers
  - Format: packed array (64-bit values, DirectMonotonic)
  - Entries: [0, fp_chunk_0, fp_chunk_0 + fp_chunk_1, ...]
  - Stores deltas: fp_delta = fp_current - fp_previous
  - totalChunks+1 entries
```

### Footer

```
BYTES | VALUE
------|-------
16    | CodecUtil footer with checksum
```

---

## 4. File Format Details: `.fdm` (Stored Fields Metadata)

### Header

```
BYTES | VALUE
------|-------
16    | CodecUtil header
      |   - codec name: "Lucene90FieldsIndexMeta"
      |   - version: 0 (META_VERSION_START)
```

### Metadata Entries (written in order)

```
VInt   | chunkSize                    -- minimum bytes per chunk (80KB or 480KB)
Int    | maxDoc (numDocs)             -- total documents in segment
Int    | blockShift                   -- log2 of block size for DirectMonotonic
Int    | totalChunks + 1              -- number of chunk boundaries
Long   | docsStartPointer             -- byte offset in .fdx where docs array begins
       | (DirectMonotonicWriter metadata follows for docs array)
Long   | docsEndPointer / startPointersStartPointer
       | (DirectMonotonicWriter metadata follows for startPointers array)
Long   | startPointersEndPointer
Long   | maxPointer                   -- end of .fdt file (last byte written)
VLong  | numChunks                    -- total number of chunks written
VLong  | numDirtyChunks               -- chunks marked as "dirty" (incomplete flush)
VLong  | numDirtyDocs                 -- total docs in dirty chunks
```

### Footer

```
BYTES | VALUE
------|-------
16    | CodecUtil footer with checksum
```

---

## 5. Chunk Flushing Logic

Documents are buffered in memory until trigger condition:

```
TRIGGER = (bufferedDocs.size() >= chunkSize) OR (numBufferedDocs >= maxDocsPerChunk)
```

**Default settings**:
- **BEST_SPEED**: chunkSize=80KB, maxDocsPerChunk=10
- **BEST_COMPRESSION**: chunkSize=480KB, maxDocsPerChunk=10

On flush:

1. Write chunk header (docBase, flags, numStoredFields[], lengths[])
2. Create ByteBuffersDataInput from buffered data
3. If sliced (totalSize > 2*chunkSize):
   - Compress in multiple chunkSize-byte blocks
4. Else:
   - Compress as single block
5. Write index entry (via FieldsIndexWriter.writeIndex())
6. Update statistics (numChunks, numDirtyChunks if forced flush)

---

## 6. Index Lookup Algorithm

**Finding document offset in .fdt**:

```java
// FieldsIndexReader.getBlockID(docID)
long blockIndex = docs.binarySearch(0, numChunks, docID);
if (blockIndex < 0) {
    blockIndex = -2 - blockIndex;  // convert insertion point
}
long blockStartPointer = startPointers.get(blockIndex);
```

**DirectMonotonicReader.binarySearch()**:
- Binary search on monotonic array of document IDs
- Returns index where docID would fit
- Negative return: insertion point = -blockIndex - 2

---

## 7. Merge Strategy

Three merge strategies based on compatibility:

| Strategy | Condition | Action |
|----------|-----------|--------|
| BULK | Same format, compression, chunk size + no deletions + clean chunks | Copy compressed blocks as-is |
| DOC | Different format or has deletions | Decompress and re-compress per document |
| VISITOR | Field reordering needed | Decompress and visit each field |

---

## 8. Critical Implementation Details

### CodecUtil Headers

```
Magic: 0x3FD76C17 (constant)
Version: varies (1 for Lucene90, 0 for metadata)
Segment ID: 16-byte unique identifier
Segment Suffix: variable-length string suffix
```

### DirectMonotonicReader

- Stores strictly monotonically increasing values
- Uses block-based encoding to achieve O(1) random access
- Supports up to 2^63-1 values
- Block shift parameter: 4-21 (power of 2 for block size)

### Compression Modes

| Mode | Algorithm | Block Size | Compression Ratio |
|------|-----------|------------|-------------------|
| BEST_SPEED | LZ4 | 8KB | ~3.5x (for text) |
| BEST_COMPRESSION | DEFLATE + preset dict | 48KB | ~4.5x (for text) |

---

## 9. Example: Reading Document 42

**Step 1**: Load .fdm and .fdx
- Read metadata: maxDoc, blockShift, numChunks, pointer positions
- Load DirectMonotonicReader for docs array
- Load DirectMonotonicReader for pointers array

**Step 2**: Find chunk containing doc 42
```
blockIndex = docs.binarySearch(0, numChunks, 42)
// Returns index of chunk starting with doc 42 or later
```

**Step 3**: Get file pointer to chunk in .fdt
```
blockStartPointer = startPointers.get(blockIndex)
```

**Step 4**: Read chunk header from .fdt at blockStartPointer
```
docBase = readVInt()           // e.g., 40
token = readVInt()            // (numDocs << 2) | flags
numDocs = token >>> 2         // e.g., 10 (docs 40-49)
sliced = (token & 1) != 0
dirtyChunk = (token & 2) != 0
numStoredFields[] = readInts()
lengths[] = readInts()
```

**Step 5**: Decompress document data
```
offset_in_chunk = 0
for i in 0..41-docBase:     // find offset for doc 42
    offset_in_chunk += lengths[i]

length = lengths[42 - docBase]
decompressor.decompress(input, chunkSize, offset_in_chunk, length, output)
```

**Step 6**: Parse document fields
```
numStoredFields = numStoredFields[42 - docBase]
for fieldNum in 0..numStoredFields-1:
    infoAndBits = readVLong()
    fieldNumber = infoAndBits >>> TYPE_BITS
    fieldType = infoAndBits & TYPE_MASK
    value = readValue(fieldType)
```

---

## 10. Configuration Parameters

### Codec Names

- **Data file header**: "Lucene90StoredFieldsFastData" or "Lucene90StoredFieldsHighData"
- **Index file header**: "Lucene90FieldsIndex"
- **Metadata file header**: "Lucene90FieldsIndexMeta"

### Configurable Parameters

```cpp
// Suggested C++ equivalents
const int CHUNK_SIZE_BEST_SPEED = 10 * 8 * 1024;     // 80KB
const int CHUNK_SIZE_BEST_COMPRESSION = 10 * 48 * 1024; // 480KB
const int MAX_DOCS_PER_CHUNK = 10;
const int BLOCK_SHIFT = 4;  // 2^4 = 16 values per DirectMonotonic block
```

---

## 11. Known Limitations

1. **Document size limit**: Single document cannot exceed 2^31 - 2^14 bytes
2. **Dirty chunks**: Incomplete final chunks tracked separately; merged index may recompress if >1% blocks are dirty
3. **Type field limit**: Field numbers 0-65535 (10 bits after shifting by 3 bits)
4. **No delta encoding between chunks**: Each chunk is independently compressed

---

## 12. Differences from Lucene 87 and Earlier

- **DirectMonotonicReader for index**: Replaces simple array lookup
- **Chunk metadata in header**: numStoredFields[] and lengths[] now in chunk header (not metadata file)
- **Dirty chunk tracking**: New fields for incomplete chunk detection during merge
- **Block-based compression**: Sub-chunking for documents > 2*chunkSize

---

## Implementation Checklist for C++

- [ ] CodecUtil header/footer writing
- [ ] StoredFieldsInts encoder/decoder (0x00, 0x08, 0x10, 0x20 modes)
- [ ] ZFloat/ZDouble/TLong encoding
- [ ] DirectMonotonicWriter integration
- [ ] DirectMonotonicReader integration
- [ ] LZ4 compression integration
- [ ] DEFLATE compression integration
- [ ] Chunk-based buffering and flushing
- [ ] Block-based decompression with slicing
- [ ] Index lookup via binary search on DirectMonotonic arrays
- [ ] Merge strategy logic (BULK/DOC/VISITOR)
- [ ] Dirty chunk detection and recompression
