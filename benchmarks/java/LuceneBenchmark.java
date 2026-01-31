/**
 * Fair Apache Lucene Benchmark
 *
 * This benchmark creates an identical workload to Diagon's LuceneComparisonBenchmark
 * for honest, apples-to-apples performance comparison.
 *
 * Key requirements for fairness:
 * 1. Identical dataset (10K docs, 100 words each, same vocabulary)
 * 2. Proper JVM warmup (1000+ iterations before measurement)
 * 3. Same queries (TermQuery, BooleanQuery, TopK)
 * 4. Accurate timing (System.nanoTime() with proper methodology)
 * 5. Same measurement iterations (10,000 queries)
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
import org.apache.lucene.store.ByteBuffersDirectory;
import org.apache.lucene.store.Directory;

import java.io.IOException;
import java.util.Random;

public class LuceneBenchmark {

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

    /**
     * Create index with synthetic documents
     */
    public void createIndex(int numDocs, int wordsPerDoc) throws IOException {
        directory = new ByteBuffersDirectory(); // In-memory for fair comparison
        StandardAnalyzer analyzer = new StandardAnalyzer();
        IndexWriterConfig config = new IndexWriterConfig(analyzer);

        IndexWriter writer = new IndexWriter(directory, config);
        Random random = new Random(42); // Same seed as Diagon

        for (int i = 0; i < numDocs; i++) {
            Document doc = new Document();
            String text = generateDocument(random, wordsPerDoc);
            doc.add(new TextField("content", text, Field.Store.NO));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();

        reader = DirectoryReader.open(directory);
        searcher = new IndexSearcher(reader);
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
     * Benchmark a query with proper warmup and measurement
     */
    public BenchmarkResult benchmark(String name, Query query, int topK, int warmupIterations, int measureIterations) throws IOException {
        // Warmup phase (critical for JIT compilation)
        for (int i = 0; i < warmupIterations; i++) {
            searcher.search(query, topK);
        }

        // Force GC before measurement
        System.gc();
        System.gc();

        // Measurement phase
        long startNanos = System.nanoTime();
        long checksum = 0;
        for (int i = 0; i < measureIterations; i++) {
            TopDocs results = searcher.search(query, topK);
            // Prevent dead code elimination
            checksum += results.scoreDocs.length;
        }
        long endNanos = System.nanoTime();

        // Use checksum to prevent elimination
        if (checksum < 0) {
            throw new RuntimeException("impossible");
        }

        double totalTimeMicros = (endNanos - startNanos) / 1000.0;
        double avgTimeMicros = totalTimeMicros / measureIterations;
        double qps = (measureIterations / totalTimeMicros) * 1_000_000;

        return new BenchmarkResult(name, avgTimeMicros, qps);
    }

    /**
     * Run all benchmarks
     */
    public void runBenchmarks() throws IOException {
        System.out.println("=".repeat(90));
        System.out.println("APACHE LUCENE BENCHMARK RESULTS");
        System.out.println("=".repeat(90));
        System.out.println();
        System.out.println("Configuration:");
        System.out.println("  - Documents: 10,000");
        System.out.println("  - Words per doc: 100");
        System.out.println("  - Vocabulary size: 100 words");
        System.out.println("  - Warmup iterations: 1,000");
        System.out.println("  - Measurement iterations: 10,000");
        System.out.println("  - Java version: " + System.getProperty("java.version"));
        System.out.println("  - Lucene version: 11.0.0-SNAPSHOT");
        System.out.println();
        System.out.println(String.format("%-35s %12s %12s", "Benchmark", "Latency (μs)", "QPS (M)"));
        System.out.println("-".repeat(90));

        int warmup = 1000;
        int iterations = 10000;

        // TermQuery (common term)
        Query q1 = new TermQuery(new Term("content", "the"));
        BenchmarkResult r1 = benchmark("TermQuery (common: 'the')", q1, 10, warmup, iterations);
        System.out.println(r1.format());

        // TermQuery (rare term)
        Query q2 = new TermQuery(new Term("content", "because"));
        BenchmarkResult r2 = benchmark("TermQuery (rare: 'because')", q2, 10, warmup, iterations);
        System.out.println(r2.format());

        // BooleanQuery AND
        BooleanQuery.Builder b1 = new BooleanQuery.Builder();
        b1.add(new TermQuery(new Term("content", "the")), BooleanClause.Occur.MUST);
        b1.add(new TermQuery(new Term("content", "and")), BooleanClause.Occur.MUST);
        BenchmarkResult r3 = benchmark("BooleanQuery (AND)", b1.build(), 10, warmup, iterations);
        System.out.println(r3.format());

        // BooleanQuery OR
        BooleanQuery.Builder b2 = new BooleanQuery.Builder();
        b2.add(new TermQuery(new Term("content", "people")), BooleanClause.Occur.SHOULD);
        b2.add(new TermQuery(new Term("content", "time")), BooleanClause.Occur.SHOULD);
        BenchmarkResult r4 = benchmark("BooleanQuery (OR)", b2.build(), 10, warmup, iterations);
        System.out.println(r4.format());

        // TopK variations
        Query q5 = new TermQuery(new Term("content", "the"));
        BenchmarkResult r5 = benchmark("TopK (k=10)", q5, 10, warmup, iterations);
        System.out.println(r5.format());

        BenchmarkResult r6 = benchmark("TopK (k=50)", q5, 50, warmup, iterations);
        System.out.println(r6.format());

        BenchmarkResult r7 = benchmark("TopK (k=100)", q5, 100, warmup, iterations);
        System.out.println(r7.format());

        BenchmarkResult r8 = benchmark("TopK (k=1000)", q5, 1000, warmup, iterations);
        System.out.println(r8.format());

        System.out.println("-".repeat(90));
        System.out.println();
        System.out.println("Note: Results include proper JVM warmup (1000 iterations) before measurement.");
        System.out.println("All queries measured over 10,000 iterations for statistical significance.");
        System.out.println();
    }

    public void close() throws IOException {
        reader.close();
        directory.close();
    }

    static class BenchmarkResult {
        String name;
        double latencyMicros;
        double qps;

        BenchmarkResult(String name, double latencyMicros, double qps) {
            this.name = name;
            this.latencyMicros = latencyMicros;
            this.qps = qps;
        }

        String format() {
            return String.format("%-35s %12.3f %12.2f", name, latencyMicros, qps / 1_000_000);
        }
    }

    public static void main(String[] args) throws IOException {
        System.out.println("Building index...");
        LuceneBenchmark benchmark = new LuceneBenchmark();
        benchmark.createIndex(10000, 100);
        System.out.println("Index created. Starting benchmarks...");
        System.out.println();

        benchmark.runBenchmarks();
        benchmark.close();

        System.out.println("Benchmark complete!");
    }
}
