// COMPACS Desktop — runtime config (env > config.yaml > code defaults).
#pragma once

#include <cstddef>
#include <filesystem>
#include <ostream>
#include <string>

namespace compacs {

enum class ConfigSource { Default, Yaml, Env };

const char *to_string(ConfigSource source);

struct Sourced {
    ConfigSource source = ConfigSource::Default;
};

struct AppConfig {
    // --- models (embed_model used by main; paths reserved for launcher) ---
    std::string model_generator = "models/llama3.2-3b-instruct-q4_K_M.gguf";
    std::string model_embedder = "models/nomic-embed-text.gguf";
    std::string embed_model = "nomic-embed-text";
    ConfigSource src_embed_model = ConfigSource::Default;

    // --- server (dual-channel: embed_port + gen_port) ---
    std::string server_host = "127.0.0.1";
    int server_port = 8080;         // legacy / log; prefer embed_port + gen_port
    int server_gen_port = 8082;     // /completion
    int server_embed_port = 8081;   // /embedding
    ConfigSource src_base_url = ConfigSource::Default;
    ConfigSource src_gen_port = ConfigSource::Default;
    ConfigSource src_embed_port = ConfigSource::Default;

    int timeout_connect_sec = 5;
    int timeout_completion_read_sec = 300;
    int timeout_embed_read_sec = 60;
    ConfigSource src_timeout_connect = ConfigSource::Default;
    ConfigSource src_timeout_completion = ConfigSource::Default;
    ConfigSource src_timeout_embed = ConfigSource::Default;

    // --- retrieval (hybrid_dense_3b_cpu profile) ---
    std::string vector_store = "vectors.bin";
    std::string lemma_map = "lemma_map.tsv";
    std::size_t top_k = 12;
    std::size_t rerank_top_k = 8;
    double similarity_threshold = 0.30;
    std::size_t context_chunks = 6;
    std::size_t chunk_chars = 800;
    std::size_t query_max_chars = 2048;
    bool hybrid_enabled = true;
    bool rerank_enabled = true;
    bool collection_routing_enabled = true;
    bool map_reduce_enabled = false;
    std::size_t map_reduce_max_chunks = 6;
    std::size_t map_chunk_chars = 400;
    int map_num_predict = 120;
    ConfigSource src_vector_store = ConfigSource::Default;
    ConfigSource src_lemma_map = ConfigSource::Default;
    ConfigSource src_top_k = ConfigSource::Default;
    ConfigSource src_rerank_top_k = ConfigSource::Default;
    ConfigSource src_similarity_threshold = ConfigSource::Default;
    ConfigSource src_context_chunks = ConfigSource::Default;
    ConfigSource src_chunk_chars = ConfigSource::Default;
    ConfigSource src_query_max_chars = ConfigSource::Default;
    ConfigSource src_hybrid_enabled = ConfigSource::Default;
    ConfigSource src_rerank_enabled = ConfigSource::Default;
    ConfigSource src_collection_routing_enabled = ConfigSource::Default;
    ConfigSource src_map_reduce_enabled = ConfigSource::Default;

    // --- generation ---
    double temperature = 0.1;
    int num_predict = 250;
    int num_ctx = 16384;  // reserved (launcher -c)
    ConfigSource src_temperature = ConfigSource::Default;
    ConfigSource src_num_predict = ConfigSource::Default;

    // --- prompt ---
    std::string prompt_system =
        "Отвечай ТОЛЬКО по фрагментам контекста ниже и только на русском языке.\n"
        "Структурируй ответ кратко: 2–5 предложений по сути вопроса.\n"
        "Если в контексте нет ответа — напиши ровно: NOT FOUND in documentation.\n"
        "Не выдумывай факты. В конце укажи источники: файл и страница.\n\n";
    std::string prompt_not_found = "NOT FOUND in documentation";
    ConfigSource src_prompt_system = ConfigSource::Default;
    ConfigSource src_prompt_not_found = ConfigSource::Default;

    // --- ui (host fixed 127.0.0.1) ---
    static constexpr const char *kUiHost = "127.0.0.1";
    int ui_port = 8765;
    std::string ui_title = "COMPACS RAG";
    int ui_width = 1024;
    int ui_height = 720;
    std::string ui_assets_dir = "assets";
    ConfigSource src_ui_port = ConfigSource::Default;
    ConfigSource src_ui_title = ConfigSource::Default;
    ConfigSource src_ui_width = ConfigSource::Default;
    ConfigSource src_ui_height = ConfigSource::Default;
    ConfigSource src_ui_assets_dir = ConfigSource::Default;

    // Resolved at load (for diagnostics / build_info).
    std::string config_path;
    std::string exe_dir;

    std::string llama_base_url() const {
        return "http://" + server_host + ":" + std::to_string(server_port);
    }

    std::string llama_embed_url() const {
        return "http://" + server_host + ":" + std::to_string(server_embed_port);
    }

    std::string llama_gen_url() const {
        return "http://" + server_host + ":" + std::to_string(server_gen_port);
    }

    void log_effective(std::ostream &out) const;
};

// Writes stand-aligned starter YAML (comments in Russian/English).
bool write_starter_config_yaml(const std::filesystem::path &path, std::string *error);

// Load: missing file → write starter, keep code defaults; present → merge yaml;
// then apply env (COMPACS_LLAMA_SERVER_URL, COMPACS_EMBED_MODEL, COMPACS_UI_PORT).
// On YAML syntax error returns false and fills *error (with line number when possible).
bool load_app_config(const std::filesystem::path &exe_dir, AppConfig *out, std::string *error);

}  // namespace compacs
