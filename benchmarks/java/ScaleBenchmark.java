/**
 * Scale Benchmark: 10M Document Performance Test
 *
 * Tests Lucene performance at realistic scale (10 million documents)
 * to validate whether small benchmark results extrapolate.
 *
 * Expected outcomes:
 * - Lucene's optimizations (WAND, MaxScore, skip lists) become more effective
 * - JVM overhead becomes proportionally smaller
 * - More realistic of production workloads
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

public class ScaleBenchmark {

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

    public void createIndex(int numDocs, int wordsPerDoc) throws IOException {
        indexPath = Paths.get("/tmp", "lucene_scale_10m");

        // Delete if exists
        if (Files.exists(indexPath)) {
            System.out.println("Removing existing index...");
            deleteDirectory(indexPath);
        }

        Files.createDirectories(indexPath);

        directory = MMapDirectory.open(indexPath);
        StandardAnalyzer analyzer = new StandardAnalyzer();
        IndexWriterConfig config = new IndexWriterConfig(analyzer);
        config.setRAMBufferSizeMB(512.0);  // Use 512MB for faster indexing
        config.setMaxBufferedDocs(IndexWriterConfig.DISABLE_AUTO_FLUSH);

        System.out.println("Creating index with " + numDocs + " documents...");
        System.out.println("Estimated index size: ~" + (numDocs * wordsPerDoc * 5 / 1024 / 1024) + " MB");
        System.out.println("Progress:");

        IndexWriter writer = new IndexWriter(directory, config);
        Random random = new Random(42);

        long startTime = System.currentTimeMillis();
        int reportInterval = numDocs / 20;  // Report every 5%

        for (int i = 0; i < numDocs; i++) {
            Document doc = new Document();
            String text = generateDocument(random, wordsPerDoc);
            doc.add(new TextField("content", text, Field.Store.NO));
            writer.addDocument(doc);

            if ((i + 1) % reportInterval == 0 || i == numDocs - 1) {
                int percent = (int)((i + 1) * 100.0 / numDocs);
                long elapsed = System.currentTimeMillis() - startTime;
                long docsPerSec = (i + 1) * 1000L / Math.max(elapsed, 1);
                System.out.printf("  [%3d%%] %,d docs (%.1fK docs/sec)%n",
                    percent, i + 1, docsPerSec / 1000.0);
            }
        }

        System.out.println("\nForce merging to single segment...");
        writer.forceMerge(1);
        writer.commit();

        long indexTime = System.currentTimeMillis() - startTime;
        System.out.printf("Index created in %.1f seconds (%.1fK docs/sec)%n",
            indexTime / 1000.0, numDocs / (indexTime / 1000.0) / 1000.0);

        writer.close();

        // Get index size
        long indexSize = getDirectorySize(indexPath);
        System.out.printf("Index size: %.1f MB%n", indexSize / 1024.0 / 1024.0);

        System.out.println("\nOpening reader...");
        reader = DirectoryReader.open(directory);
        searcher = new IndexSearcher(reader);

        System.out.println("Index ready:");
        System.out.println("  - Segments: " + reader.leaves().size());
        System.out.println("  - Documents: " + reader.maxDoc());
        System.out.println("  - Directory: " + indexPath);
    }

    private String generateDocument(Random random, int numWords) {
        StringBuilder sb = new StringBuilder(numWords * 6);
        for (int i = 0; i < numWords; i++) {
            if (i > 0) sb.append(" ");
            sb.append(VOCABULARY[random.nextInt(VOCABULARY.length)]);
        }
        return sb.toString();
    }

    private long getDirectorySize(Path path) throws IOException {
        return Files.walk(path)
            .filter(p -> p.toFile().isFile())
            .mapToLong(p -> p.toFile().length())
            .sum();
    }

    public void benchmark(String name, Query query, int topK, int warmupIterations, int measureRounds, int iterationsPerRound) throws IOException {
        List<Double> roundResults = new ArrayList<>();

        System.out.println("\n" + name + ":");
        System.out.print("  Warmup (" + warmupIterations + " iterations)...");
        System.out.flush();

        for (int i = 0; i < warmupIterations; i++) {
            searcher.search(query, topK);
        }

        System.out.println(" done");

        // GC between warmup and measurement
        System.gc();
        Thread.yield();

        System.out.print("  Measuring (" + measureRounds + " rounds × " + iterationsPerRound + " iterations)...");
        System.out.flush();

        for (int round = 0; round < measureRounds; round++) {
            long startNanos = System.nanoTime();
            long checksum = 0;

            for (int i = 0; i < iterationsPerRound; i++) {
                TopDocs results = searcher.search(query, topK);
                checksum += results.scoreDocs.length;
            }

            long endNanos = System.nanoTime();

            if (checksum < 0) throw new RuntimeException("impossible");

            double totalTimeMicros = (endNanos - startNanos) / 1000.0;
            double avgTimeMicros = totalTimeMicros / iterationsPerRound;
            roundResults.add(avgTimeMicros);
        }

        System.out.println(" done");

        // Statistics
        double sum = 0, min = Double.MAX_VALUE, max = Double.MIN_VALUE;
        for (double val : roundResults) {
            sum += val;
            min = Math.min(min, val);
            max = Math.max(max, val);
        }
        double mean = sum / roundResults.size();

        double sumSquaredDiff = 0;
        for (double val : roundResults) {
            sumSquaredDiff += Math.pow(val - mean, 2);
        }
        double stddev = Math.sqrt(sumSquaredDiff / roundResults.size());
        double cv = (stddev / mean) * 100.0;
        double qps = 1_000_000.0 / mean;

        System.out.printf("  Latency: %.3f μs (±%.1f%%, min=%.3f, max=%.3f)%n",
            mean, cv, min, max);
        System.out.printf("  Throughput: %.2f M QPS%n", qps / 1_000_000.0);
    }

    public void runBenchmarks() throws IOException {
        System.out.println("\n" + "=".repeat(90));
        System.out.println("LUCENE 10M DOCUMENT SCALE BENCHMARK");
        System.out.println("=".repeat(90));
        System.out.println();

        int warmup = 1000;
        int rounds = 5;
        int iterations = 10000;

        // TermQuery (common term - high frequency)
        Query q1 = new TermQuery(new Term("content", "the"));
        benchmark("TermQuery (common: 'the')", q1, 10, warmup, rounds, iterations);

        // TermQuery (rare term - low frequency)
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
        benchmark("TopK (k=100)", q5, 100, warmup, rounds, iterations);
        benchmark("TopK (k=1000)", q5, 1000, warmup, rounds, iterations);

        System.out.println();
        System.out.println("=".repeat(90));
        System.out.println("Note: 10M documents, MMapDirectory, 1000 warmup iterations");
        System.out.println("=".repeat(90));
    }

    public void close() throws IOException {
        if (reader != null) reader.close();
        if (directory != null) directory.close();
        // Keep index for analysis - don't delete
    }

    private void deleteDirectory(Path path) throws IOException {
        if (Files.isDirectory(path)) {
            Files.list(path).forEach(child -> {
                try {
                    deleteDirectory(child);
                } catch (IOException e) {
                    // Ignore
                }
            });
        }
        Files.deleteIfExists(path);
    }

    public static void main(String[] args) throws IOException {
        int numDocs = 10_000_000;  // 10 million
        int wordsPerDoc = 100;

        // Allow override via command line
        if (args.length > 0) {
            numDocs = Integer.parseInt(args[0]);
        }
        if (args.length > 1) {
            wordsPerDoc = Integer.parseInt(args[1]);
        }

        System.out.println("Lucene Scale Benchmark");
        System.out.println("Documents: " + String.format("%,d", numDocs));
        System.out.println("Words/doc: " + wordsPerDoc);
        System.out.println();

        ScaleBenchmark benchmark = new ScaleBenchmark();

        long startTotal = System.currentTimeMillis();
        benchmark.createIndex(numDocs, wordsPerDoc);
        benchmark.runBenchmarks();
        long totalTime = System.currentTimeMillis() - startTotal;

        System.out.printf("%nTotal benchmark time: %.1f minutes%n", totalTime / 60000.0);

        benchmark.close();
    }
}
