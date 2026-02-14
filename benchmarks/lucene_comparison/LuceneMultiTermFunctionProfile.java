import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.store.*;
import org.apache.lucene.queryparser.classic.QueryParser;

import java.io.*;
import java.nio.file.*;
import java.util.*;

/**
 * Lucene Multi-Term Function-Level Profiler (Reuters-21578)
 *
 * Two profiling modes:
 * 1. Phase-level instrumentation: Actual measurements of query phases
 *    (parse, rewrite, createWeight, search) — NOT estimates
 * 2. JFR/perf-ready: Runs queries in tight loop for external sampling
 *
 * Uses MMapDirectory for read path (matches Diagon benchmark setup).
 * Uses IndexSearcher.count() for hit counts (LUCENE-8060 compliant).
 */
public class LuceneMultiTermFunctionProfile {

    private static final String REUTERS_PATH =
        "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";
    private static final String INDEX_PATH = "/tmp/lucene_multiterm_profile_index";

    private static final int WARMUP = 500;
    private static final int ITERATIONS = 5000;
    private static final int PROFILE_ITERATIONS = 50000;  // More iterations for JFR sampling

    // Query definitions: name -> query string
    static final String[][] QUERIES = {
        {"single-term: market",      "market"},
        {"OR-2: trade OR export",    "trade OR export"},
        {"OR-3: oil OR price OR dollar", "oil OR price OR dollar"},
        {"OR-5: oil OR trade OR market OR price OR dollar",
            "oil OR trade OR market OR price OR dollar"},
        {"OR-10: 10 common terms",
            "oil OR trade OR market OR price OR dollar OR economy OR bank OR stock OR government OR company"},
        {"AND-2: oil AND price",     "oil AND price"},
        {"AND-3: oil AND price AND trade", "oil AND price AND trade"},
    };

    static class PhaseTiming {
        String name;
        long parseNs;
        long rewriteNs;
        long createWeightNs;
        long searchNs;       // searcher.search(query, topK) — includes scorer setup + execution
        long totalNs;        // end-to-end
        int hitCount;        // from searcher.count(query)
        int topKCount;       // scoreDocs.length

        // Percentile arrays (filled after all iterations)
        long[] searchLatencies;
        long[] totalLatencies;

        PhaseTiming(String name, int iterations) {
            this.name = name;
            this.searchLatencies = new long[iterations];
            this.totalLatencies = new long[iterations];
        }
    }

    public static void main(String[] args) throws Exception {
        boolean profileMode = false;
        for (String arg : args) {
            if (arg.equals("--profile")) profileMode = true;
        }

        System.out.println("============================================================");
        System.out.println("Lucene Multi-Term Function-Level Profiler (Reuters-21578)");
        System.out.println("============================================================");
        System.out.println("Mode: " + (profileMode ? "PROFILE (tight loop for JFR/perf)" : "INSTRUMENT (phase-level timing)"));
        System.out.println("Warmup: " + WARMUP + " iterations");
        System.out.println("Measurement: " + ITERATIONS + " iterations\n");

        // Setup index with MMapDirectory for read path
        IndexReader reader = setupIndex();
        IndexSearcher searcher = new IndexSearcher(reader);
        searcher.setSimilarity(new org.apache.lucene.search.similarities.BM25Similarity());
        StandardAnalyzer analyzer = new StandardAnalyzer();

        System.out.println("Index: " + reader.numDocs() + " documents");
        System.out.println("Segments: " + reader.leaves().size());
        System.out.println("Directory: MMapDirectory\n");

        // Pre-parse all queries
        Query[] parsedQueries = new Query[QUERIES.length];
        for (int i = 0; i < QUERIES.length; i++) {
            QueryParser parser = new QueryParser("body", analyzer);
            parsedQueries[i] = parser.parse(QUERIES[i][1]);
        }

        // Get exact hit counts using IndexSearcher.count()
        System.out.println("Hit counts (via IndexSearcher.count):");
        int[] hitCounts = new int[QUERIES.length];
        for (int i = 0; i < QUERIES.length; i++) {
            hitCounts[i] = searcher.count(parsedQueries[i]);
            System.out.printf("  %-45s  hits=%d\n", QUERIES[i][0], hitCounts[i]);
        }
        System.out.println();

        if (profileMode) {
            runProfileMode(searcher, parsedQueries);
        } else {
            runInstrumentMode(searcher, analyzer, parsedQueries, hitCounts);
        }

        reader.close();
    }

    /**
     * Profile mode: tight loop for JFR/perf sampling.
     * Run with: java -XX:StartFlightRecording=... or perf record -g
     */
    static void runProfileMode(IndexSearcher searcher, Query[] queries) throws Exception {
        System.out.println("=== PROFILE MODE: Running tight loop ===");
        System.out.println("Attach profiler now, or use JFR/perf.\n");

        // Warmup
        for (int i = 0; i < WARMUP; i++) {
            for (Query q : queries) {
                TopDocs td = searcher.search(q, 10);
                if (td.scoreDocs.length < 0) throw new RuntimeException("dead code");
            }
        }
        System.gc();

        // Signal that measurement starts
        System.out.println(">>> MEASUREMENT START <<<");
        System.out.println("Running " + PROFILE_ITERATIONS + " iterations for profiling...");
        long startAll = System.nanoTime();

        for (int iter = 0; iter < PROFILE_ITERATIONS; iter++) {
            for (Query q : queries) {
                TopDocs td = searcher.search(q, 10);
                if (td.scoreDocs.length < 0) throw new RuntimeException("dead code");
            }
        }

        long elapsed = System.nanoTime() - startAll;
        System.out.println(">>> MEASUREMENT END <<<");
        System.out.printf("Total: %,d iterations x %d queries = %,d searches in %.3f ms\n",
            PROFILE_ITERATIONS, queries.length, (long)PROFILE_ITERATIONS * queries.length, elapsed / 1e6);
        System.out.printf("Average: %.1f µs per search\n",
            elapsed / 1000.0 / (PROFILE_ITERATIONS * queries.length));
    }

    /**
     * Instrument mode: Measure actual time per phase.
     * Phase 1: Parse (QueryParser)
     * Phase 2: Rewrite (searcher.rewrite)
     * Phase 3: CreateWeight (searcher.createWeight)
     * Phase 4: Search (searcher.search — includes scorer creation + iteration + scoring + collection)
     */
    static void runInstrumentMode(IndexSearcher searcher, StandardAnalyzer analyzer,
                                   Query[] parsedQueries, int[] hitCounts) throws Exception {

        PhaseTiming[] results = new PhaseTiming[QUERIES.length];
        for (int i = 0; i < QUERIES.length; i++) {
            results[i] = new PhaseTiming(QUERIES[i][0], ITERATIONS);
            results[i].hitCount = hitCounts[i];
        }

        // Warmup with all phases
        System.out.println("Warmup...");
        for (int w = 0; w < WARMUP; w++) {
            for (int q = 0; q < QUERIES.length; q++) {
                QueryParser parser = new QueryParser("body", analyzer);
                Query query = parser.parse(QUERIES[q][1]);
                Query rewritten = searcher.rewrite(query);
                Weight weight = searcher.createWeight(rewritten, ScoreMode.TOP_SCORES, 1.0f);
                TopDocs td = searcher.search(query, 10);
                if (td.scoreDocs.length < 0) throw new RuntimeException("dead code");
            }
        }
        System.gc();
        Thread.sleep(100);
        System.out.println("Warmup complete.\n");

        // Measure each query
        for (int q = 0; q < QUERIES.length; q++) {
            PhaseTiming pt = results[q];
            String queryStr = QUERIES[q][1];

            long totalParse = 0, totalRewrite = 0, totalWeight = 0, totalSearch = 0, totalAll = 0;

            for (int iter = 0; iter < ITERATIONS; iter++) {
                long t0 = System.nanoTime();

                // Phase 1: Parse
                long t1 = System.nanoTime();
                QueryParser parser = new QueryParser("body", analyzer);
                Query query = parser.parse(queryStr);
                long t2 = System.nanoTime();

                // Phase 2: Rewrite
                Query rewritten = searcher.rewrite(query);
                long t3 = System.nanoTime();

                // Phase 3: CreateWeight
                Weight weight = searcher.createWeight(rewritten, ScoreMode.TOP_SCORES, 1.0f);
                long t4 = System.nanoTime();

                // Phase 4: Search (scorer creation + iteration + scoring + collection)
                TopDocs td = searcher.search(query, 10);
                long t5 = System.nanoTime();

                totalParse += (t2 - t1);
                totalRewrite += (t3 - t2);
                totalWeight += (t4 - t3);
                totalSearch += (t5 - t4);
                totalAll += (t5 - t0);

                pt.searchLatencies[iter] = t5 - t4;
                pt.totalLatencies[iter] = t5 - t0;
                pt.topKCount = td.scoreDocs.length;
            }

            pt.parseNs = totalParse / ITERATIONS;
            pt.rewriteNs = totalRewrite / ITERATIONS;
            pt.createWeightNs = totalWeight / ITERATIONS;
            pt.searchNs = totalSearch / ITERATIONS;
            pt.totalNs = totalAll / ITERATIONS;

            System.out.printf("  %-45s  avg=%.1f µs\n", pt.name, pt.totalNs / 1000.0);
        }

        System.out.println();

        // Also measure search-only (no parse, pre-built queries) for fair comparison
        System.out.println("--- Search-Only Measurement (pre-parsed queries, no parse/rewrite overhead) ---\n");

        PhaseTiming[] searchOnly = new PhaseTiming[QUERIES.length];
        for (int i = 0; i < QUERIES.length; i++) {
            searchOnly[i] = new PhaseTiming(QUERIES[i][0], ITERATIONS);
            searchOnly[i].hitCount = hitCounts[i];
        }

        // Warmup search-only
        for (int w = 0; w < WARMUP; w++) {
            for (Query pq : parsedQueries) {
                TopDocs td = searcher.search(pq, 10);
                if (td.scoreDocs.length < 0) throw new RuntimeException("dead code");
            }
        }
        System.gc();
        Thread.sleep(100);

        for (int q = 0; q < QUERIES.length; q++) {
            PhaseTiming pt = searchOnly[q];
            Query query = parsedQueries[q];

            long totalSearch = 0;

            for (int iter = 0; iter < ITERATIONS; iter++) {
                long t0 = System.nanoTime();
                TopDocs td = searcher.search(query, 10);
                long t1 = System.nanoTime();

                long elapsed = t1 - t0;
                totalSearch += elapsed;
                pt.searchLatencies[iter] = elapsed;
                pt.totalLatencies[iter] = elapsed;
                pt.topKCount = td.scoreDocs.length;
            }

            pt.searchNs = totalSearch / ITERATIONS;
            pt.totalNs = pt.searchNs;
        }

        // Sort latency arrays for percentile calculation
        for (PhaseTiming pt : results) {
            Arrays.sort(pt.searchLatencies);
            Arrays.sort(pt.totalLatencies);
        }
        for (PhaseTiming pt : searchOnly) {
            Arrays.sort(pt.searchLatencies);
            Arrays.sort(pt.totalLatencies);
        }

        // ============================================================
        // Print Report
        // ============================================================
        System.out.println("\n============================================================");
        System.out.println("LUCENE MULTI-TERM FUNCTION-LEVEL PROFILE (Reuters-21578)");
        System.out.println("============================================================\n");

        System.out.println("## Phase Breakdown (Full Pipeline: Parse + Rewrite + Weight + Search)\n");
        System.out.printf("%-35s | %8s | %8s | %8s | %8s | %8s | %8s | %6s\n",
            "Query", "Parse", "Rewrite", "Weight", "Search", "Total", "Hits", "Top-K");
        System.out.printf("%-35s | %8s | %8s | %8s | %8s | %8s | %8s | %6s\n",
            "", "(µs)", "(µs)", "(µs)", "(µs)", "(µs)", "", "");
        System.out.println("-".repeat(112));

        for (PhaseTiming pt : results) {
            System.out.printf("%-35s | %8.1f | %8.1f | %8.1f | %8.1f | %8.1f | %8d | %6d\n",
                pt.name,
                pt.parseNs / 1000.0,
                pt.rewriteNs / 1000.0,
                pt.createWeightNs / 1000.0,
                pt.searchNs / 1000.0,
                pt.totalNs / 1000.0,
                pt.hitCount,
                pt.topKCount);
        }

        System.out.println("\n\n## Search-Only Latency (Pre-parsed, Diagon-Comparable)\n");
        System.out.println("This is the fair comparison target for Diagon — excludes Java-specific");
        System.out.println("overhead (QueryParser, rewrite). Lucene's search() includes:");
        System.out.println("  createWeight + scorer() per segment + BulkScorer.score() + collection\n");

        System.out.printf("%-35s | %8s | %8s | %8s | %8s | %8s | %8s\n",
            "Query", "Mean", "P50", "P95", "P99", "Min", "Hits");
        System.out.printf("%-35s | %8s | %8s | %8s | %8s | %8s | %8s\n",
            "", "(µs)", "(µs)", "(µs)", "(µs)", "(µs)", "");
        System.out.println("-".repeat(100));

        for (PhaseTiming pt : searchOnly) {
            System.out.printf("%-35s | %8.1f | %8.1f | %8.1f | %8.1f | %8.1f | %8d\n",
                pt.name,
                pt.searchNs / 1000.0,
                pt.searchLatencies[(int)(ITERATIONS * 0.50)] / 1000.0,
                pt.searchLatencies[(int)(ITERATIONS * 0.95)] / 1000.0,
                pt.searchLatencies[(int)(ITERATIONS * 0.99)] / 1000.0,
                pt.searchLatencies[0] / 1000.0,
                pt.hitCount);
        }

        System.out.println("\n\n## Phase Percentage Breakdown\n");
        System.out.printf("%-35s | %7s | %7s | %7s | %7s\n",
            "Query", "Parse%", "Rewrt%", "Wght%", "Srch%");
        System.out.println("-".repeat(78));

        for (PhaseTiming pt : results) {
            double total = pt.totalNs;
            System.out.printf("%-35s | %6.1f%% | %6.1f%% | %6.1f%% | %6.1f%%\n",
                pt.name,
                100.0 * pt.parseNs / total,
                100.0 * pt.rewriteNs / total,
                100.0 * pt.createWeightNs / total,
                100.0 * pt.searchNs / total);
        }

        // Full pipeline percentile table
        System.out.println("\n\n## Full Pipeline Percentiles\n");
        System.out.printf("%-35s | %8s | %8s | %8s | %8s\n",
            "Query", "P50", "P95", "P99", "Max");
        System.out.printf("%-35s | %8s | %8s | %8s | %8s\n",
            "", "(µs)", "(µs)", "(µs)", "(µs)");
        System.out.println("-".repeat(78));

        for (PhaseTiming pt : results) {
            System.out.printf("%-35s | %8.1f | %8.1f | %8.1f | %8.1f\n",
                pt.name,
                pt.totalLatencies[(int)(ITERATIONS * 0.50)] / 1000.0,
                pt.totalLatencies[(int)(ITERATIONS * 0.95)] / 1000.0,
                pt.totalLatencies[(int)(ITERATIONS * 0.99)] / 1000.0,
                pt.totalLatencies[ITERATIONS - 1] / 1000.0);
        }

        System.out.println("\n\n============================================================");
        System.out.println("END OF REPORT");
        System.out.println("============================================================");
    }

    static IndexReader setupIndex() throws Exception {
        Path indexPath = Paths.get(INDEX_PATH);

        if (Files.exists(indexPath)) {
            MMapDirectory readDir = new MMapDirectory(indexPath);
            if (DirectoryReader.indexExists(readDir)) {
                System.out.println("Using existing index at " + INDEX_PATH);
                return DirectoryReader.open(readDir);
            }
        }

        System.out.println("Creating index at " + INDEX_PATH + "...");
        FSDirectory writeDir = FSDirectory.open(indexPath);
        IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
        config.setOpenMode(IndexWriterConfig.OpenMode.CREATE);
        config.setRAMBufferSizeMB(256);
        IndexWriter writer = new IndexWriter(writeDir, config);

        int count = 0;
        File reutersDir = new File(REUTERS_PATH);
        File[] files = reutersDir.listFiles((d, name) -> name.endsWith(".txt"));
        if (files == null) throw new RuntimeException("Reuters dataset not found at " + REUTERS_PATH);
        Arrays.sort(files);

        for (File file : files) {
            try (BufferedReader br = new BufferedReader(new FileReader(file))) {
                String date = br.readLine();
                if (date == null) continue;
                br.readLine(); // skip empty line
                String title = br.readLine();
                if (title == null) title = "";
                br.readLine(); // skip empty line
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
            }
        }

        writer.forceMerge(1);  // Single segment for consistent comparison
        writer.close();
        System.out.println("Indexed " + count + " documents (force-merged to 1 segment)\n");

        // Re-open with MMapDirectory for reads
        return DirectoryReader.open(new MMapDirectory(indexPath));
    }
}
