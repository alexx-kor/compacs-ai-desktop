// RAG pipeline: collection routing, hybrid retrieve, lexical rerank.
#pragma once

#include "hybrid_search.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace compacs::rag {

struct PipelineConfig {
    HybridConfig hybrid;
    bool rerank_enabled = true;
    bool collection_routing_enabled = true;
    std::size_t rerank_top_k = 8;
    std::size_t context_chunks = 6;
};

inline std::vector<std::string> route_collections(const std::string &question) {
    static const std::pair<const char *, std::vector<const char *>> routes[] = {
        {"1_OG_1", {"монитор", "терминал", "установ", "§7", "§8", "экран", "запуск"}},
        {"2_OG_1", {"график", "спектр", "анализ", "диаграм", "тренд"}},
        {"3_OG_1", {"прибор", "синхрон", "конфигур", "адрес", "субъект", "панель"}},
    };
    std::vector<std::string> matched;
    std::string lower = question;
    for (char &ch : lower) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    for (const auto &route : routes) {
        for (const char *kw : route.second) {
            if (lower.find(kw) != std::string::npos) {
                matched.emplace_back(route.first);
                break;
            }
        }
    }
    return matched;
}

inline bool source_matches_route(const std::string &source, const std::vector<std::string> &routes) {
    if (routes.empty()) {
        return true;
    }
    for (const auto &prefix : routes) {
        if (source.find(prefix) != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline double token_overlap_score(const std::vector<std::string> &query_tokens, const std::string &text,
                                  const LemmaMap *lemmas) {
    if (query_tokens.empty()) {
        return 0.0;
    }
    const auto doc_tokens = tokenize(text, lemmas);
    std::unordered_set<std::string> doc_set(doc_tokens.begin(), doc_tokens.end());
    std::size_t overlap = 0;
    for (const auto &tok : query_tokens) {
        if (doc_set.count(tok)) {
            ++overlap;
        }
    }
    return static_cast<double>(overlap) / static_cast<double>(query_tokens.size());
}

inline std::vector<RagHit> rerank_hits(
    const std::string &question,
    std::vector<RagHit> hits,
    const LemmaMap *lemmas,
    std::size_t rerank_top_k) {
    if (hits.empty()) {
        return hits;
    }
    const auto query_tokens = tokenize(question, lemmas);
    for (auto &hit : hits) {
        const double overlap = token_overlap_score(query_tokens, hit.text + " " + hit.source, lemmas);
        hit.score = hit.score * 0.6 + overlap * 0.4;
    }
    std::sort(hits.begin(), hits.end(), [](const RagHit &a, const RagHit &b) { return a.score > b.score; });
    if (hits.size() > rerank_top_k) {
        hits.resize(rerank_top_k);
    }
    return hits;
}

class RagPipeline {
public:
    void set_chunks(std::vector<RagChunk> chunks, const LemmaMap *lemmas) {
        chunks_ = std::move(chunks);
        bm25_.build(chunks_, lemmas);
    }

    std::vector<RagHit> retrieve(
        const std::vector<double> &query_embedding,
        const std::string &question,
        const LemmaMap *lemmas,
        const PipelineConfig &cfg) const {
        std::vector<RagChunk> scoped;
        scoped.reserve(chunks_.size());
        std::vector<std::string> routes;
        if (cfg.collection_routing_enabled) {
            routes = route_collections(question);
        }
        for (const auto &chunk : chunks_) {
            if (source_matches_route(chunk.source, routes)) {
                scoped.push_back(chunk);
            }
        }
        if (scoped.empty()) {
            scoped = chunks_;
        }

        Bm25Index scoped_bm25;
        if (cfg.hybrid.enabled) {
            scoped_bm25.build(scoped, lemmas);
        }

        auto hits = hybrid_retrieve(scoped, scoped_bm25, query_embedding, question, lemmas, cfg.hybrid);
        if (cfg.rerank_enabled && !hits.empty()) {
            hits = rerank_hits(question, std::move(hits), lemmas, cfg.rerank_top_k);
        } else if (hits.size() > cfg.rerank_top_k) {
            hits.resize(cfg.rerank_top_k);
        }
        return hits;
    }

private:
    std::vector<RagChunk> chunks_;
    Bm25Index bm25_;
};

}  // namespace compacs::rag
