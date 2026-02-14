import org.apache.lucene.index.*;
import org.apache.lucene.store.*;
import org.apache.lucene.analysis.standard.*;
import java.nio.file.*;
import java.lang.reflect.*;

public class CheckThreads {
    public static void main(String[] args) throws Exception {
        Path indexPath = Paths.get("/tmp/test_threads");
        FSDirectory dir = FSDirectory.open(indexPath);
        StandardAnalyzer analyzer = new StandardAnalyzer();
        IndexWriterConfig config = new IndexWriterConfig(analyzer);
        
        System.out.println("IndexWriterConfig settings:");
        System.out.println("  RAM buffer size: " + config.getRAMBufferSizeMB() + " MB");
        System.out.println("  Available processors: " + Runtime.getRuntime().availableProcessors());
        
        // List all public methods to find the right one
        System.out.println("\nAvailable methods:");
        for (Method m : config.getClass().getMethods()) {
            if (m.getName().contains("thread") || m.getName().contains("Thread") || m.getName().contains("concurrent")) {
                System.out.println("  " + m.getName() + "()");
            }
        }
        
        IndexWriter writer = new IndexWriter(dir, config);
        System.out.println("\nIndexWriter created");
        writer.close();
    }
}
