// WAND Performance Comparison: Diagon vs Apache Lucene
// Generates same synthetic data and runs identical queries

import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.search.similarities.BM25Similarity;
import org.apache.lucene.store.ByteBuffersDirectory;
import org.apache.lucene.store.Directory;

import java.io.IOException;
import java.util.*;

public class WANDComparison {

    // Same vocabulary as Diagon benchmark
    private static final String[] COMMON_WORDS = {
        "the", "be", "to", "of", "and", "a", "in", "that", "have", "I"
    };

    private static final String[] MEDIUM_WORDS = {
        "search", "engine", "index", "document", "query", "result", "score",
        "lucene", "database", "algorithm", "data", "fast", "performance"
    };

    private static final String[] RARE_WORDS = {
        "optimization", "distributed", "benchmark", "elasticsearch",
        "apache", "inverted", "posting", "tokenizer", "analyzer"
    };

    /**
     * Generate random text with same distribution as Diagon
     */
    private static String generateRandomText(int numWords, Random rng) {
        StringBuilder sb = new StringBuilder();

        for (int i = 0; i < numWords; i++) {
            if (i > 0) sb.append(" ");

            int freq = rng.nextInt(100);
            if (freq < 60) {
                // 60% common words
                sb.append(COMMON_WORDS[rng.nextInt(COMMON_WORDS.length)]);
            } else if (freq < 90) {
                // 30% medium frequency
                sb.append(MEDIUM_WORDS[rng.nextInt(MEDIUM_WORDS.length)]);
            } else {
                // 10% rare words
                sb.append(RARE_WORDS[rng.nextInt(RARE_WORDS.length)]);
            }
        }

        return sb.toString();
    }

    /**
     * Create test index with specified number of documents
     */
    private static Directory createTestIndex(int numDocs) throws IOException {
        Directory dir = new ByteBuffersDirectory();

        IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
        config.setRAMBufferSizeMB(128.0);
        config.setSimilarity(new BM25Similarity()); // Same as Diagon

        IndexWriter writer = new IndexWriter(dir, config);

        Random rng = new Random(42); // Same seed as Diagon

        System.out.println("Creating Lucene index with " + numDocs + " documents...");

        for (int i = 0; i < numDocs; i++) {
            Document doc = new Document();
            String text = generateRandomText(100, rng); // 100 words per doc
            doc.add(new TextField("body", text, Field.Store.NO));
            writer.addDocument(doc);

            if ((i + 1) % 10000 == 0) {
                System.out.println("  Indexed " + (i + 1) + " documents...");
            }
        }

        writer.commit();
        writer.close();
        System.out.println("Lucene index created!");

        return dir;
    }

    /**
     * Run benchmark with warmup
     */
    private static long runBenchmark(IndexSearcher searcher, Query query, int topK, int iterations) throws IOException {
        // Warmup
        for (int i = 0; i < 10; i++) {
            searcher.search(query, topK);
        }

        // Actual benchmark
        long startTime = System.nanoTime();
        for (int i = 0; i < iterations; i++) {
            TopDocs results = searcher.search(query, topK);
        }
        long endTime = System.nanoTime();

        return (endTime - startTime) / iterations;
    }

    /**
     * Test 2-term OR query
     */
    private static void test2TermQuery(Directory dir, int numDocs) throws IOException {
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new BM25Similarity());

        // Create 2-term OR query (SHOULD clauses)
        BooleanQuery.Builder builder = new BooleanQuery.Builder();
        builder.add(new TermQuery(new Term("body", "search")), BooleanClause.Occur.SHOULD);
        builder.add(new TermQuery(new Term("body", "engine")), BooleanClause.Occur.SHOULD);
        Query query = builder.build();

        int topK = 10;
        int iterations = 100;

        long avgTime = runBenchmark(searcher, query, topK, iterations);

        System.out.printf("2-term OR query (%d docs, topK=%d): %.2f µs%n",
                         numDocs, topK, avgTime / 1000.0);

        reader.close();
    }

    /**
     * Test multi-term OR query
     */
    private static void testMultiTermQuery(Directory dir, int numTerms) throws IOException {
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new BM25Similarity());

        String[] queryTerms = {"search", "engine", "index", "document", "query",
                               "lucene", "database", "algorithm", "performance", "optimization"};

        BooleanQuery.Builder builder = new BooleanQuery.Builder();
        for (int i = 0; i < numTerms && i < queryTerms.length; i++) {
            builder.add(new TermQuery(new Term("body", queryTerms[i])), BooleanClause.Occur.SHOULD);
        }
        Query query = builder.build();

        int topK = 10;
        int iterations = 50;

        long avgTime = runBenchmark(searcher, query, topK, iterations);

        System.out.printf("%d-term OR query (topK=%d): %.2f µs%n",
                         numTerms, topK, avgTime / 1000.0);

        reader.close();
    }

    /**
     * Test different topK values
     */
    private static void testDifferentTopK(Directory dir, int topK) throws IOException {
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new BM25Similarity());

        // 3-term OR query
        BooleanQuery.Builder builder = new BooleanQuery.Builder();
        builder.add(new TermQuery(new Term("body", "search")), BooleanClause.Occur.SHOULD);
        builder.add(new TermQuery(new Term("body", "engine")), BooleanClause.Occur.SHOULD);
        builder.add(new TermQuery(new Term("body", "lucene")), BooleanClause.Occur.SHOULD);
        Query query = builder.build();

        int iterations = 50;

        long avgTime = runBenchmark(searcher, query, topK, iterations);

        System.out.printf("3-term OR query (topK=%d): %.2f µs%n",
                         topK, avgTime / 1000.0);

        reader.close();
    }

    public static void main(String[] args) throws IOException {
        System.out.println("=== Apache Lucene WAND Performance Benchmark ===");
        System.out.println("(Lucene automatically uses Block-Max WAND for OR queries)");
        System.out.println();

        // Test with 10K documents
        System.out.println("--- 10K Documents ---");
        Directory dir10k = createTestIndex(10000);
        test2TermQuery(dir10k, 10000);
        System.out.println();

        // Test with 100K documents
        System.out.println("--- 100K Documents ---");
        Directory dir100k = createTestIndex(100000);
        test2TermQuery(dir100k, 100000);
        System.out.println();

        System.out.println("--- Multi-term queries (100K docs) ---");
        testMultiTermQuery(dir100k, 2);
        testMultiTermQuery(dir100k, 5);
        testMultiTermQuery(dir100k, 10);
        System.out.println();

        System.out.println("--- Different topK values (100K docs) ---");
        testDifferentTopK(dir100k, 10);
        testDifferentTopK(dir100k, 100);
        testDifferentTopK(dir100k, 1000);

        System.out.println();
        System.out.println("Benchmark complete!");
    }
}
