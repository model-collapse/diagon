import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.store.*;
import org.apache.lucene.queryparser.classic.QueryParser;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

/**
 * Comprehensive Lucene Multi-Term Query Profiler
 *
 * Benchmarks Lucene's multi-term query performance on Reuters-21578,
 * with detailed profiling of component-level timing.
 */
public class LuceneMultiTermProfiler {

    private static final String REUTERS_PATH = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";
    private static final String INDEX_PATH = "/tmp/lucene_profile_index";
    private static final int WARMUP_ITERATIONS = 100;
    private static final int BENCHMARK_ITERATIONS = 1000;

    static class TimingBreakdown {
        long parseTime = 0;
        long rewriteTime = 0;
        long scorerCreationTime = 0;
        long searchTime = 0;
        long totalTime = 0;
        int hits = 0;

        void add(TimingBreakdown other) {
            parseTime += other.parseTime;
            rewriteTime += other.rewriteTime;
            scorerCreationTime += other.scorerCreationTime;
            searchTime += other.searchTime;
            totalTime += other.totalTime;
            hits += other.hits;
        }

        void average(int count) {
            parseTime /= count;
            rewriteTime /= count;
            scorerCreationTime /= count;
            searchTime /= count;
            totalTime /= count;
            hits /= count;
        }

        void printBreakdown(String queryName) {
            System.out.println("  " + queryName + ":");
            System.out.printf("    Parse:           %,8d ns (%5.1f%%)\n",
                parseTime, 100.0 * parseTime / totalTime);
            System.out.printf("    Rewrite:         %,8d ns (%5.1f%%)\n",
                rewriteTime, 100.0 * rewriteTime / totalTime);
            System.out.printf("    Scorer Creation: %,8d ns (%5.1f%%)\n",
                scorerCreationTime, 100.0 * scorerCreationTime / totalTime);
            System.out.printf("    Search:          %,8d ns (%5.1f%%)\n",
                searchTime, 100.0 * searchTime / totalTime);
            System.out.printf("    TOTAL:           %,8d ns (100.0%%)\n", totalTime);
            System.out.printf("    Hits: %d\n", hits);
        }
    }

    static class QueryResult {
        String queryName;
        String queryString;
        int terms;
        String type;
        List<Long> latencies = new ArrayList<>();
        int hits;
        TimingBreakdown breakdown;

        QueryResult(String name, String query, int terms, String type) {
            this.queryName = name;
            this.queryString = query;
            this.terms = terms;
            this.type = type;
            this.breakdown = new TimingBreakdown();
        }

        void addTiming(long nanos, TimingBreakdown bd) {
            latencies.add(nanos);
            breakdown.add(bd);
        }

        double getP50() {
            return getPercentile(50);
        }

        double getP95() {
            return getPercentile(95);
        }

        double getP99() {
            return getPercentile(99);
        }

        double getAvg() {
            return latencies.stream().mapToLong(Long::longValue).average().orElse(0.0);
        }

        double getPercentile(int percentile) {
            if (latencies.isEmpty()) return 0.0;
            Collections.sort(latencies);
            int index = (int) Math.ceil(percentile / 100.0 * latencies.size()) - 1;
            return latencies.get(Math.max(0, index));
        }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("=========================================");
        System.out.println("Lucene Multi-Term Query Profiler");
        System.out.println("=========================================\n");

        // Index Reuters if needed
        System.out.println("Phase 1: Index Setup");
        System.out.println("========================================");
        IndexReader reader = setupIndex();
        IndexSearcher searcher = new IndexSearcher(reader);
        StandardAnalyzer analyzer = new StandardAnalyzer();

        System.out.println("Index statistics:");
        System.out.println("  Documents: " + reader.numDocs());
        System.out.println("  Max doc: " + reader.maxDoc());
        System.out.println("\n");

        // Define queries to benchmark
        List<QueryResult> results = new ArrayList<>();
        results.add(new QueryResult("Single: 'market'", "market", 1, "SINGLE"));
        results.add(new QueryResult("OR-2: 'trade OR export'", "trade OR export", 2, "OR"));
        results.add(new QueryResult("OR-3: 'market OR company OR trade'", "market OR company OR trade", 3, "OR"));
        results.add(new QueryResult("OR-5: 'oil OR trade OR market OR price OR dollar'",
            "oil OR trade OR market OR price OR dollar", 5, "OR"));
        results.add(new QueryResult("OR-10: common financial terms",
            "oil OR trade OR market OR price OR dollar OR economy OR bank OR stock OR government OR company", 10, "OR"));
        results.add(new QueryResult("AND-2: 'oil AND price'", "oil AND price", 2, "AND"));
        results.add(new QueryResult("AND-3: 'market AND stock AND trade'",
            "market AND stock AND trade", 3, "AND"));
        results.add(new QueryResult("OR-2 (rare): 'cocoa OR coffee'", "cocoa OR coffee", 2, "OR"));

        // Phase 2: Warmup
        System.out.println("Phase 2: JVM Warmup");
        System.out.println("========================================");
        System.out.println("Running " + WARMUP_ITERATIONS + " warmup iterations...");
        for (QueryResult qr : results) {
            QueryParser parser = new QueryParser("body", analyzer);
            Query query = parser.parse(qr.queryString);
            for (int i = 0; i < WARMUP_ITERATIONS; i++) {
                TopDocs topDocs = searcher.search(query, 10);
            }
        }
        System.out.println("✓ Warmup complete\n");

        // Phase 3: Benchmark with profiling
        System.out.println("Phase 3: Benchmark with Profiling");
        System.out.println("========================================");
        System.out.println("Running " + BENCHMARK_ITERATIONS + " iterations per query...\n");

        for (QueryResult qr : results) {
            System.out.println("Benchmarking: " + qr.queryName);

            for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
                TimingBreakdown bd = new TimingBreakdown();
                long totalStart = System.nanoTime();

                // 1. Parse query
                long parseStart = System.nanoTime();
                QueryParser parser = new QueryParser("body", analyzer);
                Query query = parser.parse(qr.queryString);
                bd.parseTime = System.nanoTime() - parseStart;

                // 2. Rewrite query
                long rewriteStart = System.nanoTime();
                Query rewritten = searcher.rewrite(query);
                bd.rewriteTime = System.nanoTime() - rewriteStart;

                // 3. Create weight/scorer (includes term lookup, postings init)
                long scorerStart = System.nanoTime();
                Weight weight = searcher.createWeight(rewritten, ScoreMode.TOP_SCORES, 1.0f);
                bd.scorerCreationTime = System.nanoTime() - scorerStart;

                // 4. Search (scoring and collection)
                long searchStart = System.nanoTime();
                TopDocs topDocs = searcher.search(query, 10);
                bd.searchTime = System.nanoTime() - searchStart;

                bd.totalTime = System.nanoTime() - totalStart;
                bd.hits = (int) topDocs.totalHits.value();

                qr.addTiming(bd.totalTime, bd);
                qr.hits = bd.hits;
            }

            qr.breakdown.average(BENCHMARK_ITERATIONS);
            System.out.println("  ✓ Complete\n");
        }

        // Phase 4: Results
        System.out.println("\n=========================================");
        System.out.println("Benchmark Results");
        System.out.println("=========================================\n");

        System.out.println("Summary Table:");
        System.out.println("  Query                                              Terms  Type    P50 (ms)  P95 (ms)  P99 (ms)  Hits");
        System.out.println("  " + "-".repeat(100));

        for (QueryResult qr : results) {
            System.out.printf("  %-50s %5d  %-6s  %8.3f  %8.3f  %8.3f  %5d\n",
                qr.queryName, qr.terms, qr.type,
                qr.getP50() / 1_000_000.0,
                qr.getP95() / 1_000_000.0,
                qr.getP99() / 1_000_000.0,
                qr.hits);
        }

        // Phase 5: Detailed profiling breakdown
        System.out.println("\n\n=========================================");
        System.out.println("Component-Level Time Breakdown");
        System.out.println("=========================================\n");

        for (QueryResult qr : results) {
            qr.breakdown.printBreakdown(qr.queryName);
            System.out.println();
        }

        // Phase 6: Generate detailed report
        generateDetailedReport(results, reader);

        reader.close();

        System.out.println("\n=========================================");
        System.out.println("Benchmark Complete");
        System.out.println("=========================================");
        System.out.println("\n✓ Results saved to: lucene_profiling_results.md");
        System.out.println("✓ Analysis document: /home/ubuntu/diagon/docs/LUCENE_MULTITERM_BASELINE_ANALYSIS.md");
    }

    static IndexReader setupIndex() throws Exception {
        Path indexPath = Paths.get(INDEX_PATH);

        if (Files.exists(indexPath)) {
            System.out.println("Using existing index at: " + INDEX_PATH);
            return DirectoryReader.open(FSDirectory.open(indexPath));
        }

        System.out.println("Creating new index at: " + INDEX_PATH);
        FSDirectory dir = FSDirectory.open(indexPath);
        StandardAnalyzer analyzer = new StandardAnalyzer();
        IndexWriterConfig config = new IndexWriterConfig(analyzer);
        config.setOpenMode(IndexWriterConfig.OpenMode.CREATE);

        IndexWriter writer = new IndexWriter(dir, config);

        // Index Reuters documents
        // Each file is one document with format:
        // Line 1: Date
        // Line 2: Empty
        // Line 3: Title
        // Line 4: Empty
        // Lines 5+: Body
        int count = 0;
        File reutersDir = new File(REUTERS_PATH);
        for (File file : reutersDir.listFiles((d, name) -> name.endsWith(".txt"))) {
            try (BufferedReader br = new BufferedReader(new FileReader(file))) {
                String date = br.readLine();
                if (date == null) continue;

                br.readLine(); // Skip empty line
                String title = br.readLine();
                if (title == null) title = "";

                br.readLine(); // Skip empty line

                // Read remaining lines as body
                StringBuilder body = new StringBuilder();
                String line;
                while ((line = br.readLine()) != null) {
                    if (body.length() > 0) body.append(" ");
                    body.append(line);
                }

                Document doc = new Document();
                doc.add(new TextField("title", title, Field.Store.YES));
                doc.add(new TextField("body", body.toString(), Field.Store.NO));
                doc.add(new StoredField("date", date));
                writer.addDocument(doc);
                count++;

                if (count % 5000 == 0) {
                    System.out.println("  Indexed " + count + " documents...");
                }
            }
        }

        System.out.println("✓ Indexed " + count + " documents");
        writer.commit();
        writer.close();

        return DirectoryReader.open(dir);
    }

    static void generateDetailedReport(List<QueryResult> results, IndexReader reader) throws IOException {
        PrintWriter pw = new PrintWriter(new FileWriter("lucene_profiling_results.md"));

        pw.println("# Lucene Multi-Term Query Profiling Results");
        pw.println();
        pw.println("**Date**: " + new Date());
        pw.println("**Dataset**: Reuters-21578");
        pw.println("**Documents**: " + reader.numDocs());
        pw.println("**Lucene Version**: 11.0.0-SNAPSHOT");
        pw.println("**Iterations**: " + BENCHMARK_ITERATIONS + " (after " + WARMUP_ITERATIONS + " warmup)");
        pw.println();

        pw.println("## Performance Summary");
        pw.println();
        pw.println("| Query | Terms | Type | P50 (ms) | P95 (ms) | P99 (ms) | Hits |");
        pw.println("|-------|-------|------|----------|----------|----------|------|");

        for (QueryResult qr : results) {
            pw.printf("| %s | %d | %s | %.3f | %.3f | %.3f | %d |\n",
                qr.queryName, qr.terms, qr.type,
                qr.getP50() / 1_000_000.0,
                qr.getP95() / 1_000_000.0,
                qr.getP99() / 1_000_000.0,
                qr.hits);
        }

        pw.println();
        pw.println("## Component Time Breakdown");
        pw.println();

        for (QueryResult qr : results) {
            pw.println("### " + qr.queryName);
            pw.println();
            pw.printf("- **Parse**: %,d ns (%.1f%%)\n",
                qr.breakdown.parseTime, 100.0 * qr.breakdown.parseTime / qr.breakdown.totalTime);
            pw.printf("- **Rewrite**: %,d ns (%.1f%%)\n",
                qr.breakdown.rewriteTime, 100.0 * qr.breakdown.rewriteTime / qr.breakdown.totalTime);
            pw.printf("- **Scorer Creation**: %,d ns (%.1f%%)\n",
                qr.breakdown.scorerCreationTime, 100.0 * qr.breakdown.scorerCreationTime / qr.breakdown.totalTime);
            pw.printf("- **Search**: %,d ns (%.1f%%)\n",
                qr.breakdown.searchTime, 100.0 * qr.breakdown.searchTime / qr.breakdown.totalTime);
            pw.printf("- **TOTAL**: %,d ns\n", qr.breakdown.totalTime);
            pw.printf("- **Hits**: %d\n", qr.hits);
            pw.println();
        }

        pw.close();
    }
}
