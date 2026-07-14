// COMPACS Desktop — lightweight RAG client (vectors.bin + llama-server).
// Build: cmake -B build && cmake --build build --config Release

#include "config.hpp"
#include "format_vectors.hpp"
#include "httplib.h"
#include "webview.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef COMPACS_EMBEDDED_VECTORS
namespace compacs_embed {
extern const unsigned char kVectorsData[];
extern const std::size_t kVectorsSize;
}  // namespace compacs_embed
#endif

namespace {

using compacs::vectors::kDefaultDim;
using compacs::vectors::kMagic;
using compacs::vectors::kVersion;

// ---------------------------------------------------------------------------
// Minimal JSON helpers (no external deps)
// ---------------------------------------------------------------------------

std::string json_escape(const std::string &text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

std::optional<std::string> json_get_string(const std::string &json, const std::string &key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return std::nullopt;
    }
    ++pos;
    std::string out;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') {
            return out;
        }
        if (ch == '\\' && pos < json.size()) {
            const char esc = json[pos++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                    if (pos + 4 <= json.size()) {
                        out += '?';
                        pos += 4;
                    }
                    break;
                default: out += esc; break;
            }
        } else {
            out += ch;
        }
    }
    return std::nullopt;
}

bool json_get_bool(const std::string &json, const std::string &key, bool default_value) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return default_value;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return default_value;
    }
    const auto tail = json.substr(pos + 1, 12);
    if (tail.find("true") != std::string::npos) {
        return true;
    }
    if (tail.find("false") != std::string::npos) {
        return false;
    }
    return default_value;
}

std::optional<std::vector<double>> json_extract_embedding_array(const std::string &json) {
    const std::string key = "\"embedding\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = json.find('[', pos);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    std::vector<double> values;
    while (pos < json.size()) {
        while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ',')) {
            ++pos;
        }
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        char *end = nullptr;
        const double value = std::strtod(json.c_str() + pos, &end);
        if (end == json.c_str() + pos) {
            break;
        }
        values.push_back(value);
        pos = static_cast<std::size_t>(end - json.c_str());
    }
    if (values.empty()) {
        return std::nullopt;
    }
    return values;
}

// Set from wmain/main.
const char *&argv0_path() {
    static const char *path = "main.exe";
    return path;
}

std::filesystem::path exe_directory() {
    std::error_code ec;
    return std::filesystem::weakly_canonical(std::filesystem::path(argv0_path()), ec).parent_path();
}

// ---------------------------------------------------------------------------
// Vector store (COMPACS1 binary, 768d float32)
// ---------------------------------------------------------------------------

struct ChunkRow {
    std::string id;
    std::string source;
    int page = 0;
    std::string text;
    std::vector<double> embedding;
};

class VectorStore {
public:
    bool load_bytes(const unsigned char *data, std::size_t size, std::string *error) {
        rows_.clear();
        dim_ = 0;
        if (size < 24) {
            if (error) {
                *error = "vector blob too small";
            }
            return false;
        }
        if (std::memcmp(data, kMagic, 8) != 0) {
            if (error) {
                *error = "invalid vectors magic (expected COMPACS1)";
            }
            return false;
        }
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        std::uint32_t dim = 0;
        std::uint32_t reserved = 0;
        std::memcpy(&version, data + 8, 4);
        std::memcpy(&count, data + 12, 4);
        std::memcpy(&dim, data + 16, 4);
        std::memcpy(&reserved, data + 20, 4);
        (void)reserved;
        if (version != kVersion || dim == 0 || count == 0) {
            if (error) {
                *error = "unsupported or empty vectors header";
            }
            return false;
        }

        std::size_t offset = 24;
        rows_.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (offset + 14 > size) {
                if (error) {
                    *error = "truncated vector record header";
                }
                return false;
            }
            std::uint32_t id_num = 0;
            std::uint32_t page = 0;
            std::uint16_t source_len = 0;
            std::uint32_t text_len = 0;
            std::memcpy(&id_num, data + offset, 4);
            offset += 4;
            std::memcpy(&page, data + offset, 4);
            offset += 4;
            std::memcpy(&source_len, data + offset, 2);
            offset += 2;
            std::memcpy(&text_len, data + offset, 4);
            offset += 4;

            if (offset + source_len + text_len + dim * sizeof(float) > size) {
                if (error) {
                    *error = "truncated vector record payload";
                }
                return false;
            }

            ChunkRow row;
            row.id = std::to_string(id_num);
            row.page = static_cast<int>(page);
            row.source.assign(reinterpret_cast<const char *>(data + offset), source_len);
            offset += source_len;
            row.text.assign(reinterpret_cast<const char *>(data + offset), text_len);
            offset += text_len;
            row.embedding.resize(dim);
            for (std::uint32_t d = 0; d < dim; ++d) {
                float value = 0.0f;
                std::memcpy(&value, data + offset, sizeof(float));
                offset += sizeof(float);
                row.embedding[d] = static_cast<double>(value);
            }
            if (!row.source.empty() && !row.text.empty()) {
                rows_.push_back(std::move(row));
            }
        }

        if (rows_.empty()) {
            if (error) {
                *error = "no usable chunks in vectors blob";
            }
            return false;
        }
        dim_ = dim;
        return true;
    }

    bool load_file(const std::filesystem::path &path, std::string *error) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            if (error) {
                *error = "cannot open " + path.string();
            }
            return false;
        }
        input.seekg(0, std::ios::end);
        const auto file_size = input.tellg();
        if (file_size <= 0) {
            if (error) {
                *error = "empty vector file";
            }
            return false;
        }
        std::vector<unsigned char> bytes(static_cast<std::size_t>(file_size));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char *>(bytes.data()), file_size);
        return load_bytes(bytes.data(), bytes.size(), error);
    }

    std::size_t size() const { return rows_.size(); }
    std::size_t dim() const { return dim_; }
    const std::vector<ChunkRow> &rows() const { return rows_; }

private:
    std::vector<ChunkRow> rows_;
    std::size_t dim_ = 0;
};

struct ChunkHit {
    std::string id;
    std::string source;
    int page = 0;
    std::string text;
    double score = 0.0;
};

double cosine_distance(const std::vector<double> &a, const std::vector<double> &b) {
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

std::vector<ChunkHit> search_vectors(
    const VectorStore &store,
    const std::vector<double> &query,
    std::size_t top_k,
    double similarity_threshold) {
    struct Scored {
        double distance;
        const ChunkRow *row;
    };
    std::vector<Scored> scored;
    scored.reserve(store.rows().size());
    for (const auto &row : store.rows()) {
        const double distance = cosine_distance(query, row.embedding);
        if (distance >= similarity_threshold) {
            continue;
        }
        scored.push_back({distance, &row});
    }
    const std::size_t limit = std::min(top_k, scored.size());
    if (limit > 0) {
        std::partial_sort(
            scored.begin(),
            scored.begin() + static_cast<std::ptrdiff_t>(limit),
            scored.end(),
            [](const Scored &x, const Scored &y) { return x.distance < y.distance; });
    }
    std::vector<ChunkHit> hits;
    hits.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        ChunkHit hit;
        hit.id = scored[i].row->id;
        hit.source = scored[i].row->source;
        hit.page = scored[i].row->page;
        hit.text = scored[i].row->text;
        hit.score = 1.0 - scored[i].distance;
        hits.push_back(std::move(hit));
    }
    return hits;
}

// ---------------------------------------------------------------------------
// llama-server HTTP client (httplib)
// ---------------------------------------------------------------------------

struct LlamaConfig {
    std::string base_url = "http://127.0.0.1:8080";
    std::string embed_model = "nomic-embed-text";
    double temperature = 0.1;
    int num_predict = 512;
    std::size_t context_chunks = 6;
    std::size_t chunk_chars = 1200;
    std::size_t top_k = 6;
    double similarity_threshold = 0.7;
    std::size_t query_max_chars = 2048;
    int timeout_connect_sec = 5;
    int timeout_completion_read_sec = 300;
    int timeout_embed_read_sec = 60;
    std::string prompt_system =
        "Answer ONLY from the context chunks below. If not found, reply: NOT FOUND in documentation.\n\n";
    std::string prompt_not_found = "NOT FOUND in documentation";

    static LlamaConfig from_app(const compacs::AppConfig &app) {
        LlamaConfig cfg;
        cfg.base_url = app.llama_base_url();
        cfg.embed_model = app.embed_model;
        cfg.temperature = app.temperature;
        cfg.num_predict = app.num_predict;
        cfg.context_chunks = app.context_chunks;
        cfg.chunk_chars = app.chunk_chars;
        cfg.top_k = app.top_k;
        cfg.similarity_threshold = app.similarity_threshold;
        cfg.query_max_chars = app.query_max_chars;
        cfg.timeout_connect_sec = app.timeout_connect_sec;
        cfg.timeout_completion_read_sec = app.timeout_completion_read_sec;
        cfg.timeout_embed_read_sec = app.timeout_embed_read_sec;
        cfg.prompt_system = app.prompt_system;
        cfg.prompt_not_found = app.prompt_not_found;
        return cfg;
    }
};

bool parse_base_url(const std::string &url, std::string *host, int *port) {
    if (url.rfind("http://", 0) != 0) {
        return false;
    }
    const auto rest = url.substr(7);
    const auto colon = rest.find(':');
    const auto slash = rest.find('/');
    if (colon == std::string::npos) {
        *host = rest.substr(0, slash);
        *port = 80;
        return true;
    }
    *host = rest.substr(0, colon);
    const auto port_end = slash == std::string::npos ? rest.size() : slash;
    try {
        *port = std::stoi(rest.substr(colon + 1, port_end - colon - 1));
    } catch (...) {
        return false;
    }
    return true;
}

std::string trim_trailing_slash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string truncate_query(std::string text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    text.resize(limit);
    const auto last_period = text.rfind('.');
    if (last_period != std::string::npos && last_period > limit / 2) {
        text.resize(last_period + 1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

class LlamaClient {
public:
    explicit LlamaClient(LlamaConfig config)
        : config_(std::move(config)) {
        config_.base_url = trim_trailing_slash(config_.base_url);
        parse_base_url(config_.base_url, &host_, &port_);
    }

    bool embed(const std::string &text, std::vector<double> *out, std::string *error) const {
        const std::string trimmed = truncate_query(text, config_.query_max_chars);
        if (embed_native(trimmed, out, error)) {
            return true;
        }
        return embed_openai(trimmed, out, error);
    }

    bool completion_stream(
        const std::string &prompt,
        const std::function<bool(const std::string &token)> &on_token,
        std::string *error) const {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(config_.timeout_connect_sec, 0);
        cli.set_read_timeout(config_.timeout_completion_read_sec, 0);

        const std::string body =
            "{\"prompt\":\"" + json_escape(prompt) +
            "\",\"stream\":true,\"temperature\":" + std::to_string(config_.temperature) +
            ",\"n_predict\":" + std::to_string(config_.num_predict) + "}";

        std::string buffer;
        auto response = cli.Post(
            "/completion",
            body,
            "application/json",
            [&](const char *data, std::size_t len) {
                buffer.append(data, len);
                consume_sse(&buffer, on_token);
                return true;
            });

        if (!response) {
            if (error) {
                *error = "llama-server /completion unreachable at " + config_.base_url;
            }
            return false;
        }
        if (response->status < 200 || response->status >= 300) {
            if (error) {
                *error = "llama-server /completion HTTP " + std::to_string(response->status);
            }
            return false;
        }
        consume_sse(&buffer, on_token);
        return true;
    }

private:
    static void consume_sse(std::string *buffer, const std::function<bool(const std::string &)> &on_token) {
        while (true) {
            const auto pos = buffer->find('\n');
            if (pos == std::string::npos) {
                break;
            }
            std::string line = buffer->substr(0, pos);
            buffer->erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.rfind("data:", 0) != 0) {
                continue;
            }
            std::string payload = line.substr(5);
            while (!payload.empty() && (payload.front() == ' ' || payload.front() == '\t')) {
                payload.erase(payload.begin());
            }
            if (payload == "[DONE]") {
                continue;
            }
            if (auto token = json_get_string(payload, "content")) {
                if (!token->empty()) {
                    on_token(*token);
                }
            } else if (auto token = json_get_string(payload, "token")) {
                if (!token->empty()) {
                    on_token(*token);
                }
            }
        }
    }

    bool embed_native(const std::string &text, std::vector<double> *out, std::string *error) const {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(config_.timeout_connect_sec, 0);
        cli.set_read_timeout(config_.timeout_embed_read_sec, 0);
        const std::string body = "{\"content\":\"" + json_escape(text) + "\"}";
        auto response = cli.Post("/embedding", body, "application/json");
        if (!response || response->status >= 400) {
            response = cli.Post("/embeddings", body, "application/json");
        }
        if (!response) {
            if (error) {
                *error = "llama-server /embedding unreachable";
            }
            return false;
        }
        if (response->status < 200 || response->status >= 300) {
            return false;
        }
        if (auto values = json_extract_embedding_array(response->body)) {
            *out = std::move(*values);
            return true;
        }
        if (error) {
            *error = "cannot parse /embedding response";
        }
        return false;
    }

    bool embed_openai(const std::string &text, std::vector<double> *out, std::string *error) const {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(config_.timeout_connect_sec, 0);
        cli.set_read_timeout(config_.timeout_embed_read_sec, 0);
        const std::string body =
            "{\"input\":\"" + json_escape(text) + "\",\"model\":\"" + json_escape(config_.embed_model) +
            "\",\"encoding_format\":\"float\"}";
        const auto response = cli.Post("/v1/embeddings", body, "application/json");
        if (!response) {
            if (error) {
                *error = "llama-server /v1/embeddings unreachable";
            }
            return false;
        }
        if (response->status < 200 || response->status >= 300) {
            if (error) {
                *error = "llama-server /v1/embeddings HTTP " + std::to_string(response->status);
            }
            return false;
        }
        if (auto values = json_extract_embedding_array(response->body)) {
            *out = std::move(*values);
            return true;
        }
        if (error) {
            *error = "cannot parse /v1/embeddings response";
        }
        return false;
    }

    LlamaConfig config_;
    std::string host_;
    int port_ = 8080;
};

// ---------------------------------------------------------------------------
// Prompt builder (Llama 3 format)
// ---------------------------------------------------------------------------

std::string llama3_token(const char *name) {
    return std::string("<|") + name + "|>";
}

std::string llama3_wrap(const std::string &role, const std::string &content) {
    return llama3_token("start_header_id") + role + llama3_token("end_header_id") + "\n\n" + content +
           llama3_token("eot_id");
}

std::string build_rag_prompt(
    const std::string &question,
    const std::vector<ChunkHit> &hits,
    std::size_t chunk_char_limit,
    const std::string &system_prefix) {
    std::string context = system_prefix;
    for (const auto &hit : hits) {
        std::string clipped = hit.text;
        if (clipped.size() > chunk_char_limit) {
            clipped.resize(chunk_char_limit);
        }
        if (context.size() > system_prefix.size()) {
            context += "\n\n";
        }
        context += "[" + hit.source + ", p." + std::to_string(hit.page) + "]\n" + clipped;
    }
    return llama3_token("begin_of_text") + llama3_wrap("system", context) + llama3_wrap("user", question) +
           llama3_token("start_header_id") + "assistant" + llama3_token("end_header_id") + "\n\n";
}

// ---------------------------------------------------------------------------
// RAG controller
// ---------------------------------------------------------------------------

struct AskResult {
    std::string question;
    std::string answer;
    std::string error;
    double ms = 0.0;
    std::vector<ChunkHit> sources;
};

class RagController {
public:
    RagController(VectorStore store, LlamaConfig llama)
        : store_(std::move(store)), llama_(std::move(llama)), client_(llama_) {}

    std::size_t chunk_count() const { return store_.size(); }
    std::size_t embedding_dim() const { return store_.dim(); }
    const std::string &load_error() const { return load_error_; }
    const LlamaConfig &llama_config() const { return llama_; }

    bool ready(std::string *error = nullptr) const {
        if (!load_error_.empty()) {
            if (error) {
                *error = load_error_;
            }
            return false;
        }
        if (store_.size() == 0) {
            if (error) {
                *error = "vector store is empty";
            }
            return false;
        }
        return true;
    }

    void ask_stream(
        const std::string &question,
        const std::function<void(const std::string &)> &on_status,
        const std::function<void(const std::string &)> &on_token,
        const std::function<void(const AskResult &)> &on_done) {
        AskResult result;
        result.question = question;
        const auto started = std::chrono::steady_clock::now();

        if (question.empty()) {
            result.error = "empty question";
            on_done(result);
            return;
        }
        if (!ready(&result.error)) {
            on_done(result);
            return;
        }

        if (on_status) {
            on_status("retrieval");
        }

        std::vector<double> embedding;
        if (!client_.embed(question, &embedding, &result.error)) {
            on_done(result);
            return;
        }
        if (embedding.size() != store_.dim()) {
            result.error = "embedding dim " + std::to_string(embedding.size()) + " != index dim " +
                           std::to_string(store_.dim());
            on_done(result);
            return;
        }

        auto hits = search_vectors(store_, embedding, llama_.top_k, llama_.similarity_threshold);
        if (hits.empty()) {
            result.answer = llama_.prompt_not_found;
            if (on_token) {
                on_token(result.answer);
            }
            result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            on_done(result);
            return;
        }

        const std::size_t limit = std::min(llama_.context_chunks, hits.size());
        hits.resize(limit);
        result.sources = hits;

        if (on_status) {
            on_status("generation");
        }

        const std::string prompt =
            build_rag_prompt(question, hits, llama_.chunk_chars, llama_.prompt_system);
        std::ostringstream answer;
        std::string gen_error;
        const bool ok = client_.completion_stream(
            prompt,
            [&](const std::string &token) {
                answer << token;
                if (on_token) {
                    on_token(token);
                }
                return true;
            },
            &gen_error);

        result.answer = answer.str();
        if (!ok && result.answer.empty()) {
            result.error = gen_error.empty() ? "completion failed" : gen_error;
        }
        result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        on_done(result);
    }

    AskResult ask(const std::string &question) {
        AskResult result;
        ask_stream(
            question,
            [](const std::string &) {},
            [&](const std::string &token) { result.answer += token; },
            [&](const AskResult &done) {
                result.sources = done.sources;
                result.error = done.error;
                result.ms = done.ms;
            });
        return result;
    }

private:
    VectorStore store_;
    LlamaConfig llama_;
    LlamaClient client_;
    std::string load_error_;
};

// ---------------------------------------------------------------------------
// Local HTTP API + static UI (httplib)
// ---------------------------------------------------------------------------

class SseQueue {
public:
    void push(std::string chunk) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            chunks_.push_back(std::move(chunk));
        }
        cv_.notify_one();
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool wait_pop(std::string *out) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !chunks_.empty() || closed_;
        });
        if (chunks_.empty()) {
            return !closed_;
        }
        *out = std::move(chunks_.front());
        chunks_.pop_front();
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> chunks_;
    bool closed_ = false;
};

std::string format_sse(const std::string &event, const std::string &json_data) {
    return "event: " + event + "\ndata: " + json_data + "\n\n";
}

class ApiServer {
public:
    explicit ApiServer(RagController *controller) : controller_(controller) {}

    bool start(int port, const std::filesystem::path &assets_dir, std::string *error) {
        port_ = port;
        assets_dir_ = assets_dir;

        server_.set_pre_routing_handler([](const httplib::Request &, httplib::Response &res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            return httplib::Server::HandlerResponse::Unhandled;
        });

        server_.Options(R"(/.*)", [](const httplib::Request &, httplib::Response &res) {
            res.status = 204;
        });

        server_.Get("/health", [](const httplib::Request &, httplib::Response &res) {
            res.set_content(R"({"status":"ok"})", "application/json");
        });

        server_.Get("/api/info", [this](const httplib::Request &, httplib::Response &res) {
            if (!controller_) {
                res.status = 503;
                res.set_content(R"({"error":"controller not ready"})", "application/json");
                return;
            }
            std::string err;
            if (!controller_->ready(&err)) {
                res.status = 503;
                res.set_content("{\"error\":\"" + json_escape(err) + "\"}", "application/json");
                return;
            }
            const std::string body =
                "{\"chunks\":" + std::to_string(controller_->chunk_count()) +
                ",\"dim\":" + std::to_string(controller_->embedding_dim()) +
                ",\"llama\":\"" + json_escape(controller_->llama_config().base_url) + "\"}";
            res.set_content(body, "application/json");
        });

        server_.Post("/api/ask", [this](const httplib::Request &req, httplib::Response &res) {
            if (!controller_) {
                res.status = 503;
                res.set_content(R"({"error":"controller not ready"})", "application/json");
                return;
            }
            const auto question = json_get_string(req.body, "question");
            if (!question || question->empty()) {
                res.status = 400;
                res.set_content(R"({"error":"empty question"})", "application/json");
                return;
            }
            if (asking_.exchange(true)) {
                res.status = 429;
                res.set_content(R"({"error":"busy"})", "application/json");
                return;
            }

            const bool stream = json_get_bool(req.body, "stream", true);
            if (!stream) {
                const auto result = controller_->ask(*question);
                asking_ = false;
                std::ostringstream sources;
                sources << "[";
                for (std::size_t i = 0; i < result.sources.size(); ++i) {
                    if (i > 0) {
                        sources << ",";
                    }
                    sources << "{\"source\":\"" << json_escape(result.sources[i].source) << "\",\"page\":"
                            << result.sources[i].page << "}";
                }
                sources << "]";
                const std::string body =
                    "{\"answer\":\"" + json_escape(result.answer) + "\",\"error\":\"" + json_escape(result.error) +
                    "\",\"ms\":" + std::to_string(result.ms) + ",\"sources\":" + sources.str() + "}";
                res.set_content(body, "application/json");
                return;
            }

            auto queue = std::make_shared<SseQueue>();
            std::thread([this, question = *question, queue]() {
                controller_->ask_stream(
                    question,
                    [queue](const std::string &phase) {
                        queue->push(format_sse("status", "{\"phase\":\"" + json_escape(phase) + "\"}"));
                    },
                    [queue](const std::string &token) {
                        queue->push(format_sse("token", "{\"text\":\"" + json_escape(token) + "\"}"));
                    },
                    [this, queue](const AskResult &result) {
                        std::ostringstream sources;
                        sources << "[";
                        for (std::size_t i = 0; i < result.sources.size(); ++i) {
                            if (i > 0) {
                                sources << ",";
                            }
                            sources << "{\"source\":\"" << json_escape(result.sources[i].source) << "\",\"page\":"
                                    << result.sources[i].page << "}";
                        }
                        sources << "]";
                        const std::string payload =
                            "{\"answer\":\"" + json_escape(result.answer) + "\",\"error\":\"" +
                            json_escape(result.error) + "\",\"ms\":" + std::to_string(result.ms) +
                            ",\"sources\":" + sources.str() + "}";
                        queue->push(format_sse("done", payload));
                        queue->close();
                        asking_ = false;
                    });
            }).detach();

            res.status = 200;
            res.set_header("Cache-Control", "no-cache");
            res.set_content_provider(
                "text/event-stream",
                [queue](std::size_t, httplib::DataSink &sink) {
                    std::string chunk;
                    while (queue->wait_pop(&chunk)) {
                        if (!sink.write(chunk.c_str(), chunk.size())) {
                            return false;
                        }
                    }
                    return true;
                });
        });

        server_.Get("/", [this](const httplib::Request &, httplib::Response &res) {
            serve_index(res);
        });
        server_.Get("/index.html", [this](const httplib::Request &, httplib::Response &res) {
            serve_index(res);
        });

        thread_ = std::thread([this, port, error]() {
            if (!server_.listen("127.0.0.1", port)) {
                if (error) {
                    *error = "cannot bind UI API on port " + std::to_string(port);
                }
            }
        });

        for (int i = 0; i < 50; ++i) {
            httplib::Client probe("127.0.0.1", port);
            if (auto health = probe.Get("/health")) {
                if (health->status == 200) {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (error) {
            *error = "UI API did not become ready on port " + std::to_string(port);
        }
        return false;
    }

    void stop() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

private:
    void serve_index(httplib::Response &res) const {
        const auto path = assets_dir_ / "index.html";
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
            return;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        std::string html = buffer.str();
        const std::string base = base_url();
        constexpr const char *kToken = "__COMPACS_API_BASE__";
        for (std::size_t pos = 0; (pos = html.find(kToken, pos)) != std::string::npos;) {
            html.replace(pos, std::strlen(kToken), base);
            pos += base.size();
        }
        res.set_content(html, "text/html; charset=utf-8");
    }

    RagController *controller_ = nullptr;
    httplib::Server server_;
    std::thread thread_;
    std::atomic<bool> asking_{false};
    int port_ = 8765;
    std::filesystem::path assets_dir_;
};

bool load_vector_store(VectorStore *store, const std::string &vector_store_rel, std::string *error) {
#ifdef COMPACS_EMBEDDED_VECTORS
    if (store->load_bytes(compacs_embed::kVectorsData, compacs_embed::kVectorsSize, error)) {
        return true;
    }
#endif
    const auto exe_dir = exe_directory();
    const std::filesystem::path configured(vector_store_rel);
    const auto candidates = {
        configured.is_absolute() ? configured : (exe_dir / configured),
        exe_dir / "vectors.bin",
        exe_dir.parent_path() / "vectors.bin",
        std::filesystem::current_path() / "vectors.bin",
    };
    for (const auto &path : candidates) {
        if (std::filesystem::exists(path) && store->load_file(path, error)) {
            return true;
        }
    }
    if (error && error->empty()) {
        *error = "vectors.bin not found (embed at build time or place next to main.exe)";
    }
    return false;
}

}  // namespace

int main(int argc, char *argv[]) {
    argv0_path() = (argc > 0 && argv[0]) ? argv[0] : "main.exe";

    const auto exe_dir = exe_directory();
    compacs::AppConfig app_cfg;
    std::string cfg_error;
    if (!compacs::load_app_config(exe_dir, &app_cfg, &cfg_error)) {
        std::cerr << "Config error: " << cfg_error << "\n";
        return 1;
    }
    app_cfg.log_effective(std::cout);

    VectorStore store;
    std::string load_error;
    if (!load_vector_store(&store, app_cfg.vector_store, &load_error)) {
        std::cerr << "Vector load error: " << load_error << "\n";
    }

    const LlamaConfig llama_cfg = LlamaConfig::from_app(app_cfg);
    RagController controller(std::move(store), llama_cfg);

    const int api_port = app_cfg.ui_port;

    ApiServer api(&controller);
    std::string api_error;
    const auto assets_dir = exe_dir / app_cfg.ui_assets_dir;
    if (!api.start(api_port, assets_dir, &api_error)) {
        std::cerr << "API error: " << api_error << "\n";
        return 1;
    }

    std::string status;
    std::string ready_error;
    if (controller.ready(&ready_error)) {
        status = std::to_string(controller.chunk_count()) + " chunks, dim=" +
                 std::to_string(controller.embedding_dim()) + " | " + llama_cfg.base_url;
    } else {
        status = "Index: " + ready_error;
    }

    webview::webview app;
    app.set_title(app_cfg.ui_title.c_str());
    app.set_size(app_cfg.ui_width, app_cfg.ui_height, WEBVIEW_HINT_NONE);
    const std::string ui_base = api.base_url();
    app.init("window.COMPACS_API_BASE='" + ui_base + "';");
    app.navigate(ui_base + "/");

    std::cout << "COMPACS Desktop running at " << api.base_url() << "\n";
    std::cout << status << "\n";

    app.run();
    api.stop();
    return 0;
}
