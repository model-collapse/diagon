/**
 * Fair Apple-to-Apple Lucene Benchmark
 *
 * Aligned with standard Lucene benchmarking practices:
 * - Uses MMapDirectory (Lucene's production default, not ByteBuffers)
 * - Persists index to disk for realistic comparison
 * - Uses FSDirectory for consistent with typical deployments
 * - Extended warmup (10,000 iterations) for proper JIT compilation
 * - Multiple rounds to detect variance
 *
 * Methodology based on:
 * - Lucene benchmark module best practices
 * - JMH (Java Microbenchmark Harness) methodology
 * - Published Lucene performance research
 */

import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.Document;
import org.apache.lucene.document.Field;
import org.apache.lucene.document.TextField;
import org.apache.lucene.index.DirectoryReader;
import org.apache.lucene.index.IndexWriter;
import org.apache.lucene.index.IndexWriterConfig;
import org.apache.lucene.index.Term;
import org.apache.lucene.search.BooleanClause;
import org.apache.lucene.search.BooleanQuery;
import org.apache.lucene.search.IndexSearcher;
import org.apache.lucene.search.Query;
import org.apache.lucene.search.TermQuery;
import org.apache.lucene.search.TopDocs;
import org.apache.lucene.store.Directory;
import org.apache.lucene.store.MMapDirectory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

public class FairLuceneBenchmark {

    // Same vocabulary as Diagon benchmark
    private static final String[] VOCABULARY = {
        "the", "be", "to", "of", "and", "a", "in", "that", "have", "i",
        "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
        "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
        "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
        "so", "up", "out", "if", "about", "who", "get", "which", "go", "me",
        "when", "make", "can", "like", "time", "no", "just", "him", "know", "take",
        "people", "into", "year", "your", "good", "some", "could", "them", "see", "other",
        "than", "then", "now", "look", "only", "come", "its", "over", "think", "also",
        "back", "after", "use", "two", "how", "our", "work", "first", "well", "way",
        "even", "new", "want", "because", "any", "these", "give", "day", "most", "us"
    };

    private Directory directory;
    private DirectoryReader reader;
    private IndexSearcher searcher;
    private Path indexPath;

    /**
     * Create index with MMapDirectory (production default)
     */
    public void createIndex(int numDocs, int wordsPerDoc) throws IOException {
        // Use persistent MMapDirectory (Lucene production default)
        indexPath = Paths.get(System.getProperty("java.io.tmpdir"), "lucene_fair_benchmark_" + System.currentTimeMillis());
        Files.createDirectories(indexPath);

        directory = MMapDirectory.open(indexPath);
        StandardAnalyzer analyzer = new StandardAnalyzer();
        IndexWriterConfig config = new IndexWriterConfig(analyzer);

        // Production-like settings
        config.setRAMBufferSizeMB(256.0);  // Lucene default is 16MB, but allow more for performance
        config.setMaxBufferedDocs(IndexWriterConfig.DISABLE_AUTO_FLUSH);  // Use RAM buffer only

        IndexWriter writer = new IndexWriter(directory, config);
        Random random = new Random(42); // Same seed as Diagon

        for (int i = 0; i < numDocs; i++) {
            Document doc = new Document();
            String text = generateDocument(random, wordsPerDoc);
            doc.add(new TextField("content", text, Field.Store.NO));
            writer.addDocument(doc);
        }

        writer.forceMerge(1);  // Merge to single segment for consistent comparison
        writer.commit();
        writer.close();

        reader = DirectoryReader.open(directory);
        searcher = new IndexSearcher(reader);

        System.out.println("Index created:");
        System.out.println("  - Segments: " + reader.leaves().size());
        System.out.println("  - Documents: " + reader.maxDoc());
        System.out.println("  - Directory: MMapDirectory at " + indexPath);
    }

    /**
     * Generate synthetic document with random words
     */
    private String generateDocument(Random random, int numWords) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < numWords; i++) {
            if (i > 0) sb.append(" ");
            sb.append(VOCABULARY[random.nextInt(VOCABULARY.length)]);
        }
        return sb.toString();
    }

    /**
     * Benchmark with multiple rounds for variance detection
     */
    public void benchmark(String name, Query query, int topK, int warmupIterations, int measureRounds, int iterationsPerRound) throws IOException {
        List<Double> roundResults = new ArrayList<>();

        // Extended warmup phase (critical for JIT compilation)
        System.out.println("\n" + name + ":");
        System.out.print("  Warmup (" + warmupIterations + " iterations)...");
        for (int i = 0; i < warmupIterations; i++) {
            searcher.search(query, topK);
        }
        System.out.println(" done");

        // Force GC between warmup and measurement
        System.gc();
        Thread.yield();
        try {
            Thread.sleep(100);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }

        // Multiple measurement rounds
        System.out.print("  Measuring (" + measureRounds + " rounds × " + iterationsPerRound + " iterations)...");
        for (int round = 0; round < measureRounds; round++) {
            long startNanos = System.nanoTime();
            long checksum = 0;

            for (int i = 0; i < iterationsPerRound; i++) {
                TopDocs results = searcher.search(query, topK);
                checksum += results.scoreDocs.length;
            }

            long endNanos = System.nanoTime();

            // Prevent dead code elimination
            if (checksum < 0) {
                throw new RuntimeException("impossible");
            }

            double totalTimeMicros = (endNanos - startNanos) / 1000.0;
            double avgTimeMicros = totalTimeMicros / iterationsPerRound;
            roundResults.add(avgTimeMicros);
        }
        System.out.println(" done");

        // Calculate statistics
        double sum = 0;
        double min = Double.MAX_VALUE;
        double max = Double.MIN_VALUE;
        for (double val : roundResults) {
            sum += val;
            min = Math.min(min, val);
            max = Math.max(max, val);
        }
        double mean = sum / roundResults.size();

        // Calculate standard deviation
        double sumSquaredDiff = 0;
        for (double val : roundResults) {
            double diff = val - mean;
            sumSquaredDiff += diff * diff;
        }
        double stddev = Math.sqrt(sumSquaredDiff / roundResults.size());
        double cv = (stddev / mean) * 100.0;

        double qps = (1_000_000.0 / mean);

        // Print results
        System.out.println(String.format("  Latency: %.3f μs (±%.1f%%, min=%.3f, max=%.3f)",
            mean, cv, min, max));
        System.out.println(String.format("  Throughput: %.2f M QPS", qps / 1_000_000.0));
    }

    /**
     * Run all benchmarks
     */
    public void runBenchmarks() throws IOException {
        System.out.println("=".repeat(90));
        System.out.println("FAIR APACHE LUCENE BENCHMARK");
        System.out.println("=".repeat(90));
        System.out.println();
        System.out.println("Configuration:");
        System.out.println("  - Documents: 10,000");
        System.out.println("  - Words per doc: 100");
        System.out.println("  - Vocabulary size: 100 words");
        System.out.println("  - Directory: MMapDirectory (production default)");
        System.out.println("  - Segments: Force merged to 1");
        System.out.println("  - Warmup iterations: 10,000 (extended for JIT)");
        System.out.println("  - Measurement: 5 rounds × 10,000 iterations");
        System.out.println("  - Java version: " + System.getProperty("java.version"));
        System.out.println("  - JVM: " + System.getProperty("java.vm.name"));
        System.out.println("  - Lucene version: 11.0.0-SNAPSHOT");
        System.out.println();

        int warmup = 10000;  // Extended warmup
        int rounds = 5;
        int iterations = 10000;

        // TermQuery (common term)
        Query q1 = new TermQuery(new Term("content", "the"));
        benchmark("TermQuery (common: 'the')", q1, 10, warmup, rounds, iterations);

        // TermQuery (rare term)
        Query q2 = new TermQuery(new Term("content", "because"));
        benchmark("TermQuery (rare: 'because')", q2, 10, warmup, rounds, iterations);

        // BooleanQuery AND
        BooleanQuery.Builder b1 = new BooleanQuery.Builder();
        b1.add(new TermQuery(new Term("content", "the")), BooleanClause.Occur.MUST);
        b1.add(new TermQuery(new Term("content", "and")), BooleanClause.Occur.MUST);
        benchmark("BooleanQuery (AND)", b1.build(), 10, warmup, rounds, iterations);

        // BooleanQuery OR
        BooleanQuery.Builder b2 = new BooleanQuery.Builder();
        b2.add(new TermQuery(new Term("content", "people")), BooleanClause.Occur.SHOULD);
        b2.add(new TermQuery(new Term("content", "time")), BooleanClause.Occur.SHOULD);
        benchmark("BooleanQuery (OR)", b2.build(), 10, warmup, rounds, iterations);

        // TopK variations
        Query q5 = new TermQuery(new Term("content", "the"));
        benchmark("TopK (k=10)", q5, 10, warmup, rounds, iterations);
        benchmark("TopK (k=50)", q5, 50, warmup, rounds, iterations);
        benchmark("TopK (k=100)", q5, 100, warmup, rounds, iterations);
        benchmark("TopK (k=1000)", q5, 1000, warmup, rounds, iterations);

        System.out.println();
        System.out.println("=".repeat(90));
        System.out.println("Note: Results from 5 measurement rounds with extended warmup (10K iterations).");
        System.out.println("Using MMapDirectory (Lucene's production default) for realistic performance.");
        System.out.println("=".repeat(90));
    }

    public void close() throws IOException {
        if (reader != null) reader.close();
        if (directory != null) directory.close();

        // Clean up temporary index
        if (indexPath != null && Files.exists(indexPath)) {
            deleteDirectory(indexPath);
        }
    }

    private void deleteDirectory(Path path) throws IOException {
        if (Files.isDirectory(path)) {
            Files.list(path).forEach(child -> {
                try {
                    deleteDirectory(child);
                } catch (IOException e) {
                    System.err.println("Failed to delete: " + child);
                }
            });
        }
        Files.deleteIfExists(path);
    }

    public static void main(String[] args) throws IOException {
        System.out.println("Building index with MMapDirectory...");
        FairLuceneBenchmark benchmark = new FairLuceneBenchmark();
        benchmark.createIndex(10000, 100);
        System.out.println();

        benchmark.runBenchmarks();
        benchmark.close();

        System.out.println("\nBenchmark complete!");
    }
}
