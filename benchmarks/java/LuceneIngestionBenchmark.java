/**
 * Lucene Ingestion Speed Benchmark
 *
 * Measures Lucene indexing throughput for direct comparison with Diagon.
 *
 * Test cases:
 * 1. Reuters-21578 single-field (body text, StandardAnalyzer)
 * 2. Multi-field (25 fields, 20 words each — matches Issue #6 CGO workload)
 * 3. Single-field synthetic (1 body, 50 words — matches IndexingBenchmark)
 *
 * Methodology:
 * - FSDirectory (disk-backed, same as Diagon)
 * - StandardAnalyzer (same tokenization as Diagon's StandardTokenizer)
 * - Multiple runs with warmup for stable numbers
 * - Reports docs/sec, time, and index size
 */

import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.Document;
import org.apache.lucene.document.Field;
import org.apache.lucene.document.FieldType;
import org.apache.lucene.document.TextField;
import org.apache.lucene.index.IndexWriter;
import org.apache.lucene.index.IndexWriterConfig;
import org.apache.lucene.store.Directory;
import org.apache.lucene.store.FSDirectory;

import org.apache.lucene.index.IndexOptions;

import java.io.IOException;
import java.nio.file.*;
import java.util.*;
import java.util.stream.Collectors;

public class LuceneIngestionBenchmark {

    // FieldType matching Diagon's createIndexedFieldType() for synthetic benchmarks:
    // DOCS_AND_FREQS_AND_POSITIONS, stored=true, tokenized=true
    private static final FieldType STORED_INDEXED_TYPE = new FieldType();
    static {
        STORED_INDEXED_TYPE.setIndexOptions(IndexOptions.DOCS_AND_FREQS_AND_POSITIONS);
        STORED_INDEXED_TYPE.setStored(true);
        STORED_INDEXED_TYPE.setTokenized(true);
        STORED_INDEXED_TYPE.freeze();
    }

    private static final String REUTERS_PATH =
        "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";

    // Same vocabulary as Diagon's IndexingBenchmark
    private static final String[] WORDS = {
        "the", "quick", "brown", "fox", "jumps", "over",
        "lazy", "dog", "search", "engine", "index", "document",
        "query", "result", "score", "lucene", "elasticsearch", "solr",
        "database", "algorithm", "data", "performance", "benchmark", "optimization",
        "memory", "disk", "cache", "distributed", "scalable", "fast",
        "efficient", "robust", "reliable"
    };

    private static String generateText(Random rng, int numWords) {
        StringBuilder sb = new StringBuilder(numWords * 8);
        for (int i = 0; i < numWords; i++) {
            if (i > 0) sb.append(' ');
            sb.append(WORDS[rng.nextInt(WORDS.length)]);
        }
        return sb.toString();
    }

    private static String loadReutersDocument(Path filepath) throws IOException {
        List<String> lines = Files.readAllLines(filepath);
        StringBuilder text = new StringBuilder();
        if (lines.size() > 2) text.append(lines.get(2)).append(" ");
        if (lines.size() > 4) {
            for (int i = 4; i < lines.size(); i++) {
                text.append(lines.get(i)).append(" ");
            }
        }
        return text.toString();
    }

    private static void deleteDirectory(Path dir) throws IOException {
        if (Files.exists(dir)) {
            Files.walk(dir)
                .sorted(Comparator.reverseOrder())
                .forEach(p -> { try { Files.delete(p); } catch (IOException e) {} });
        }
    }

    // ==================== Benchmark 1: Reuters single-field ====================

    static void benchmarkReuters() throws IOException {
        System.out.println("========================================");
        System.out.println("Benchmark 1: Reuters-21578 (single body field)");
        System.out.println("========================================");

        // Load all Reuters documents into memory first (exclude I/O from timing)
        List<String> documents = new ArrayList<>();
        List<Path> files = Files.walk(Paths.get(REUTERS_PATH))
            .filter(p -> p.toString().endsWith(".txt"))
            .sorted()
            .collect(Collectors.toList());

        for (Path filepath : files) {
            try {
                documents.add(loadReutersDocument(filepath));
            } catch (Exception e) {
                // skip
            }
        }
        System.out.println("Loaded " + documents.size() + " documents into memory");

        // Warmup run
        Path warmupPath = Paths.get("/tmp/lucene_ingest_warmup");
        deleteDirectory(warmupPath);
        Files.createDirectories(warmupPath);
        {
            Directory dir = FSDirectory.open(warmupPath);
            IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
            config.setRAMBufferSizeMB(128.0);
            IndexWriter writer = new IndexWriter(dir, config);
            for (String text : documents) {
                Document doc = new Document();
                doc.add(new TextField("body", text, Field.Store.NO));
                writer.addDocument(doc);
            }
            writer.commit();
            writer.close();
            dir.close();
        }
        deleteDirectory(warmupPath);

        // Timed runs
        int runs = 3;
        double[] throughputs = new double[runs];
        long[] timesMs = new long[runs];
        long indexSizeBytes = 0;

        for (int r = 0; r < runs; r++) {
            Path indexPath = Paths.get("/tmp/lucene_ingest_reuters_" + r);
            deleteDirectory(indexPath);
            Files.createDirectories(indexPath);

            Directory dir = FSDirectory.open(indexPath);
            IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
            config.setRAMBufferSizeMB(128.0);
            config.setMaxBufferedDocs(IndexWriterConfig.DISABLE_AUTO_FLUSH);

            long start = System.nanoTime();

            IndexWriter writer = new IndexWriter(dir, config);
            for (String text : documents) {
                Document doc = new Document();
                doc.add(new TextField("body", text, Field.Store.NO));
                writer.addDocument(doc);
            }
            writer.commit();
            writer.close();

            long elapsed = System.nanoTime() - start;
            timesMs[r] = elapsed / 1_000_000;
            throughputs[r] = documents.size() * 1_000_000_000.0 / elapsed;

            // Measure index size on last run
            if (r == runs - 1) {
                indexSizeBytes = Files.walk(indexPath)
                    .filter(Files::isRegularFile)
                    .mapToLong(p -> { try { return Files.size(p); } catch (Exception e) { return 0; } })
                    .sum();
            }

            dir.close();
            deleteDirectory(indexPath);
        }

        // Report best of 3
        double bestThroughput = 0;
        long bestTime = Long.MAX_VALUE;
        for (int r = 0; r < runs; r++) {
            if (throughputs[r] > bestThroughput) {
                bestThroughput = throughputs[r];
                bestTime = timesMs[r];
            }
        }

        System.out.printf("  Documents: %d%n", documents.size());
        System.out.printf("  Best time: %.3f seconds%n", bestTime / 1000.0);
        System.out.printf("  Best throughput: %.0f docs/sec%n", bestThroughput);
        System.out.printf("  Index size: %.1f MB%n", indexSizeBytes / (1024.0 * 1024.0));
        System.out.printf("  Bytes/doc: %d%n", indexSizeBytes / documents.size());
        for (int r = 0; r < runs; r++) {
            System.out.printf("  Run %d: %.0f docs/sec (%.3fs)%n", r + 1, throughputs[r], timesMs[r] / 1000.0);
        }
        System.out.println();
    }

    // ==================== Benchmark 2: Multi-field (25 fields) ====================

    static void benchmarkMultiField() throws IOException {
        System.out.println("========================================");
        System.out.println("Benchmark 2: Multi-field (25 fields x 20 words, stored=true)");
        System.out.println("========================================");

        int numDocs = 5000;
        int numFields = 25;
        int wordsPerField = 20;
        Random rng = new Random(42);

        // Pre-generate field names
        String[] fieldNames = new String[numFields];
        for (int f = 0; f < numFields; f++) {
            fieldNames[f] = "field_" + f;
        }

        // Warmup
        Path warmupPath = Paths.get("/tmp/lucene_ingest_mf_warmup");
        deleteDirectory(warmupPath);
        Files.createDirectories(warmupPath);
        {
            Random warmupRng = new Random(42);
            Directory dir = FSDirectory.open(warmupPath);
            IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
            config.setRAMBufferSizeMB(128.0);
            IndexWriter writer = new IndexWriter(dir, config);
            for (int i = 0; i < numDocs; i++) {
                Document doc = new Document();
                for (int f = 0; f < numFields; f++) {
                    doc.add(new Field(fieldNames[f], generateText(warmupRng, wordsPerField), STORED_INDEXED_TYPE));
                }
                writer.addDocument(doc);
            }
            writer.commit();
            writer.close();
            dir.close();
        }
        deleteDirectory(warmupPath);

        // Timed runs
        int runs = 3;
        double[] throughputs = new double[runs];
        long[] timesMs = new long[runs];
        long indexSizeBytes = 0;

        for (int r = 0; r < runs; r++) {
            Random runRng = new Random(42);  // Same seed each run for consistency
            Path indexPath = Paths.get("/tmp/lucene_ingest_mf_" + r);
            deleteDirectory(indexPath);
            Files.createDirectories(indexPath);

            Directory dir = FSDirectory.open(indexPath);
            IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
            config.setRAMBufferSizeMB(128.0);
            config.setMaxBufferedDocs(IndexWriterConfig.DISABLE_AUTO_FLUSH);

            long start = System.nanoTime();

            IndexWriter writer = new IndexWriter(dir, config);
            for (int i = 0; i < numDocs; i++) {
                Document doc = new Document();
                for (int f = 0; f < numFields; f++) {
                    doc.add(new Field(fieldNames[f], generateText(runRng, wordsPerField), STORED_INDEXED_TYPE));
                }
                writer.addDocument(doc);
            }
            writer.commit();
            writer.close();

            long elapsed = System.nanoTime() - start;
            timesMs[r] = elapsed / 1_000_000;
            throughputs[r] = numDocs * 1_000_000_000.0 / elapsed;

            if (r == runs - 1) {
                indexSizeBytes = Files.walk(indexPath)
                    .filter(Files::isRegularFile)
                    .mapToLong(p -> { try { return Files.size(p); } catch (Exception e) { return 0; } })
                    .sum();
            }

            dir.close();
            deleteDirectory(indexPath);
        }

        double bestThroughput = 0;
        long bestTime = Long.MAX_VALUE;
        for (int r = 0; r < runs; r++) {
            if (throughputs[r] > bestThroughput) {
                bestThroughput = throughputs[r];
                bestTime = timesMs[r];
            }
        }

        System.out.printf("  Documents: %d (%d fields x %d words each)%n", numDocs, numFields, wordsPerField);
        System.out.printf("  Best time: %.3f seconds%n", bestTime / 1000.0);
        System.out.printf("  Best throughput: %.0f docs/sec%n", bestThroughput);
        System.out.printf("  Index size: %.1f MB%n", indexSizeBytes / (1024.0 * 1024.0));
        for (int r = 0; r < runs; r++) {
            System.out.printf("  Run %d: %.0f docs/sec (%.3fs)%n", r + 1, throughputs[r], timesMs[r] / 1000.0);
        }
        System.out.println();
    }

    // ==================== Benchmark 3: Single-field synthetic ====================

    static void benchmarkSingleField() throws IOException {
        System.out.println("========================================");
        System.out.println("Benchmark 3: Synthetic single-field (1 body x 50 words, stored=true)");
        System.out.println("========================================");

        int numDocs = 5000;
        int wordsPerDoc = 50;

        // Warmup
        Path warmupPath = Paths.get("/tmp/lucene_ingest_sf_warmup");
        deleteDirectory(warmupPath);
        Files.createDirectories(warmupPath);
        {
            Random warmupRng = new Random(42);
            Directory dir = FSDirectory.open(warmupPath);
            IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
            config.setRAMBufferSizeMB(128.0);
            IndexWriter writer = new IndexWriter(dir, config);
            for (int i = 0; i < numDocs; i++) {
                Document doc = new Document();
                doc.add(new Field("body", generateText(warmupRng, wordsPerDoc), STORED_INDEXED_TYPE));
                writer.addDocument(doc);
            }
            writer.commit();
            writer.close();
            dir.close();
        }
        deleteDirectory(warmupPath);

        // Timed runs
        int runs = 3;
        double[] throughputs = new double[runs];
        long[] timesMs = new long[runs];

        for (int r = 0; r < runs; r++) {
            Random runRng = new Random(42);
            Path indexPath = Paths.get("/tmp/lucene_ingest_sf_" + r);
            deleteDirectory(indexPath);
            Files.createDirectories(indexPath);

            Directory dir = FSDirectory.open(indexPath);
            IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
            config.setRAMBufferSizeMB(128.0);
            config.setMaxBufferedDocs(IndexWriterConfig.DISABLE_AUTO_FLUSH);

            long start = System.nanoTime();

            IndexWriter writer = new IndexWriter(dir, config);
            for (int i = 0; i < numDocs; i++) {
                Document doc = new Document();
                doc.add(new Field("body", generateText(runRng, wordsPerDoc), STORED_INDEXED_TYPE));
                writer.addDocument(doc);
            }
            writer.commit();
            writer.close();

            long elapsed = System.nanoTime() - start;
            timesMs[r] = elapsed / 1_000_000;
            throughputs[r] = numDocs * 1_000_000_000.0 / elapsed;

            dir.close();
            deleteDirectory(indexPath);
        }

        double bestThroughput = 0;
        long bestTime = Long.MAX_VALUE;
        for (int r = 0; r < runs; r++) {
            if (throughputs[r] > bestThroughput) {
                bestThroughput = throughputs[r];
                bestTime = timesMs[r];
            }
        }

        System.out.printf("  Documents: %d (1 field x %d words)%n", numDocs, wordsPerDoc);
        System.out.printf("  Best time: %.3f seconds%n", bestTime / 1000.0);
        System.out.printf("  Best throughput: %.0f docs/sec%n", bestThroughput);
        for (int r = 0; r < runs; r++) {
            System.out.printf("  Run %d: %.0f docs/sec (%.3fs)%n", r + 1, throughputs[r], timesMs[r] / 1000.0);
        }
        System.out.println();
    }

    // ==================== Main ====================

    public static void main(String[] args) throws IOException {
        System.out.println("=========================================");
        System.out.println("Lucene Ingestion Speed Benchmark");
        System.out.println("=========================================");
        System.out.println("Lucene version: " + org.apache.lucene.util.Version.LATEST);
        System.out.println("JVM: " + System.getProperty("java.version"));
        System.out.println("Analyzer: StandardAnalyzer");
        System.out.println("Directory: FSDirectory (disk-backed)");
        System.out.println("RAM buffer: 128 MB");
        System.out.println();

        benchmarkReuters();
        benchmarkMultiField();
        benchmarkSingleField();

        System.out.println("=========================================");
        System.out.println("Done");
        System.out.println("=========================================");
    }
}
