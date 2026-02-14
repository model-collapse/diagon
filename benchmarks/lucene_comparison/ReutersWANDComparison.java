// WAND Performance Comparison on Reuters-21578: Diagon vs Apache Lucene

import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.search.similarities.BM25Similarity;
import org.apache.lucene.store.ByteBuffersDirectory;
import org.apache.lucene.store.Directory;

import java.io.IOException;
import java.nio.file.*;
import java.util.*;
import java.util.stream.Collectors;

public class ReutersWANDComparison {

    private static final String REUTERS_PATH = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";

    /**
     * Load a single Reuters document from text file
     */
    private static String loadReutersDocument(String filepath) throws IOException {
        List<String> lines = Files.readAllLines(Paths.get(filepath));

        // Skip date (line 0) and blank line (line 1)
        // Line 2 is title, skip blank (line 3), rest is body
        StringBuilder text = new StringBuilder();
        if (lines.size() > 2) {
            text.append(lines.get(2)).append(" "); // title
        }
        if (lines.size() > 4) {
            for (int i = 4; i < lines.size(); i++) {
                text.append(lines.get(i)).append(" ");
            }
        }
        return text.toString();
    }

    /**
     * Create Lucene index from Reuters-21578 dataset
     */
    private static Directory createReutersIndex() throws IOException {
        Directory dir = new ByteBuffersDirectory();

        IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
        config.setRAMBufferSizeMB(128.0);
        config.setSimilarity(new BM25Similarity());

        IndexWriter writer = new IndexWriter(dir, config);

        System.out.println("Creating Lucene index from Reuters-21578...");

        // Load all Reuters documents
        List<Path> files = Files.walk(Paths.get(REUTERS_PATH))
            .filter(p -> p.toString().endsWith(".txt"))
            .collect(Collectors.toList());

        System.out.println("Found " + files.size() + " Reuters documents");

        int indexed = 0;
        for (Path filepath : files) {
            try {
                String text = loadReutersDocument(filepath.toString());

                Document doc = new Document();
                doc.add(new TextField("body", text, Field.Store.NO));
                writer.addDocument(doc);

                indexed++;
                if (indexed % 1000 == 0) {
                    System.out.println("  Indexed " + indexed + " documents...");
                }
            } catch (Exception e) {
                System.err.println("Error loading " + filepath + ": " + e.getMessage());
            }
        }

        System.out.println("Committing index with " + indexed + " documents...");
        writer.commit();
        writer.close();
        System.out.println("Lucene Reuters index created!");

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
    private static void test2TermQuery(Directory dir) throws IOException {
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new BM25Similarity());

        // Same terms as Diagon benchmark
        BooleanQuery.Builder builder = new BooleanQuery.Builder();
        builder.add(new TermQuery(new Term("body", "market")), BooleanClause.Occur.SHOULD);
        builder.add(new TermQuery(new Term("body", "company")), BooleanClause.Occur.SHOULD);
        Query query = builder.build();

        int topK = 10;
        int iterations = 100;

        long avgTime = runBenchmark(searcher, query, topK, iterations);

        System.out.printf("2-term OR query (topK=%d): %.2f µs%n",
                         topK, avgTime / 1000.0);

        reader.close();
    }

    /**
     * Test multi-term OR query
     */
    private static void testMultiTermQuery(Directory dir, int numTerms) throws IOException {
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new BM25Similarity());

        String[] queryTerms = {"market", "company", "stock", "trade", "price",
                               "bank", "dollar", "oil", "export", "government"};

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
        builder.add(new TermQuery(new Term("body", "market")), BooleanClause.Occur.SHOULD);
        builder.add(new TermQuery(new Term("body", "company")), BooleanClause.Occur.SHOULD);
        builder.add(new TermQuery(new Term("body", "trade")), BooleanClause.Occur.SHOULD);
        Query query = builder.build();

        int iterations = 50;

        long avgTime = runBenchmark(searcher, query, topK, iterations);

        System.out.printf("3-term OR query (topK=%d): %.2f µs%n",
                         topK, avgTime / 1000.0);

        reader.close();
    }

    /**
     * Test rare term query
     */
    private static void testRareTermQuery(Directory dir) throws IOException {
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new BM25Similarity());

        // Rare terms
        BooleanQuery.Builder builder = new BooleanQuery.Builder();
        builder.add(new TermQuery(new Term("body", "cocoa")), BooleanClause.Occur.SHOULD);
        builder.add(new TermQuery(new Term("body", "coffee")), BooleanClause.Occur.SHOULD);
        Query query = builder.build();

        int topK = 10;
        int iterations = 100;

        long avgTime = runBenchmark(searcher, query, topK, iterations);

        System.out.printf("Rare term query (topK=%d): %.2f µs%n",
                         topK, avgTime / 1000.0);

        reader.close();
    }

    public static void main(String[] args) throws IOException {
        System.out.println("=== Apache Lucene WAND Performance Benchmark (Reuters-21578) ===");
        System.out.println("(Lucene automatically uses Block-Max WAND for OR queries)");
        System.out.println();

        Directory dir = createReutersIndex();

        System.out.println("--- 2-term OR queries ---");
        test2TermQuery(dir);
        System.out.println();

        System.out.println("--- Multi-term queries ---");
        testMultiTermQuery(dir, 2);
        testMultiTermQuery(dir, 5);
        testMultiTermQuery(dir, 10);
        System.out.println();

        System.out.println("--- Different topK values ---");
        testDifferentTopK(dir, 10);
        testDifferentTopK(dir, 100);
        testDifferentTopK(dir, 1000);
        System.out.println();

        System.out.println("--- Rare term query ---");
        testRareTermQuery(dir);

        System.out.println();
        System.out.println("Benchmark complete!");
    }
}
