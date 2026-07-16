// Hybrid retrieval: dense cosine + BM25 + weighted RRF fusion + section boost.
#pragma once

#include "lemma_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace compacs::rag {

struct RagChunk {
    std::string id;
    std::string source;
    int page = 0;
    std::string text;
    const std::vector<double> *embedding = nullptr;
};

struct RagHit {
    std::string id;
    std::string source;
    int page = 0;
    std::string text;
    double score = 0.0;
};

struct HybridConfig {
    std::size_t top_k = 12;
    double similarity_threshold = 0.30;
    bool enabled = true;
    double dense_weight = 0.65;
    double bm25_weight = 0.20;
    double rrf_weight = 0.15;
    double bm25_k1 = 1.2;
    double bm25_b = 0.75;
    int rrf_k = 60;
};

inline bool is_word_char(unsigned char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           (ch >= 0xC0);  // UTF-8 lead byte — keep Cyrillic runs
}

inline std::vector<std::string> tokenize(const std::string &text, const LemmaMap *lemmas) {
    std::vector<std::string> tokens;
    std::string current;
    auto flush = [&]() {
        if (current.size() >= 2) {
            if (lemmas) {
                current = lemmas->normalize_token(current);
            }
            tokens.push_back(current);
        }
        current.clear();
    };
    for (unsigned char ch : text) {
        if (is_word_char(ch)) {
            current += static_cast<char>(ch);
        } else if (!current.empty()) {
            flush();
        }
    }
    flush();
    return tokens;
}

inline double cosine_distance(const std::vector<double> &a, const std::vector<double> &b) {
    if (a.size() != b.size() || a.empty()) {
        return 1.0;
    }
    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    if (norm_a == 0.0 || norm_b == 0.0) {
        return 1.0;
    }
    return 1.0 - dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

class Bm25Index {
public:
    void build(const std::vector<RagChunk> &chunks, const LemmaMap *lemmas) {
        docs_.clear();
        doc_freq_.clear();
        avg_len_ = 0.0;
        docs_.reserve(chunks.size());
        double total_len = 0.0;
        for (const auto &chunk : chunks) {
            Doc doc;
            doc.tokens = tokenize(chunk.text + " " + chunk.source, lemmas);
            total_len += static_cast<double>(doc.tokens.size());
            for (const auto &tok : doc.tokens) {
                ++doc.term_freq[tok];
            }
            for (const auto &kv : doc.term_freq) {
                (void)kv;
                ++doc_freq_[kv.first];
            }
            docs_.push_back(std::move(doc));
        }
        if (!docs_.empty()) {
            avg_len_ = total_len / static_cast<double>(docs_.size());
        }
    }

    double score(std::size_t doc_idx, const std::vector<std::string> &query_tokens, double k1, double b) const {
        if (doc_idx >= docs_.size() || query_tokens.empty()) {
            return 0.0;
        }
        const auto &doc = docs_[doc_idx];
        const double doc_len = static_cast<double>(doc.tokens.size());
        const double N = static_cast<double>(docs_.size());
        double total = 0.0;
        for (const auto &term : query_tokens) {
            const auto tf_it = doc.term_freq.find(term);
            if (tf_it == doc.term_freq.end()) {
                continue;
            }
            const auto df_it = doc_freq_.find(term);
            const double df = (df_it == doc_freq_.end()) ? 0.0 : static_cast<double>(df_it->second);
            const double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);
            const double tf = static_cast<double>(tf_it->second);
            const double denom = tf + k1 * (1.0 - b + b * doc_len / std::max(avg_len_, 1.0));
            total += idf * (tf * (k1 + 1.0)) / denom;
        }
        return total;
    }

    std::size_t size() const { return docs_.size(); }

private:
    struct Doc {
        std::vector<std::string> tokens;
        std::unordered_map<std::string, int> term_freq;
    };
    std::vector<Doc> docs_;
    std::unordered_map<std::string, int> doc_freq_;
    double avg_len_ = 0.0;
};

inline double section_boost(const std::string &question, const std::string &text) {
    static const std::pair<const char *, const char *> rules[] = {
        {"§7.1", "§7.1"},
        {"§8.1", "§8.1"},
        {"7.1", "§7.1"},
        {"8.1", "§8.1"},
        {"терминал", "§7"},
        {"монитор", "монитор"},
        {"синхрон", "синхрон"},
        {"прибор", "прибор"},
    };
    for (const auto &[qneedle, tneedle] : rules) {
        if (question.find(qneedle) != std::string::npos && text.find(tneedle) != std::string::npos) {
            return 1.25;
        }
    }
    return 1.0;
}

inline std::vector<RagHit> hybrid_retrieve(
    const std::vector<RagChunk> &chunks,
    const Bm25Index &bm25,
    const std::vector<double> &query_embedding,
    const std::string &question,
    const LemmaMap *lemmas,
    const HybridConfig &cfg) {
    if (chunks.empty()) {
        return {};
    }

    const auto query_tokens = tokenize(question, lemmas);

    struct Scored {
        std::size_t idx;
        double dense = 0.0;
        double bm25 = 0.0;
        double rrf = 0.0;
        double final = 0.0;
    };
    std::vector<Scored> pool;
    pool.reserve(chunks.size());

    std::vector<std::pair<double, std::size_t>> dense_ranked;
    std::vector<std::pair<double, std::size_t>> bm25_ranked;

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto &chunk = chunks[i];
        Scored row{i, 0.0, 0.0, 0.0, 0.0};
        if (chunk.embedding && query_embedding.size() == chunk.embedding->size()) {
            const double dist = cosine_distance(query_embedding, *chunk.embedding);
            if (dist < cfg.similarity_threshold) {
                row.dense = 1.0 - dist;
                dense_ranked.emplace_back(row.dense, i);
            }
        }
        if (cfg.enabled && bm25.size() == chunks.size()) {
            row.bm25 = bm25.score(i, query_tokens, cfg.bm25_k1, cfg.bm25_b);
            if (row.bm25 > 0.0) {
                bm25_ranked.emplace_back(row.bm25, i);
            }
        }
        pool.push_back(row);
    }

    auto sort_desc = [](auto &v) {
        std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    };
    sort_desc(dense_ranked);
    sort_desc(bm25_ranked);

    std::unordered_map<std::size_t, int> dense_rank;
    std::unordered_map<std::size_t, int> bm25_rank;
    for (std::size_t r = 0; r < dense_ranked.size(); ++r) {
        dense_rank[dense_ranked[r].second] = static_cast<int>(r) + 1;
    }
    for (std::size_t r = 0; r < bm25_ranked.size(); ++r) {
        bm25_rank[bm25_ranked[r].second] = static_cast<int>(r) + 1;
    }

    const double dense_max = dense_ranked.empty() ? 1.0 : dense_ranked.front().first;
    const double bm25_max = bm25_ranked.empty() ? 1.0 : bm25_ranked.front().first;

    for (auto &row : pool) {
        const double dense_norm = dense_max > 0.0 ? row.dense / dense_max : 0.0;
        const double bm25_norm = bm25_max > 0.0 ? row.bm25 / bm25_max : 0.0;
        double rrf = 0.0;
        if (const auto it = dense_rank.find(row.idx); it != dense_rank.end()) {
            rrf += 1.0 / (cfg.rrf_k + it->second);
        }
        if (const auto it = bm25_rank.find(row.idx); it != bm25_rank.end()) {
            rrf += 1.0 / (cfg.rrf_k + it->second);
        }
        row.rrf = rrf;
        if (cfg.enabled) {
            row.final = cfg.dense_weight * dense_norm + cfg.bm25_weight * bm25_norm + cfg.rrf_weight * row.rrf;
        } else {
            row.final = dense_norm;
        }
        row.final *= section_boost(question, chunks[row.idx].text + " " + chunks[row.idx].source);
    }

    std::sort(pool.begin(), pool.end(), [](const Scored &a, const Scored &b) { return a.final > b.final; });

    const std::size_t limit = std::min(cfg.top_k, pool.size());
    std::vector<RagHit> hits;
    hits.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        if (pool[i].final <= 0.0) {
            continue;
        }
        const auto &chunk = chunks[pool[i].idx];
        hits.push_back(RagHit{chunk.id, chunk.source, chunk.page, chunk.text, pool[i].final});
    }
    return hits;
}

}  // namespace compacs::rag
