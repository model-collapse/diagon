// Comprehensive Multi-Term Query Benchmark for Apache Lucene
// Matches the Diagon benchmark queries exactly

import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.search.similarities.BM25Similarity;
import org.apache.lucene.store.FSDirectory;
import org.apache.lucene.store.Directory;

import java.io.IOException;
import java.io.FileWriter;
import java.nio.file.*;
import java.util.*;
import java.util.stream.Collectors;

public class LuceneMultiTermBenchmark {

    private static final String REUTERS_PATH = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";
    private static final String INDEX_PATH = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/lucene_reuters_index";

    static class QueryResult {
        String queryName;
        int termCount;
        String queryType;
        int topK;
        int totalHits;
        double avgLatencyUs;
        double p50LatencyUs;
        double p95LatencyUs;
        double p99LatencyUs;
        double minLatencyUs;
        double maxLatencyUs;
    }

    private static String loadReutersDocument(String filepath) throws IOException {
        List<String> lines = Files.readAllLines(Paths.get(filepath));
        StringBuilder text = new StringBuilder();
        if (lines.size() > 2) {
            text.append(lines.get(2)).append(" ");
        }
        if (lines.size() > 4) {
            for (int i = 4; i < lines.size(); i++) {
                text.append(lines.get(i)).append(" ");
            }
        }
        return text.toString();
    }

    private static void createIndex(boolean useExisting) throws IOException {
        if (useExisting && Files.exists(Paths.get(INDEX_PATH))) {
            System.out.println("Using existing index at: " + INDEX_PATH);
            return;
        }

        System.out.println("Creating Lucene index from Reuters-21578...");

        // Clean up old index
        if (Files.exists(Paths.get(INDEX_PATH))) {
            Files.walk(Paths.get(INDEX_PATH))
                .sorted(Comparator.reverseOrder())
                .forEach(p -> {
                    try { Files.delete(p); } catch (IOException e) {}
                });
        }

        Directory dir = FSDirectory.open(Paths.get(INDEX_PATH));
        IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
        config.setRAMBufferSizeMB(128.0);
        config.setSimilarity(new BM25Similarity());

        IndexWriter writer = new IndexWriter(dir, config);

        List<Path> files = Files.walk(Paths.get(REUTERS_PATH))
            .filter(p -> p.toString().endsWith(".txt"))
            .collect(Collectors.toList());

        System.out.println("Found " + files.size() + " Reuters documents");

        int indexed = 0;
        long startTime = System.currentTimeMillis();

        for (Path filepath : files) {
            try {
                String text = loadReutersDocument(filepath.toString());
                Document doc = new Document();
                doc.add(new TextField("body", text, Field.Store.NO));
                writer.addDocument(doc);

                indexed++;
                if (indexed % 1000 == 0) {
                    System.out.print("  Indexed " + indexed + " documents...\r");
                }
            } catch (Exception e) {
                System.err.println("Error loading " + filepath + ": " + e.getMessage());
            }
        }

        long endTime = System.currentTimeMillis();
        System.out.println("\nCommitting index with " + indexed + " documents...");
        writer.commit();
        writer.close();
        dir.close();

        double throughput = indexed * 1000.0 / (endTime - startTime);
        System.out.println("✓ Indexed " + indexed + " documents in " +
                          ((endTime - startTime) / 1000.0) + " seconds");
        System.out.println("✓ Throughput: " + String.format("%.0f", throughput) + " docs/sec\n");
    }

    private static QueryResult benchmarkQuery(IndexSearcher searcher, String name,
                                               int termCount, String type, Query query,
                                               int topK, int iterations, int warmup) throws IOException {
        // Warmup
        for (int i = 0; i < warmup; i++) {
            searcher.search(query, topK);
        }

        // Benchmark
        long[] latencies = new long[iterations];
        int totalHits = 0;

        for (int i = 0; i < iterations; i++) {
            long start = System.nanoTime();
            TopDocs results = searcher.search(query, topK);
            long end = System.nanoTime();

            latencies[i] = (end - start) / 1000; // Convert to microseconds

            if (i == 0) {
                totalHits = (int) results.totalHits.value();
            }
        }

        Arrays.sort(latencies);

        QueryResult result = new QueryResult();
        result.queryName = name;
        result.termCount = termCount;
        result.queryType = type;
        result.topK = topK;
        result.totalHits = totalHits;

        result.avgLatencyUs = Arrays.stream(latencies).average().getAsDouble();
        result.p50LatencyUs = latencies[latencies.length / 2];
        result.p95LatencyUs = latencies[(int)(latencies.length * 0.95)];
        result.p99LatencyUs = latencies[(int)(latencies.length * 0.99)];
        result.minLatencyUs = latencies[0];
        result.maxLatencyUs = latencies[latencies.length - 1];

        return result;
    }

    private static void printResult(QueryResult r) {
        System.out.printf("  %-50s %5d %5s %7d %9.2f ms %9.2f ms %9.2f ms %9d hits%n",
                         r.queryName, r.termCount, r.queryType, r.topK,
                         r.avgLatencyUs / 1000.0,
                         r.p50LatencyUs / 1000.0,
                         r.p99LatencyUs / 1000.0,
                         r.totalHits);
    }

    private static void printTableHeader() {
        System.out.println();
        System.out.printf("  %-50s %5s %5s %7s %9s %9s %9s %9s%n",
                         "Query", "Terms", "Type", "TopK", "Avg", "P50", "P99", "Hits");
        System.out.println("  " + "-".repeat(120));
    }

    private static void saveResults(List<QueryResult> results, String filename) throws IOException {
        FileWriter writer = new FileWriter(filename);

        writer.write("# Apache Lucene Multi-Term Query Benchmark Results\n\n");
        writer.write("## Query Results\n\n");
        writer.write("| Query | Terms | Type | TopK | Avg (ms) | P50 (ms) | P95 (ms) | P99 (ms) | Hits |\n");
        writer.write("|-------|-------|------|------|----------|----------|----------|----------|------|\n");

        for (QueryResult r : results) {
            writer.write(String.format("| %s | %d | %s | %d | %.3f | %.3f | %.3f | %.3f | %d |\n",
                                      r.queryName, r.termCount, r.queryType, r.topK,
                                      r.avgLatencyUs / 1000.0,
                                      r.p50LatencyUs / 1000.0,
                                      r.p95LatencyUs / 1000.0,
                                      r.p99LatencyUs / 1000.0,
                                      r.totalHits));
        }

        writer.write("\n## Detailed Statistics\n\n");
        for (QueryResult r : results) {
            writer.write("### " + r.queryName + "\n");
            writer.write("- Terms: " + r.termCount + " " + r.queryType + "\n");
            writer.write("- TopK: " + r.topK + "\n");
            writer.write("- Hits: " + r.totalHits + "\n");
            writer.write(String.format("- Avg: %.3f ms\n", r.avgLatencyUs / 1000.0));
            writer.write(String.format("- P50: %.3f ms\n", r.p50LatencyUs / 1000.0));
            writer.write(String.format("- P95: %.3f ms\n", r.p95LatencyUs / 1000.0));
            writer.write(String.format("- P99: %.3f ms\n", r.p99LatencyUs / 1000.0));
            writer.write(String.format("- Min: %.3f ms\n", r.minLatencyUs / 1000.0));
            writer.write(String.format("- Max: %.3f ms\n\n", r.maxLatencyUs / 1000.0));
        }

        writer.close();
    }

    public static void main(String[] args) throws IOException {
        System.out.println("\n=========================================");
        System.out.println("Apache Lucene Multi-Term Query Benchmark");
        System.out.println("=========================================\n");

        boolean useExisting = args.length > 0 && args[0].equals("--use-existing");

        // Phase 1: Indexing
        System.out.println("Phase 1: Creating/Loading Index");
        System.out.println("========================================");
        createIndex(useExisting);

        // Phase 2: Query Benchmarks
        System.out.println("Phase 2: Query Benchmarks");
        System.out.println("========================================");

        Directory dir = FSDirectory.open(Paths.get(INDEX_PATH));
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new BM25Similarity());

        List<QueryResult> results = new ArrayList<>();

        printTableHeader();

        // Test 1: Single-term queries
        {
            Query query = new TermQuery(new Term("body", "market"));
            QueryResult r = benchmarkQuery(searcher, "Single: 'market'", 1, "N/A", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 2: 2-term OR queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            builder.add(new TermQuery(new Term("body", "trade")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "export")), BooleanClause.Occur.SHOULD);
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "OR-2: 'trade OR export'", 2, "OR", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 3: 3-term OR queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            builder.add(new TermQuery(new Term("body", "market")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "company")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "trade")), BooleanClause.Occur.SHOULD);
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "OR-3: 'market OR company OR trade'", 3, "OR", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 4: 5-term OR queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            builder.add(new TermQuery(new Term("body", "oil")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "trade")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "market")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "price")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "dollar")), BooleanClause.Occur.SHOULD);
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "OR-5: 'oil OR trade OR market OR price OR dollar'", 5, "OR", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 5: 10-term OR queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            String[] terms = {"oil", "trade", "market", "price", "dollar",
                            "export", "bank", "government", "company", "president"};
            for (String term : terms) {
                builder.add(new TermQuery(new Term("body", term)), BooleanClause.Occur.SHOULD);
            }
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "OR-10: common financial terms", 10, "OR", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 5b: 20-term OR queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            String[] terms = {"market", "company", "stock", "trade", "price",
                            "bank", "dollar", "oil", "export", "government",
                            "share", "billion", "profit", "exchange", "interest",
                            "economic", "report", "industry", "investment", "revenue"};
            for (String term : terms) {
                builder.add(new TermQuery(new Term("body", term)), BooleanClause.Occur.SHOULD);
            }
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "OR-20: broad financial terms", 20, "OR", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 5c: 50-term OR queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            String[] terms = {"market", "company", "stock", "trade", "price",
                            "bank", "dollar", "oil", "export", "government",
                            "share", "billion", "profit", "exchange", "interest",
                            "economic", "report", "industry", "investment", "revenue",
                            "million", "percent", "year", "said", "would",
                            "new", "also", "last", "first", "group",
                            "accord", "tax", "rate", "growth", "debt",
                            "loss", "quarter", "month", "net", "income",
                            "sales", "earnings", "bond", "foreign", "loan",
                            "budget", "deficit", "surplus", "inflation", "central"};
            for (String term : terms) {
                builder.add(new TermQuery(new Term("body", term)), BooleanClause.Occur.SHOULD);
            }
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "OR-50: comprehensive financial terms", 50, "OR", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 6: 2-term AND queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            builder.add(new TermQuery(new Term("body", "oil")), BooleanClause.Occur.MUST);
            builder.add(new TermQuery(new Term("body", "price")), BooleanClause.Occur.MUST);
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "AND-2: 'oil AND price'", 2, "AND", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 7: 3-term AND queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            builder.add(new TermQuery(new Term("body", "market")), BooleanClause.Occur.MUST);
            builder.add(new TermQuery(new Term("body", "stock")), BooleanClause.Occur.MUST);
            builder.add(new TermQuery(new Term("body", "trade")), BooleanClause.Occur.MUST);
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "AND-3: 'market AND stock AND trade'", 3, "AND", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        // Test 8: Different TopK values (5-term OR)
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            builder.add(new TermQuery(new Term("body", "oil")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "trade")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "market")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "price")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "dollar")), BooleanClause.Occur.SHOULD);
            Query query = builder.build();

            for (int topK : new int[]{10, 100, 1000}) {
                QueryResult r = benchmarkQuery(searcher, "OR-5 (topK=" + topK + ")", 5, "OR", query, topK, 100, 10);
                results.add(r);
                printResult(r);
            }
        }

        // Test 9: Rare term queries
        {
            BooleanQuery.Builder builder = new BooleanQuery.Builder();
            builder.add(new TermQuery(new Term("body", "cocoa")), BooleanClause.Occur.SHOULD);
            builder.add(new TermQuery(new Term("body", "coffee")), BooleanClause.Occur.SHOULD);
            Query query = builder.build();
            QueryResult r = benchmarkQuery(searcher, "OR-2 (rare): 'cocoa OR coffee'", 2, "OR", query, 10, 100, 10);
            results.add(r);
            printResult(r);
        }

        reader.close();
        dir.close();

        System.out.println("\n=========================================");
        System.out.println("Benchmark Complete");
        System.out.println("=========================================\n");

        saveResults(results, "lucene_multiterm_results.md");
        System.out.println("✓ Results saved to: lucene_multiterm_results.md\n");
    }
}
