# Design 16: OpenSearch Index Format Compatibility

## Status: DESIGN SPEC

## Goal

Enable Diagon to produce index files that are **byte-level compatible** with OpenSearch/Lucene, so that:
1. An index written by Diagon in "OS-compat" mode can be opened by OpenSearch directly
2. An index written by OpenSearch can be opened by Diagon
3. Diagon can auto-detect which format a file uses (native vs OS-compat)
4. Both modes coexist — native mode retains all Diagon optimizations

## Motivation

- **Adoption**: Drop-in replacement for OpenSearch indexing pipeline
- **Validation**: Bit-exact comparison with OpenSearch output for correctness testing
- **Migration**: Users can migrate between Diagon and OpenSearch without reindexing
- **Interop**: Mixed clusters (Diagon indexers + OpenSearch searchers or vice versa)

---

## 1. Format Gap Analysis

### 1.1 Structural Framing

| Aspect | Diagon (Native) | OpenSearch/Lucene | Gap |
|--------|-----------------|-------------------|-----|
| File header | String codec name + VInt version | `CodecUtil`: magic(0x3fd76c17) + String codecName + int32 version (all big-endian) | **Critical** |
| Index header | No segment ID, no suffix | CodecHeader + 16-byte segmentID + 1-byte suffixLen + suffix | **Critical** |
| File footer | None | `~CODEC_MAGIC` + algorithmID(0) + CRC32 (16 bytes, big-endian) | **Critical** |
| segments_N | Custom: magic(0x3fd76c17) + int32(1) + generation + segments inline | Lucene: IndexHeader + LuceneVersion + version + nameCounter + segCount + per-seg metadata + CommitUserData + Footer | **Critical** |
| .si file | Not written (data in segments_N) | Per-segment file with IndexHeader, version, attributes, diagnostics, files, Footer | **Critical** |
| .fnm file | Not written (FieldInfos in segments_N) | Per-segment file with IndexHeader, per-field metadata, Footer | **Critical** |

### 1.2 Postings Format (.doc / .pos / .pay / .tim / .tip / .tmd)

| Aspect | Diagon (Lucene104) | OpenSearch (Lucene104) | Gap |
|--------|---------------------|------------------------|-----|
| Block size | 128 docs | 128 docs | Same |
| .doc codec name | (no header) | `"Lucene104PostingsWriterDoc"` | Header missing |
| .doc encoding | BitPack128 + PFOR-Delta (1-byte bitsPerValue header) | PackedInts (ForUtil) | **Different encoding** |
| .pos codec name | (no header) | `"Lucene104PostingsWriterPos"` | Header missing |
| .pay file | Not written | Written when payloads exist | **Missing file** |
| .tim codec name | Custom block header | `"BlockTreeTermsDict"` IndexHeader | **Different header** |
| .tim block format | VInt termCount, column-stride metadata, LZ4 suffixes | VInt termCount, different metadata layout | **Different wire format** |
| .tip codec name | TIP6 magic (0x54495036) | `"BlockTreeTermsIndex"` IndexHeader + FST/Trie | **Different trie format** |
| .tmd file | Not written | `"BlockTreeTermsMeta"` with per-field aggregates | **Missing file** |
| Skip data | .skp file (separate) | Inline in .doc file | **Different location** |

### 1.3 Stored Fields (.fdt / .fdx / .fdm)

| Aspect | Diagon | OpenSearch (Lucene90) | Gap |
|--------|--------|------------------------|-----|
| Files | .fdt + .fdx (2 files) | .fdt + .fdx + .fdm (3 files) | **Missing .fdm** |
| .fdt header | String "DiagonStoredFields" + VInt version | `"Lucene90FieldsData"` IndexHeader | **Different header** |
| .fdt compression | LZ4 blocks | LZ4 or DEFLATE blocks (configurable) | Similar but different block framing |
| .fdx format | VInt blockOffsets | FieldsIndexWriter (DirectMonotonicWriter) | **Different index format** |
| .fdm format | N/A | Metadata: numDocs, numChunks, numDirtyChunks | **Missing** |
| Field types | STRING/INT/LONG (3 bits) | STRING/BYTE_ARR/NUMERIC_INT/FLOAT/LONG/DOUBLE (3 bits) | **Fewer types** |

### 1.4 Doc Values (.dvd / .dvm)

| Aspect | Diagon | OpenSearch (Lucene90) | Gap |
|--------|--------|------------------------|-----|
| .dvd header | String "DiagonDocValues" + VInt version | `"Lucene90DocValuesData"` IndexHeader | **Different header** |
| .dvm header | N/A (metadata inline) | `"Lucene90DocValuesMetadata"` IndexHeader | **Missing meta file** |
| Encoding | Custom per-type | Lucene90 encoding (DirectWriter, etc.) | **Different encoding** |

### 1.5 Norms (.nvd / .nvm)

| Aspect | Diagon | OpenSearch (Lucene90) | Gap |
|--------|--------|------------------------|-----|
| .nvd header | Custom V1/V2 | `"Lucene90NormsData"` IndexHeader | **Different header** |
| .nvm header | Custom | `"Lucene90NormsMetadata"` IndexHeader | **Different header** |
| Encoding | Sparse norms, custom | Lucene90 encoding | **Different encoding** |

### 1.6 Points/BKD (.kdd / .kdi / .kdm)

| Aspect | Diagon | OpenSearch (Lucene90) | Gap |
|--------|--------|------------------------|-----|
| .kdm header | Custom (per-field metadata) | `"Lucene90PointsFormatMeta"` IndexHeader | **Different header** |
| .kdi header | Custom KDI v2 | `"Lucene90PointsFormatIndex"` IndexHeader | **Different header** |
| .kdd header | Custom | `"Lucene90PointsFormatData"` IndexHeader | **Different header** |
| Tree format | Custom post-order, delta-encoded | Lucene90 post-order | **Likely similar but need verification** |

### 1.7 Live Docs (.liv)

| Aspect | Diagon | OpenSearch (Lucene90) | Gap |
|--------|--------|------------------------|-----|
| Header | String "DiagonLiveDocs" + VInt 1 | `"Lucene90LiveDocs"` IndexHeader | **Different header** |
| Format | numDocs + delCount + uint64[] bitset | Lucene FixedBitSet (long[] bits) | **Similar, verify wire format** |

### 1.8 Missing Formats

| Format | Status in Diagon | Required for OS-compat |
|--------|------------------|------------------------|
| Term Vectors (.tvd/.tvx/.tvm) | Stub | Optional (only if storeTermVector=true) |
| Compound Files (.cfs/.cfe) | Partial (CompoundFileWriter exists) | Optional (compound file is a config option) |
| KNN Vectors (.vec/.vex/.vem) | Stub | Optional (only for vector search) |

---

## 2. Architecture: Dual-Mode Codec

### 2.1 Codec Registry

```
Codec::getDefault()  →  "Diagon104"     (native, all optimizations)
Codec::forName("Lucene104")  →  Lucene104OSCodec  (OS-compatible)
```

Two codecs registered:
- **Diagon104Codec** (current `Lucene104Codec`, renamed): Native format, all optimizations
- **Lucene104OSCodec** (new): Byte-level OpenSearch-compatible output

### 2.2 Format Detection (Adaptive Read)

Every Diagon reader already needs to handle multiple format versions. We extend this to detect Lucene vs. Diagon formats:

```
Reader opens file → reads first 4 bytes:
  0x3fd76c17 (CODEC_MAGIC)  → Lucene CodecUtil header → read codecName → dispatch
  Other magic (e.g., TIP6)  → Diagon native format → dispatch
```

For segments_N:
```
Read first 4 bytes:
  0x3fd76c17 → check codecName == "Lucene90SegmentInfo" → Lucene path
  0x3fd76c17 → check codecName != recognized → Diagon legacy path (reinterpret)
```

The challenge: Diagon's current segments_N **also** starts with `0x3fd76c17` (it borrowed the magic). Resolution: after the magic, Diagon writes `int32(1)` (version), while Lucene writes a String (codecName). A String starts with a VInt length byte. We can distinguish:
- Byte 5 == 0x00 followed by 0x00,0x00,0x01 → Diagon native (int32 version = 1)
- Byte 5 is a VInt length > 0 → Lucene CodecUtil header

### 2.3 File Extension Strategy

**No extension changes needed.** Both modes use the same extensions (.tim, .tip, .doc, etc.). Format detection works via headers. This is how Lucene itself handles codec evolution across versions.

The user's suggestion to differentiate via extensions is a fallback — unnecessary given reliable header-based detection.

---

## 3. Implementation: CodecUtil for C++

### 3.1 New File: `src/core/include/diagon/codecs/CodecUtil.h`

```cpp
namespace diagon::codecs {

class CodecUtil {
public:
    static constexpr int32_t CODEC_MAGIC = 0x3fd76c17;
    static constexpr int32_t FOOTER_MAGIC = ~CODEC_MAGIC;  // 0xc0289de8
    static constexpr int FOOTER_LENGTH = 16;

    // Write Lucene-compatible codec header (big-endian magic + string + big-endian version)
    static void writeHeader(store::IndexOutput& out, const std::string& codec, int version);

    // Write Lucene-compatible index header (header + 16-byte segmentID + suffix)
    static void writeIndexHeader(store::IndexOutput& out, const std::string& codec, int version,
                                 const uint8_t* segmentID, const std::string& suffix);

    // Write Lucene-compatible footer (big-endian ~magic + 0 + CRC32)
    static void writeFooter(store::IndexOutput& out);

    // Read and validate codec header, return version
    static int checkHeader(store::IndexInput& in, const std::string& codec, int minVer, int maxVer);

    // Read and validate index header, return version
    static int checkIndexHeader(store::IndexInput& in, const std::string& codec,
                                int minVer, int maxVer,
                                const uint8_t* expectedSegmentID, const std::string& expectedSuffix);

    // Validate and read footer
    static int64_t checkFooter(store::IndexInput& in);

    // Big-endian I/O helpers (Lucene uses big-endian for header/footer)
    static void writeBEInt(store::IndexOutput& out, int32_t v);
    static int32_t readBEInt(store::IndexInput& in);
    static void writeBELong(store::IndexOutput& out, int64_t v);
    static int64_t readBELong(store::IndexInput& in);
};

}
```

### 3.2 Segment ID

Diagon currently has no segment ID concept. Add a 16-byte random UUID to `SegmentInfo`:

```cpp
class SegmentInfo {
    // ... existing fields ...
    uint8_t segmentID_[16];  // 16-byte random ID (Lucene StringHelper.ID_LENGTH)

    const uint8_t* segmentID() const { return segmentID_; }
    void setSegmentID(const uint8_t* id);
    static void generateSegmentID(uint8_t* out);  // Random 16 bytes
};
```

### 3.3 CRC32 Checksums

Add CRC32 support to `IndexOutput`:

```cpp
class IndexOutput {
    // ... existing ...
    virtual int64_t getChecksum() const;  // CRC32 of all bytes written so far
};
```

This requires wrapping writes with a CRC32 accumulator. Use `<zlib.h>` `crc32()` or a custom implementation.

---

## 4. Format Implementation Priority

### Phase 1: Infrastructure (Week 1)
1. **CodecUtil** — header/footer reading/writing
2. **SegmentID** — 16-byte ID in SegmentInfo
3. **CRC32** — checksumming in IndexOutput
4. **Big-endian I/O** — writeBEInt/readBEInt in IndexOutput/IndexInput
5. **Lucene104OSCodec** skeleton — registers as "Lucene104"

### Phase 2: Metadata Files (Week 2)
6. **.si writer/reader** — Lucene99SegmentInfoFormat compatible
7. **.fnm writer/reader** — Lucene94FieldInfosFormat compatible
8. **segments_N** — Lucene SegmentInfos format
9. **Adaptive detection** for segments_N (Diagon vs Lucene)

### Phase 3: Core Postings (Week 3-4)
10. **.tim writer** — BlockTreeTermsDict format with CodecUtil header/footer
11. **.tip writer** — BlockTreeTermsIndex (Lucene103 trie, already implemented as TIP6)
12. **.tmd writer** — BlockTreeTermsMeta (new file)
13. **.doc writer** — Lucene104PostingsWriterDoc with ForUtil encoding
14. **.pos writer** — Lucene104PostingsWriterPos
15. **.doc/.pos/.tim/.tip/.tmd readers** with adaptive detection

### Phase 4: Stored Fields (Week 5)
16. **.fdt writer** — Lucene90FieldsData format
17. **.fdx writer** — Lucene90FieldsIndex (DirectMonotonicWriter)
18. **.fdm writer** — Lucene90FieldsMetadata
19. **Readers** with adaptive detection

### Phase 5: Doc Values + Norms (Week 6)
20. **.dvd/.dvm** — Lucene90DocValues format
21. **.nvd/.nvm** — Lucene90Norms format
22. **Readers** with adaptive detection

### Phase 6: Auxiliary Formats (Week 7)
23. **.liv** — Lucene90LiveDocs format
24. **.kdd/.kdi/.kdm** — Lucene90Points format
25. **.cfs/.cfe** — Lucene90CompoundFormat (optional)

### Phase 7: End-to-End Validation (Week 8)
26. Write index with Diagon in OS-compat mode → open with OpenSearch
27. Write index with OpenSearch → open with Diagon
28. Bit-exact comparison tests
29. Performance benchmarks (OS-compat mode vs native)

---

## 5. Detailed Format Specifications

### 5.1 segments_N (Lucene Format)

```
IndexHeader:
  magic=0x3fd76c17 (big-endian)
  codecName="Lucene90SegmentInfo"
  version=10
  segmentID=16 bytes (all-zero for segments_N since it's not a per-segment file)
  suffixLength=0

LuceneVersion: major(VInt) minor(VInt) bugfix(VInt)
Version: int64 (commit counter)
NameCounter: int32 (next segment name number)
SegCount: int32
MinSegmentLuceneVersion: major(VInt) minor(VInt) bugfix(VInt)  [only if SegCount > 0]

Per segment:
  SegName: String
  SegID: 16 bytes
  SegCodec: String (e.g., "Lucene104")
  DelGen: int64 (-1 = no deletes)
  DeletionCount: int32
  FieldInfosGen: int64 (-1)
  DocValuesGen: int64 (-1)
  SoftDeleteCount: int32 (0)
  SciID: 1 byte (0)
  FieldInfosFiles: Set<String> (empty)
  DocValuesUpdatesFiles: Map<int, Set<String>> (empty)

CommitUserData: Map<String, String> (empty)
Footer: ~magic + algorithmID(0) + CRC32 (16 bytes)
```

### 5.2 .si File (Per-Segment Info)

```
IndexHeader:
  codecName="Lucene90SegmentInfo"
  version=3
  segmentID=16 bytes
  suffix=""

LuceneVersion: major(VInt) minor(VInt) bugfix(VInt)
HasMinVersion: byte (1)
MinVersion: major(VInt) minor(VInt) bugfix(VInt)
DocCount: int32
IsCompoundFile: byte (0 or 1)
Diagnostics: Map<String, String>
Files: Set<String>
Attributes: Map<String, String>
SortField[]: VInt(0) [no sorting]

Footer
```

### 5.3 .fnm File (Field Infos)

```
IndexHeader:
  codecName="Lucene94FieldInfos"
  version=1
  segmentID=16 bytes
  suffix=""

FieldCount: VInt
Per field:
  FieldName: String
  FieldNumber: VInt
  FieldBits: byte (indexed | hasVectors | omitNorms | hasPayloads | softDeletesField)
  IndexOptions: byte (0-4: NONE/DOCS/DOCS_AND_FREQS/DOCS_AND_FREQS_AND_POSITIONS/ALL)
  DocValuesType: byte (0-5: NONE/NUMERIC/BINARY/SORTED/SORTED_NUMERIC/SORTED_SET)
  DocValuesGen: int64 (-1)
  Attributes: Map<String, String>
  PointDimensionCount: VInt
  PointIndexDimensionCount: VInt
  PointNumBytes: VInt
  VectorDimension: VInt (0)
  VectorEncoding: byte (0)
  VectorSimilarity: byte (0)
  VectorIndexType: byte (0)

Footer
```

### 5.4 .doc File (Postings — Doc IDs + Freqs)

```
IndexHeader:
  codecName="Lucene104PostingsWriterDoc"
  version=0
  (no segmentID — uses plain Header, not IndexHeader)

Per-term data:
  Packed blocks (every 128 docs):
    128 doc deltas: ForUtil.encode(bitsPerValue, packed int128)
    128 freq values: ForUtil.encode(bitsPerValue, packed int128)
  VInt tail (remaining < 128 docs):
    VInt docDelta (if freq != 1: docDelta << 1, else: (docDelta << 1) | 1)
    VInt freq (only if low bit was 0)

  Skip data (inline, every 128 docs):
    VLong(docFP delta)
    VLong(posFP delta)
    VLong(payFP delta)
    VInt(docDelta to last doc in skip)
    VInt(posBlockCount) [if positions]

Footer
```

**Critical**: Lucene 104 uses `ForUtil` for packing, which differs from Diagon's BitPack128/PFOR-Delta. `ForUtil` writes:
- 1 byte: bitsPerValue (0 = all same value, then VLong constant)
- ceil(128 * bitsPerValue / 8) bytes: packed values

This is actually very similar to Diagon's BitPack128 format. The key difference is:
- Diagon uses PFOR-Delta with exceptions
- Lucene uses pure ForUtil (no exceptions, just picks wider bit width)

### 5.5 .tim File (Term Dictionary)

```
IndexHeader:
  codecName="BlockTreeTermsDict"
  version=3
  segmentID=16 bytes
  suffix=""

[term block data — per field, written in field order]
  Per block (leaf or inner):
    VInt((entryCount << 1) | isLeafBlock)

    Suffix block:
      VInt(suffixesLength)
      [suffixLength bytes: VInt per suffix length]
      [suffix bytes]

    Stats block:
      VInt(statsLength)
      [docFreq/totalTermFreq pairs — VInt/VLong encoding]

    Metadata block:
      VInt(metadataLength)
      [per-term: docStartFP(VLong) + posStartFP(VLong) + payStartFP(VLong) + skipOffset(VLong)]

Footer
```

### 5.6 .tip File (Term Index)

```
IndexHeader:
  codecName="BlockTreeTermsIndex"
  version=3
  segmentID=16 bytes
  suffix=""

Per field:
  [Lucene103 Trie data — TIP6 is already aligned to this format!]
  Note: TIP6 trie encoding matches Lucene103 TrieBuilder/TrieReader exactly.
  The header/footer framing just needs to change.

Footer
```

### 5.7 .tmd File (Term Meta — NEW)

```
IndexHeader:
  codecName="BlockTreeTermsMeta"
  version=3
  segmentID=16 bytes
  suffix=""

Per field:
  numTerms: VLong
  rootCode: VInt(length) + bytes (FST output for root block)
  sumTotalTermFreq: VLong
  sumDocFreq: VLong
  docCount: VInt
  longsSize: VInt (Lucene104 = 3: docStartFP, posStartFP, payStartFP)
  minTerm: VInt(length) + bytes
  maxTerm: VInt(length) + bytes

Postings writer meta:
  codecName="Lucene104PostingsWriterTerms"
  [per-field postings metadata]

Footer
```

---

## 6. Adaptive Detection Algorithm

### 6.1 File-Level Detection

```cpp
FormatType detectFormat(IndexInput& in) {
    int64_t savedPos = in.getFilePointer();
    int32_t magic = readBEInt(in);  // Try big-endian first
    in.seek(savedPos);

    if (magic == CodecUtil::CODEC_MAGIC) {
        // Lucene CodecUtil header — read codec name to identify exact format
        return FormatType::LUCENE_COMPAT;
    }

    // Try Diagon native magic numbers
    int32_t nativeMagic = in.readInt();  // Little-endian
    in.seek(savedPos);

    switch (nativeMagic) {
        case 0x54495036: return FormatType::DIAGON_TIP6;
        case 0x54495035: return FormatType::DIAGON_TIP5;
        // ... other Diagon magics
    }

    // Try string-based codec name (Diagon StoredFields, LiveDocs, etc.)
    std::string codecName = in.readString();
    in.seek(savedPos);
    if (codecName.find("Diagon") == 0) {
        return FormatType::DIAGON_NATIVE;
    }

    throw CorruptIndexException("Unknown file format");
}
```

### 6.2 segments_N Detection

```cpp
SegmentInfos readSegments(Directory& dir, const std::string& fileName) {
    auto in = dir.openInput(fileName, IOContext::READ);

    // Both Diagon and Lucene start with 0x3fd76c17
    int32_t magic = readBEInt(*in);
    assert(magic == 0x3fd76c17);

    // Peek at next byte: Lucene writes a String (VInt length > 0)
    // Diagon writes int32(1) = {0x00, 0x00, 0x00, 0x01} big-endian
    in->seek(4);
    uint8_t nextByte = in->readByte();
    in->seek(0);

    if (nextByte > 0 && nextByte < 128) {
        // Likely a VInt string length → Lucene format
        return readLuceneSegments(*in);
    } else {
        // Diagon native format (version int32 = 1 → first byte is 0x00)
        return readDiagonSegments(*in);
    }
}
```

---

## 7. Writer Mode Selection

### 7.1 IndexWriterConfig

```cpp
class IndexWriterConfig {
    enum class FormatMode {
        NATIVE,     // Diagon optimized (default)
        OS_COMPAT   // OpenSearch/Lucene compatible
    };

    FormatMode formatMode_ = FormatMode::NATIVE;

    IndexWriterConfig& setFormatMode(FormatMode mode);
    FormatMode getFormatMode() const;
};
```

### 7.2 Codec Selection

```cpp
// In IndexWriter constructor:
if (config.getFormatMode() == FormatMode::OS_COMPAT) {
    codec_ = Codec::forName("Lucene104OS");
} else {
    codec_ = Codec::getDefault();  // "Diagon104"
}
```

---

## 8. Testing Strategy

### 8.1 Round-Trip Tests

```
Test 1: Diagon (OS-compat) write → OpenSearch read
  - Index Reuters with Diagon in OS-compat mode
  - Copy index dir to OpenSearch data path
  - Run queries via OpenSearch REST API
  - Compare hit counts and top-K results

Test 2: OpenSearch write → Diagon read
  - Index Reuters with OpenSearch
  - Open index dir with Diagon DirectoryReader
  - Run same queries
  - Compare hit counts

Test 3: Mixed segments
  - Index some docs with Diagon native
  - Index more docs with Diagon OS-compat
  - Verify both segments readable
  - Verify merge produces correct output
```

### 8.2 Bit-Exact Comparison

```
- Index same document set with both OpenSearch and Diagon (OS-compat)
- Compare file sizes (should be very close)
- Compare postings for specific terms (exact match)
- Compare stored field retrieval (exact match)
```

---

## 9. Performance Expectations

| Mode | Index Speed | Query Speed | Index Size | Notes |
|------|-------------|-------------|------------|-------|
| **Native** | Baseline | Baseline | Baseline | All Diagon optimizations |
| **OS-Compat** | ~0.9x | ~0.95x | ~1.0x | ForUtil vs PFOR-Delta, CodecUtil overhead |

Expected regressions in OS-compat mode:
- **Indexing**: ~10% slower (ForUtil packing vs PFOR-Delta, CRC32 computation)
- **Query**: ~5% slower (ForUtil decoding vs SIMD PFOR-Delta)
- **Size**: ~same (both achieve similar compression ratios)

Native mode retains all Diagon advantages (TIP6 trie, PFOR-Delta, BitPack128).

---

## 10. Non-Goals (This Phase)

- Term vectors (.tvd/.tvx/.tvm) — rarely used, add later
- KNN vectors (.vec/.vex/.vem) — separate feature
- Compound files (.cfs/.cfe) — optimization, not required
- DEFLATE stored fields — LZ4 is sufficient for compatibility
- Multi-dimensional BKD — 1D only for now
