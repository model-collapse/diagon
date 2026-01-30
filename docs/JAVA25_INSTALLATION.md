# Java 25 Installation Summary

## ✅ Installation Complete

Java 25 has been successfully installed to enable Lucene benchmark comparisons with Diagon.

### Installation Details

**Version**: OpenJDK 25.0.2 GA (Build 25.0.2+10-69)
**Release Date**: 2026-01-20
**Location**: `/usr/lib/jvm/jdk-25.0.2`
**Download**: https://jdk.java.net/25/

### Why Java 25?

Lucene main branch requires Java 25 as minimum version:
```toml
# gradle/libs.versions.toml
minJava = "25"
```

System had Java 21, which caused build failures:
```
ERROR: java version must be 25, your version: 21
```

### Installation Steps Performed

```bash
# 1. Downloaded Java 25.0.2 GA
cd /tmp
wget https://download.java.net/java/GA/jdk25.0.2/b1e0dfa218384cb9959bdcb897162d4e/10/GPL/openjdk-25.0.2_linux-x64_bin.tar.gz

# 2. Extracted
tar -xzf openjdk-25.0.2_linux-x64_bin.tar.gz

# 3. Moved to system location
sudo mv jdk-25.0.2 /usr/lib/jvm/

# 4. Verified installation
/usr/lib/jvm/jdk-25.0.2/bin/java -version
# Output: openjdk version "25.0.2" 2026-01-20
```

### Usage

**For Lucene benchmarks**, use the helper script:

```bash
cd /home/ubuntu/opensearch_warmroom/lucene

# Source the setup script (sets JAVA_HOME and PATH)
source setup_java25.sh

# Now run Lucene benchmarks
./gradlew :lucene:benchmark:run -PtaskAlg=conf/diagon_baseline.alg -PmaxHeapSize=2G
```

**Or manually set environment:**

```bash
export JAVA_HOME=/usr/lib/jvm/jdk-25.0.2
export PATH=$JAVA_HOME/bin:$PATH
java -version  # Should show: openjdk version "25.0.2"
```

### Verification

Tested with simple benchmark algorithm:

```bash
cd /home/ubuntu/opensearch_warmroom/lucene
source setup_java25.sh

# Downloaded Reuters dataset
./gradlew :lucene:benchmark:getReuters

# Ran test benchmark
./gradlew :lucene:benchmark:run -PtaskAlg=conf/diagon_test.alg

# Results:
# ✓ BUILD SUCCESSFUL
# ✓ Indexed 2000 docs at 6,230 docs/sec
# ✓ No errors
```

### System Java Versions

The system now has multiple Java versions:

```
/usr/lib/jvm/
├── java-11-openjdk-amd64  (Java 11)
├── java-21-openjdk-amd64  (Java 21) ← System default
└── jdk-25.0.2             (Java 25) ← For Lucene benchmarks
```

**Note**: Java 21 remains the system default. Java 25 is only used when explicitly set via `JAVA_HOME` for Lucene benchmarks.

### Next Steps

With Java 25 installed, you can now proceed with Phase 2:

1. **Run Diagon benchmarks** (Java version doesn't matter - C++ native)
   ```bash
   cd /home/ubuntu/diagon/build
   ./benchmarks/LuceneCompatBenchmark --benchmark_out=results.json
   ```

2. **Run Lucene benchmarks** (requires Java 25)
   ```bash
   cd /home/ubuntu/opensearch_warmroom/lucene
   source setup_java25.sh
   ./gradlew :lucene:benchmark:run -PtaskAlg=conf/diagon_baseline.alg
   ```

3. **Compare results**
   ```bash
   python3 /home/ubuntu/diagon/scripts/compare_benchmarks.py \
       diagon_results.csv lucene_results.csv > COMPARISON.md
   ```

See `/home/ubuntu/diagon/docs/BENCHMARK_SETUP.md` for complete workflow.

### Troubleshooting

**If Java 25 is not found:**
```bash
ls -la /usr/lib/jvm/jdk-25.0.2
# Should exist with bin/, lib/, etc.
```

**If Lucene still complains about Java version:**
```bash
# Check JAVA_HOME is set
echo $JAVA_HOME
# Should output: /usr/lib/jvm/jdk-25.0.2

# Check java command uses correct version
which java
java -version
# Should show: openjdk version "25.0.2"
```

**If benchmarks fail with "NoSuchFileException":**
```bash
# Make sure Reuters dataset is downloaded
cd /home/ubuntu/opensearch_warmroom/lucene
./gradlew :lucene:benchmark:getReuters

# Verify files exist
ls -la lucene/benchmark/work/reuters-out/
# Should contain 22 .sgm files
```

## Summary

✅ Java 25.0.2 installed
✅ Lucene benchmarks verified working
✅ Reuters dataset downloaded
✅ Helper script created (`setup_java25.sh`)
✅ Test benchmark passed (6,230 docs/sec)

**Status**: Ready for Phase 2 baseline comparison
