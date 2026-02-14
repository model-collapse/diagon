// Check if Lucene generates impacts for Reuters terms

import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.search.similarities.BM25Similarity;
import org.apache.lucene.store.ByteBuffersDirectory;
import org.apache.lucene.store.Directory;
import org.apache.lucene.util.BytesRef;

import java.io.IOException;
import java.nio.file.*;
import java.util.*;
import java.util.stream.Collectors;

public class CheckLuceneImpacts {

    private static final String REUTERS_PATH = "/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out";

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

    private static Directory createReutersIndex() throws IOException {
        Directory dir = new ByteBuffersDirectory();
        IndexWriterConfig config = new IndexWriterConfig(new StandardAnalyzer());
        config.setRAMBufferSizeMB(128.0);
        config.setSimilarity(new BM25Similarity());
        IndexWriter writer = new IndexWriter(dir, config);

        System.out.println("Creating Lucene index from Reuters-21578...");
        List<Path> files = Files.walk(Paths.get(REUTERS_PATH))
            .filter(p -> p.toString().endsWith(".txt"))
            .collect(Collectors.toList());

        int indexed = 0;
        for (Path filepath : files) {
            try {
                String text = loadReutersDocument(filepath.toString());
                Document doc = new Document();
                doc.add(new TextField("body", text, Field.Store.NO));
                writer.addDocument(doc);
                indexed++;
            } catch (Exception e) {}
        }

        System.out.println("Indexed " + indexed + " documents");
        writer.commit();
        writer.close();
        return dir;
    }

    public static void main(String[] args) throws IOException {
        Directory dir = createReutersIndex();
        DirectoryReader reader = DirectoryReader.open(dir);

        String[] queryTerms = {"market", "company", "stock", "trade", "price"};

        System.out.println("\n=== Lucene Impacts Check ===\n");
        System.out.println("Lucene BLOCK_SIZE = 256 (Level 0 impacts every 256 docs)");
        System.out.println("Lucene LEVEL1_NUM_DOCS = 8,192 (Level 1 impacts every 8,192 docs)\n");

        for (LeafReaderContext context : reader.leaves()) {
            System.out.println("Segment " + context.ord + ": " + context.reader().maxDoc() + " docs");
            Terms terms = context.reader().terms("body");
            if (terms == null) continue;

            TermsEnum termsEnum = terms.iterator();

            for (String queryTerm : queryTerms) {
                if (termsEnum.seekExact(new BytesRef(queryTerm))) {
                    int docFreq = termsEnum.docFreq();

                    // Check if Lucene would generate impacts
                    boolean hasLevel0 = docFreq >= 256;  // Level 0 skip data
                    boolean hasLevel1 = docFreq >= 8192; // Level 1 skip data

                    System.out.print("  '" + queryTerm + "': docFreq=" + docFreq);
                    if (hasLevel1) {
                        System.out.println(" ✅✅ (Level 0 + Level 1 impacts)");
                    } else if (hasLevel0) {
                        System.out.println(" ✅ (Level 0 impacts only)");
                    } else {
                        System.out.println(" ❌ (NO impacts - too few docs)");
                    }
                }
            }
            System.out.println();
        }

        reader.close();
    }
}
