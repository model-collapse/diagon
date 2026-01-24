# Hybrid Query Examples

Examples combining text search with filters and aggregations.

## Text Search with Numeric Filter

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Text query
        auto query = search::TermQuery::create("title", "laptop");

        // Numeric range filter (price between $500-$1500)
        auto filter = search::RangeFilter::create("price", 500.0, 1500.0);

        // Search with filter
        auto results = searcher.search(query.get(), filter.get(), 10);

        std::cout << "Laptops priced $500-$1500:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("title")
                     << " - $" << doc->getNumeric("price")
                     << " (score: " << hit.score << ")\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Multi-Filter Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;
using namespace search;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        IndexSearcher searcher(reader.get());

        // Text query
        auto query = TermQuery::create("description", "laptop");

        // Multiple filters
        auto priceFilter = RangeFilter::create("price", 0.0, 1000.0);
        auto categoryFilter = TermFilter::create("category", "electronics");
        auto ratingFilter = RangeFilter::createMin("rating", 4.0);

        // Combine filters (all must match)
        auto combinedFilter = BooleanFilter::create(
            {priceFilter.get(), categoryFilter.get(), ratingFilter.get()},
            BooleanClause::MUST
        );

        // Search
        auto results = searcher.search(query.get(), combinedFilter.get(), 20);

        std::cout << "Electronics laptops under $1000 with rating >= 4.0:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("title")
                     << " - $" << doc->getNumeric("price")
                     << " (" << doc->getNumeric("rating") << "★)\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Faceted Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>
#include <map>
#include <set>

using namespace diagon;

// Collect facet counts
std::map<std::string, int> collectFacets(
    search::IndexSearcher& searcher,
    search::Query* query,
    const std::string& facet_field)
{
    std::map<std::string, int> counts;

    // Get all results
    auto results = searcher.search(query, 10000);

    // Count field values
    for (const auto& hit : results.scoreDocs) {
        auto doc = searcher.doc(hit.doc, {facet_field});
        std::string value = doc->get(facet_field);
        counts[value]++;
    }

    return counts;
}

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Base query
        auto query = search::TermQuery::create("title", "laptop");
        auto results = searcher.search(query.get(), 10);

        std::cout << "=== Search Results ===\n";
        std::cout << "Found " << results.totalHits << " laptops\n\n";

        // Show top results
        for (size_t i = 0; i < std::min(size_t(5), results.scoreDocs.size()); i++) {
            auto doc = searcher.doc(results.scoreDocs[i].doc);
            std::cout << (i + 1) << ". " << doc->get("title")
                     << " - $" << doc->getNumeric("price") << "\n";
        }

        // Show facets
        std::cout << "\n=== Filter by Category ===\n";
        auto categoryFacets = collectFacets(searcher, query.get(), "category");
        for (const auto& [category, count] : categoryFacets) {
            std::cout << category << " (" << count << ")\n";
        }

        std::cout << "\n=== Filter by Brand ===\n";
        auto brandFacets = collectFacets(searcher, query.get(), "brand");
        for (const auto& [brand, count] : brandFacets) {
            std::cout << brand << " (" << count << ")\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Time-Range Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>
#include <chrono>

using namespace diagon;

int64_t daysAgo(int days) {
    auto now = std::chrono::system_clock::now();
    auto then = now - std::chrono::hours(24 * days);
    return std::chrono::duration_cast<std::chrono::seconds>(
        then.time_since_epoch()).count();
}

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/log_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Text query
        auto query = search::TermQuery::create("message", "error");

        // Time range filters
        auto last24h = search::RangeFilter::createMin("timestamp", daysAgo(1));
        auto last7days = search::RangeFilter::createMin("timestamp", daysAgo(7));
        auto last30days = search::RangeFilter::createMin("timestamp", daysAgo(30));

        // Search different time ranges
        std::cout << "=== Error Logs ===\n\n";

        auto results = searcher.search(query.get(), last24h.get(), 100);
        std::cout << "Last 24 hours: " << results.totalHits << " errors\n";

        results = searcher.search(query.get(), last7days.get(), 100);
        std::cout << "Last 7 days: " << results.totalHits << " errors\n";

        results = searcher.search(query.get(), last30days.get(), 100);
        std::cout << "Last 30 days: " << results.totalHits << " errors\n";

        // Show recent errors
        std::cout << "\n=== Recent Errors ===\n";
        results = searcher.search(query.get(), last24h.get(), 5);

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("message") << "\n";
            std::cout << "  Level: " << doc->get("level") << "\n";
            std::cout << "  Logger: " << doc->get("logger") << "\n\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## E-Commerce Product Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>
#include <vector>
#include <string>

using namespace diagon;
using namespace search;

struct SearchFilters {
    std::string category;
    double min_price = 0.0;
    double max_price = std::numeric_limits<double>::max();
    double min_rating = 0.0;
    std::vector<std::string> brands;
    bool in_stock_only = false;
};

std::unique_ptr<Filter> buildFilter(const SearchFilters& filters) {
    std::vector<Filter*> filterList;

    // Category filter
    auto categoryFilter = TermFilter::create("category", filters.category);
    filterList.push_back(categoryFilter.get());

    // Price range filter
    auto priceFilter = RangeFilter::create("price",
        filters.min_price, filters.max_price);
    filterList.push_back(priceFilter.get());

    // Rating filter
    if (filters.min_rating > 0.0) {
        auto ratingFilter = RangeFilter::createMin("rating", filters.min_rating);
        filterList.push_back(ratingFilter.get());
    }

    // Brand filter (OR of multiple brands)
    if (!filters.brands.empty()) {
        std::vector<Filter*> brandFilters;
        std::vector<std::unique_ptr<Filter>> brandFilterStorage;

        for (const auto& brand : filters.brands) {
            brandFilterStorage.push_back(TermFilter::create("brand", brand));
            brandFilters.push_back(brandFilterStorage.back().get());
        }

        auto brandFilter = BooleanFilter::create(brandFilters,
                                                 BooleanClause::SHOULD);
        filterList.push_back(brandFilter.get());
    }

    // Stock filter
    if (filters.in_stock_only) {
        auto stockFilter = RangeFilter::createMin("stock", 1);
        filterList.push_back(stockFilter.get());
    }

    // Combine all filters (AND)
    return BooleanFilter::create(filterList, BooleanClause::MUST);
}

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/products_index");
        auto reader = index::DirectoryReader::open(dir.get());
        IndexSearcher searcher(reader.get());

        // Search query
        auto query = TermQuery::create("description", "wireless");

        // Build filters
        SearchFilters filters;
        filters.category = "electronics";
        filters.min_price = 50.0;
        filters.max_price = 500.0;
        filters.min_rating = 4.0;
        filters.brands = {"Apple", "Samsung", "Sony"};
        filters.in_stock_only = true;

        auto filter = buildFilter(filters);

        // Execute search
        auto results = searcher.search(query.get(), filter.get(), 20);

        std::cout << "=== Wireless Electronics ===\n";
        std::cout << "Price: $" << filters.min_price
                 << " - $" << filters.max_price << "\n";
        std::cout << "Rating: " << filters.min_rating << "+\n";
        std::cout << "Brands: ";
        for (const auto& brand : filters.brands) {
            std::cout << brand << " ";
        }
        std::cout << "\nIn stock only\n\n";

        std::cout << "Found " << results.totalHits << " products:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("title") << "\n";
            std::cout << "  Brand: " << doc->get("brand") << "\n";
            std::cout << "  Price: $" << doc->getNumeric("price") << "\n";
            std::cout << "  Rating: " << doc->getNumeric("rating") << "★\n";
            std::cout << "  Stock: " << doc->getNumericInt("stock") << "\n\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Geospatial Search (Future Work)

```cpp
// Future API for geospatial search
#include <diagon/search/IndexSearcher.h>
#include <diagon/search/GeoFilter.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/places_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Text query
        auto query = search::TermQuery::create("type", "restaurant");

        // Geospatial filter (within 5km of location)
        auto geoFilter = search::GeoFilter::createRadius(
            "location",
            37.7749,  // latitude
            -122.4194,  // longitude (San Francisco)
            5000.0  // radius in meters
        );

        // Search
        auto results = searcher.search(query.get(), geoFilter.get(), 20);

        std::cout << "Restaurants within 5km:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("name") << "\n";
            std::cout << "  Address: " << doc->get("address") << "\n";
            std::cout << "  Distance: " << hit.distance << " meters\n\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Analytical Query (Aggregations - Future Work)

```cpp
// Future API for aggregations
#include <diagon/search/IndexSearcher.h>
#include <diagon/search/Aggregations.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;
using namespace search;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/sales_index");
        auto reader = index::DirectoryReader::open(dir.get());
        IndexSearcher searcher(reader.get());

        // Base query (all sales)
        auto query = MatchAllQuery::create();

        // Build aggregations
        auto aggs = Aggregations::Builder()
            // Sum of revenue
            .sum("total_revenue", "amount")

            // Average order value
            .avg("avg_order_value", "amount")

            // Count by category
            .terms("sales_by_category", "category", 10)

            // Revenue over time (histogram)
            .dateHistogram("revenue_by_month", "timestamp", "1M")

            .build();

        // Execute with aggregations
        auto results = searcher.searchWithAggregations(
            query.get(), aggs.get(), 0);  // 0 docs, just aggs

        // Display aggregation results
        std::cout << "=== Sales Analysis ===\n\n";

        std::cout << "Total Revenue: $"
                 << results.getAggregation("total_revenue")->asSum()
                 << "\n";

        std::cout << "Average Order Value: $"
                 << results.getAggregation("avg_order_value")->asAvg()
                 << "\n\n";

        std::cout << "Sales by Category:\n";
        auto categoryAgg = results.getAggregation("sales_by_category")->asTerms();
        for (const auto& bucket : categoryAgg->buckets()) {
            std::cout << "  " << bucket.key
                     << ": " << bucket.docCount << " sales\n";
        }

        std::cout << "\nRevenue by Month:\n";
        auto timeAgg = results.getAggregation("revenue_by_month")->asHistogram();
        for (const auto& bucket : timeAgg->buckets()) {
            std::cout << "  " << bucket.keyAsDate()
                     << ": $" << bucket.docCount << "\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## See Also

- [Text Search Examples](text-search.md)
- [Searching Guide](../guides/searching.md)
- [Performance Guide](../guides/performance.md)
- [Search API Reference](../api/search.md)
