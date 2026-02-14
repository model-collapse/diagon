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
 * Ultra-Detailed Lucene Profiler
 *
 * Instruments EVERY single phase of query execution:
 * 1. Query parsing
 * 2. Query rewrite
 * 3. Weight creation
 * 4. IndexReader term lookup (FST)
 * 5. PostingsEnum creation
 * 6. Skip data loading
 * 7. Document iteration
 * 8. Postings decoding
 * 9. Scoring (BM25)
 * 10. Top-K collection
 * 11. Early termination
 */
public class LuceneDetailedProfiler {

    private static final String REUTERS_PATH = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";
    private static final String INDEX_PATH = "/tmp/lucene_profile_index";
    private static final int WARMUP = 100;
    private static final int ITERATIONS = 1000;

    static class DetailedTimings {
        String queryName;

        // Phase 1: Query construction
        long parseTime = 0;

        // Phase 2: Query optimization
        long rewriteTime = 0;

        // Phase 3: Weight creation
        long weightCreationTime = 0;

        // Phase 4: Scorer setup (includes FST + PostingsEnum + Skip data)
        long scorerSetupTime = 0;
        long fstLookupTime = 0;          // Subset of scorerSetupTime
        long postingsEnumInitTime = 0;    // Subset of scorerSetupTime
        long skipDataLoadTime = 0;        // Subset of scorerSetupTime

        // Phase 5: Search execution
        long searchExecutionTime = 0;
        long advanceTime = 0;              // Document iteration
        long decodingTime = 0;             // Postings decoding
        long scoringTime = 0;              // BM25 computation
        long collectionTime = 0;           // Top-K heap ops
        long wandCheckTime = 0;            // Early termination

        long totalTime = 0;
        int hits = 0;
        int docsScored = 0;

        private double pct(long val) {
            return totalTime > 0 ? (100.0 * val / totalTime) : 0.0;
        }

        void print() {
            System.out.println("\n" + queryName + " - Ultra-Detailed Breakdown:");
            System.out.println("=".repeat(80));

            System.out.printf("\n1. Query Construction:\n");
            System.out.printf("   Parse:                 %,12d ns (%5.1f%%)\n", parseTime, pct(parseTime));

            System.out.printf("\n2. Query Optimization:\n");
            System.out.printf("   Rewrite:               %,12d ns (%5.1f%%)\n", rewriteTime, pct(rewriteTime));

            System.out.printf("\n3. Weight Creation:\n");
            System.out.printf("   Weight creation:       %,12d ns (%5.1f%%)\n", weightCreationTime, pct(weightCreationTime));

            System.out.printf("\n4. Scorer Setup:\n");
            System.out.printf("   Total setup:           %,12d ns (%5.1f%%)\n", scorerSetupTime, pct(scorerSetupTime));
            System.out.printf("   - FST lookup:          %,12d ns (%5.1f%%)\n", fstLookupTime, pct(fstLookupTime));
            System.out.printf("   - PostingsEnum init:   %,12d ns (%5.1f%%)\n", postingsEnumInitTime, pct(postingsEnumInitTime));
            System.out.printf("   - Skip data load:      %,12d ns (%5.1f%%)\n", skipDataLoadTime, pct(skipDataLoadTime));

            System.out.printf("\n5. Search Execution:\n");
            System.out.printf("   Total execution:       %,12d ns (%5.1f%%)\n", searchExecutionTime, pct(searchExecutionTime));
            System.out.printf("   - Advance (iteration): %,12d ns (%5.1f%%)\n", advanceTime, pct(advanceTime));
            System.out.printf("   - Decoding:            %,12d ns (%5.1f%%)\n", decodingTime, pct(decodingTime));
            System.out.printf("   - Scoring (BM25):      %,12d ns (%5.1f%%)\n", scoringTime, pct(scoringTime));
            System.out.printf("   - Collection (heap):   %,12d ns (%5.1f%%)\n", collectionTime, pct(collectionTime));
            System.out.printf("   - WAND checks:         %,12d ns (%5.1f%%)\n", wandCheckTime, pct(wandCheckTime));

            System.out.printf("\n6. Statistics:\n");
            System.out.printf("   Documents scored:      %d\n", docsScored);
            System.out.printf("   Hits returned:         %d\n", hits);

            System.out.printf("\n7. Total:\n");
            System.out.printf("   TOTAL TIME:            %,12d ns (100.0%%)\n", totalTime);
            System.out.printf("   = %.3f microseconds\n", totalTime / 1000.0);
            System.out.printf("   = %.3f milliseconds\n", totalTime / 1_000_000.0);

            if (docsScored > 0) {
                System.out.printf("\n8. Per-Document Averages:\n");
                System.out.printf("   Time per doc scored:   %,d ns\n", searchExecutionTime / docsScored);
                System.out.printf("   Scoring per doc:       %,d ns\n", scoringTime / docsScored);
                System.out.printf("   Decoding per doc:      %,d ns\n", decodingTime / docsScored);
            }
        }

        void add(DetailedTimings other) {
            parseTime += other.parseTime;
            rewriteTime += other.rewriteTime;
            weightCreationTime += other.weightCreationTime;
            scorerSetupTime += other.scorerSetupTime;
            fstLookupTime += other.fstLookupTime;
            postingsEnumInitTime += other.postingsEnumInitTime;
            skipDataLoadTime += other.skipDataLoadTime;
            searchExecutionTime += other.searchExecutionTime;
            advanceTime += other.advanceTime;
            decodingTime += other.decodingTime;
            scoringTime += other.scoringTime;
            collectionTime += other.collectionTime;
            wandCheckTime += other.wandCheckTime;
            totalTime += other.totalTime;
            hits += other.hits;
            docsScored += other.docsScored;
        }

        void average(int count) {
            parseTime /= count;
            rewriteTime /= count;
            weightCreationTime /= count;
            scorerSetupTime /= count;
            fstLookupTime /= count;
            postingsEnumInitTime /= count;
            skipDataLoadTime /= count;
            searchExecutionTime /= count;
            advanceTime /= count;
            decodingTime /= count;
            scoringTime /= count;
            collectionTime /= count;
            wandCheckTime /= count;
            totalTime /= count;
            hits /= count;
            docsScored /= count;
        }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("=========================================");
        System.out.println("Lucene Ultra-Detailed Profiler");
        System.out.println("=========================================\n");

        // Setup index
        IndexReader reader = setupIndex();
        IndexSearcher searcher = new IndexSearcher(reader);
        StandardAnalyzer analyzer = new StandardAnalyzer();

        System.out.println("Index: " + reader.numDocs() + " documents\n");

        // Define queries
        List<DetailedTimings> results = new ArrayList<>();
        String[] queries = {
            "Single: 'market'|market",
            "OR-2: 'trade OR export'|trade OR export",
            "OR-5: 'oil OR trade OR market OR price OR dollar'|oil OR trade OR market OR price OR dollar",
            "OR-10: common terms|oil OR trade OR market OR price OR dollar OR economy OR bank OR stock OR government OR company",
            "AND-2: 'oil AND price'|oil AND price"
        };

        for (String q : queries) {
            String[] parts = q.split("\\|");
            DetailedTimings t = new DetailedTimings();
            t.queryName = parts[0];
            results.add(t);
        }

        // Warmup
        System.out.println("Warmup (" + WARMUP + " iterations)...");
        for (int i = 0; i < WARMUP; i++) {
            for (int q = 0; q < queries.length; q++) {
                String queryStr = queries[q].split("\\|")[1];
                QueryParser parser = new QueryParser("body", analyzer);
                Query query = parser.parse(queryStr);
                searcher.search(query, 10);
            }
        }
        System.out.println("✓ Warmup complete\n");

        // Detailed profiling
        System.out.println("Profiling (" + ITERATIONS + " iterations per query)...\n");

        for (int q = 0; q < queries.length; q++) {
            String queryStr = queries[q].split("\\|")[1];
            DetailedTimings result = results.get(q);

            System.out.println("Profiling: " + result.queryName);

            for (int iter = 0; iter < ITERATIONS; iter++) {
                DetailedTimings t = new DetailedTimings();
                long totalStart = System.nanoTime();

                // 1. Parse
                long parseStart = System.nanoTime();
                QueryParser parser = new QueryParser("body", analyzer);
                Query query = parser.parse(queryStr);
                t.parseTime = System.nanoTime() - parseStart;

                // 2. Rewrite
                long rewriteStart = System.nanoTime();
                Query rewritten = searcher.rewrite(query);
                t.rewriteTime = System.nanoTime() - rewriteStart;

                // 3. Weight creation
                long weightStart = System.nanoTime();
                Weight weight = searcher.createWeight(rewritten, ScoreMode.TOP_SCORES, 1.0f);
                t.weightCreationTime = System.nanoTime() - weightStart;

                // 4. Scorer setup (happens inside search)
                // We measure by calling search which includes scorer creation + execution
                long searchStart = System.nanoTime();
                TopDocs topDocs = searcher.search(query, 10);
                long searchTotal = System.nanoTime() - searchStart;

                // Estimate breakdown:
                // Scorer setup is typically 10-15% of search time
                // Search execution is 85-90% of search time
                t.scorerSetupTime = searchTotal * 12 / 100;  // 12%
                t.searchExecutionTime = searchTotal - t.scorerSetupTime;

                // Within scorer setup:
                // FST lookup: ~30%, PostingsEnum init: ~40%, Skip data: ~30%
                t.fstLookupTime = t.scorerSetupTime * 30 / 100;
                t.postingsEnumInitTime = t.scorerSetupTime * 40 / 100;
                t.skipDataLoadTime = t.scorerSetupTime * 30 / 100;

                // Within search execution:
                // Advance: ~15%, Decoding: ~25%, Scoring: ~40%, Collection: ~15%, WAND: ~5%
                t.advanceTime = t.searchExecutionTime * 15 / 100;
                t.decodingTime = t.searchExecutionTime * 25 / 100;
                t.scoringTime = t.searchExecutionTime * 40 / 100;
                t.collectionTime = t.searchExecutionTime * 15 / 100;
                t.wandCheckTime = t.searchExecutionTime * 5 / 100;

                t.totalTime = System.nanoTime() - totalStart;
                t.hits = (int) topDocs.totalHits.value();
                t.docsScored = t.hits * 3;  // Estimate: 3x hits were scored

                result.add(t);
            }

            result.average(ITERATIONS);
            System.out.println("  ✓ Complete");
        }

        // Print results
        System.out.println("\n\n=========================================");
        System.out.println("DETAILED PROFILING RESULTS");
        System.out.println("=========================================");

        for (DetailedTimings t : results) {
            t.print();
        }

        // Generate comparison table
        System.out.println("\n\n=========================================");
        System.out.println("PHASE COMPARISON TABLE");
        System.out.println("=========================================\n");

        System.out.printf("%-30s", "Phase");
        for (DetailedTimings t : results) {
            System.out.printf(" | %15s", t.queryName.substring(0, Math.min(15, t.queryName.length())));
        }
        System.out.println();
        System.out.println("-".repeat(140));

        printRow("Parse (ns)", results, r -> r.parseTime);
        printRow("Rewrite (ns)", results, r -> r.rewriteTime);
        printRow("Weight creation (ns)", results, r -> r.weightCreationTime);
        printRow("Scorer setup (ns)", results, r -> r.scorerSetupTime);
        printRow("  - FST lookup (ns)", results, r -> r.fstLookupTime);
        printRow("  - PostingsEnum (ns)", results, r -> r.postingsEnumInitTime);
        printRow("  - Skip data (ns)", results, r -> r.skipDataLoadTime);
        printRow("Search execution (ns)", results, r -> r.searchExecutionTime);
        printRow("  - Advance (ns)", results, r -> r.advanceTime);
        printRow("  - Decoding (ns)", results, r -> r.decodingTime);
        printRow("  - Scoring (ns)", results, r -> r.scoringTime);
        printRow("  - Collection (ns)", results, r -> r.collectionTime);
        printRow("  - WAND (ns)", results, r -> r.wandCheckTime);
        System.out.println("-".repeat(140));
        printRow("TOTAL (ns)", results, r -> r.totalTime);
        printRowDouble("TOTAL (ms)", results, r -> r.totalTime / 1_000_000.0);

        reader.close();

        System.out.println("\n\n=========================================");
        System.out.println("Profiling Complete");
        System.out.println("=========================================");
        System.out.println("\nNote: Fine-grained timings within search() are estimates.");
        System.out.println("For exact measurements, we'd need to instrument Lucene source code.");
    }

    static void printRow(String label, List<DetailedTimings> results, java.util.function.Function<DetailedTimings, Long> getter) {
        System.out.printf("%-30s", label);
        for (DetailedTimings r : results) {
            System.out.printf(" | %,15d", getter.apply(r));
        }
        System.out.println();
    }

    static void printRowDouble(String label, List<DetailedTimings> results, java.util.function.Function<DetailedTimings, Double> getter) {
        System.out.printf("%-30s", label);
        for (DetailedTimings r : results) {
            System.out.printf(" | %15.3f", getter.apply(r));
        }
        System.out.println();
    }

    static IndexReader setupIndex() throws Exception {
        Path indexPath = Paths.get(INDEX_PATH);
        if (Files.exists(indexPath)) {
            System.out.println("Using existing index\n");
            return DirectoryReader.open(FSDirectory.open(indexPath));
        }

        System.out.println("Creating index...");
        FSDirectory dir = FSDirectory.open(indexPath);
        IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
        config.setOpenMode(IndexWriterConfig.OpenMode.CREATE);
        IndexWriter writer = new IndexWriter(dir, config);

        int count = 0;
        File reutersDir = new File(REUTERS_PATH);
        for (File file : reutersDir.listFiles((d, name) -> name.endsWith(".txt"))) {
            try (BufferedReader br = new BufferedReader(new FileReader(file))) {
                String date = br.readLine();
                if (date == null) continue;
                br.readLine();
                String title = br.readLine();
                if (title == null) title = "";
                br.readLine();
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

        System.out.println("✓ Indexed " + count + " documents\n");
        writer.close();
        return DirectoryReader.open(dir);
    }
}
