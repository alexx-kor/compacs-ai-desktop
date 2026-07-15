#include "config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace compacs {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

std::string strip_comment(const std::string &line) {
    bool in_single = false;
    bool in_double = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
        } else if (ch == '"' && !in_single) {
            in_double = !in_double;
        } else if (ch == '#' && !in_single && !in_double) {
            return trim(line.substr(0, i));
        }
    }
    return trim(line);
}

int indent_of(const std::string &raw) {
    int n = 0;
    for (char ch : raw) {
        if (ch == ' ') {
            ++n;
        } else if (ch == '\t') {
            n += 2;
        } else {
            break;
        }
    }
    return n;
}

using YamlMap = std::map<std::string, std::string>;

struct ParseError : std::runtime_error {
    explicit ParseError(const std::string &msg) : std::runtime_error(msg) {}
};

YamlMap parse_simple_yaml(const std::string &text) {
    YamlMap out;
    std::vector<std::string> lines;
    {
        std::string body = text;
        // Strip UTF-8 BOM if present (Windows editors / PowerShell often add it).
        if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
            static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF) {
            body.erase(0, 3);
        }
        std::istringstream in(body);
        std::string raw;
        while (std::getline(in, raw)) {
            if (!raw.empty() && raw.back() == '\r') {
                raw.pop_back();
            }
            lines.push_back(raw);
        }
    }

    std::vector<std::pair<int, std::string>> stack;
    auto path_of = [&](const std::string &leaf) {
        std::string p;
        for (const auto &seg : stack) {
            if (!p.empty()) {
                p += '.';
            }
            p += seg.second;
        }
        if (!p.empty()) {
            p += '.';
        }
        p += leaf;
        return p;
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int line_no = static_cast<int>(i + 1);
        const std::string &raw = lines[i];
        const std::string line = strip_comment(raw);
        if (line.empty()) {
            continue;
        }
        const int ind = indent_of(raw);
        while (!stack.empty() && stack.back().first >= ind) {
            stack.pop_back();
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            throw ParseError("line " + std::to_string(line_no) + ": expected key: value");
        }
        const std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (key.empty()) {
            throw ParseError("line " + std::to_string(line_no) + ": empty key");
        }

        if (value == "|" || value == ">") {
            const bool keep_nl = (value == "|");
            std::string block;
            const int content_indent = ind + 2;
            std::size_t j = i + 1;
            for (; j < lines.size(); ++j) {
                const std::string &br = lines[j];
                const std::string peek = strip_comment(br);
                if (peek.empty()) {
                    const int blank_ind = indent_of(br);
                    if (blank_ind > ind || br.find_first_not_of(" \t") == std::string::npos) {
                        if (keep_nl && !block.empty()) {
                            block += '\n';
                        }
                        continue;
                    }
                }
                const int child_ind = indent_of(br);
                if (child_ind <= ind) {
                    break;
                }
                std::string content;
                if (static_cast<int>(br.size()) > content_indent) {
                    content = br.substr(static_cast<std::size_t>(content_indent));
                }
                if (!block.empty()) {
                    block += '\n';
                }
                block += content;
            }
            if (keep_nl && !block.empty() && block.back() != '\n') {
                block += '\n';
            }
            out[path_of(key)] = block;
            i = j - 1;
            continue;
        }

        if (value.empty()) {
            stack.push_back({ind, key});
            continue;
        }

        if ((value.size() >= 2) &&
            ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        out[path_of(key)] = value;
    }
    return out;
}

bool has_key(const YamlMap &m, const char *key) { return m.find(key) != m.end(); }

void log_missing(const char *key, const std::string &default_repr) {
    std::cerr << "config: param " << key << " not set, using default " << default_repr << "\n";
}

template <typename T>
void apply_int(const YamlMap &m, const char *key, T *dst, ConfigSource *src, const T &code_default) {
    if (!has_key(m, key)) {
        log_missing(key, std::to_string(static_cast<long long>(code_default)));
        return;
    }
    try {
        *dst = static_cast<T>(std::stoll(m.at(key)));
        *src = ConfigSource::Yaml;
    } catch (...) {
        throw ParseError(std::string("invalid integer for ") + key + ": " + m.at(key));
    }
}

void apply_double(const YamlMap &m, const char *key, double *dst, ConfigSource *src, double code_default) {
    if (!has_key(m, key)) {
        log_missing(key, std::to_string(code_default));
        return;
    }
    try {
        *dst = std::stod(m.at(key));
        *src = ConfigSource::Yaml;
    } catch (...) {
        throw ParseError(std::string("invalid number for ") + key + ": " + m.at(key));
    }
}

void apply_string(const YamlMap &m, const char *key, std::string *dst, ConfigSource *src,
                  const std::string &code_default) {
    if (!has_key(m, key)) {
        log_missing(key, code_default.empty() ? "(empty)" : code_default);
        return;
    }
    *dst = m.at(key);
    *src = ConfigSource::Yaml;
}

std::string getenv_or(const char *name) {
    if (const char *value = std::getenv(name); value && value[0] != '\0') {
        return value;
    }
    return {};
}

bool parse_http_url(const std::string &url, std::string *host, int *port) {
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

void apply_yaml_map(const YamlMap &m, AppConfig *cfg) {
    const AppConfig code;

    apply_string(m, "models.embed_model", &cfg->embed_model, &cfg->src_embed_model, code.embed_model);
    if (has_key(m, "models.generator")) {
        cfg->model_generator = m.at("models.generator");
    }
    if (has_key(m, "models.embedder")) {
        cfg->model_embedder = m.at("models.embedder");
    }

    ConfigSource host_src = ConfigSource::Default;
    ConfigSource port_src = ConfigSource::Default;
    apply_string(m, "server.host", &cfg->server_host, &host_src, code.server_host);
    apply_int(m, "server.port", &cfg->server_port, &port_src, code.server_port);
    if (host_src == ConfigSource::Yaml || port_src == ConfigSource::Yaml) {
        cfg->src_base_url = ConfigSource::Yaml;
    }

    apply_int(m, "server.gen_port", &cfg->server_gen_port, &cfg->src_gen_port, code.server_gen_port);
    apply_int(m, "server.embed_port", &cfg->server_embed_port, &cfg->src_embed_port, code.server_embed_port);

    apply_int(m, "server.timeouts.connect_sec", &cfg->timeout_connect_sec, &cfg->src_timeout_connect,
              code.timeout_connect_sec);
    apply_int(m, "server.timeouts.completion_read_sec", &cfg->timeout_completion_read_sec,
              &cfg->src_timeout_completion, code.timeout_completion_read_sec);
    apply_int(m, "server.timeouts.embed_read_sec", &cfg->timeout_embed_read_sec, &cfg->src_timeout_embed,
              code.timeout_embed_read_sec);

    apply_string(m, "retrieval.vector_store", &cfg->vector_store, &cfg->src_vector_store, code.vector_store);
    apply_int(m, "retrieval.top_k", &cfg->top_k, &cfg->src_top_k, code.top_k);
    apply_double(m, "retrieval.similarity_threshold", &cfg->similarity_threshold, &cfg->src_similarity_threshold,
                 code.similarity_threshold);
    apply_int(m, "retrieval.context_chunks", &cfg->context_chunks, &cfg->src_context_chunks, code.context_chunks);
    apply_int(m, "retrieval.chunk_chars", &cfg->chunk_chars, &cfg->src_chunk_chars, code.chunk_chars);
    apply_int(m, "retrieval.query_max_chars", &cfg->query_max_chars, &cfg->src_query_max_chars, code.query_max_chars);

    apply_double(m, "generation.temperature", &cfg->temperature, &cfg->src_temperature, code.temperature);
    apply_int(m, "generation.num_predict", &cfg->num_predict, &cfg->src_num_predict, code.num_predict);
    if (has_key(m, "generation.num_ctx")) {
        try {
            cfg->num_ctx = std::stoi(m.at("generation.num_ctx"));
        } catch (...) {
            throw ParseError("invalid integer for generation.num_ctx");
        }
    }

    apply_string(m, "prompt.system", &cfg->prompt_system, &cfg->src_prompt_system, code.prompt_system);
    apply_string(m, "prompt.not_found", &cfg->prompt_not_found, &cfg->src_prompt_not_found, code.prompt_not_found);

    apply_int(m, "ui.port", &cfg->ui_port, &cfg->src_ui_port, code.ui_port);
    apply_string(m, "ui.title", &cfg->ui_title, &cfg->src_ui_title, code.ui_title);
    apply_int(m, "ui.width", &cfg->ui_width, &cfg->src_ui_width, code.ui_width);
    apply_int(m, "ui.height", &cfg->ui_height, &cfg->src_ui_height, code.ui_height);
    apply_string(m, "ui.assets_dir", &cfg->ui_assets_dir, &cfg->src_ui_assets_dir, code.ui_assets_dir);
}

void apply_env(AppConfig *cfg) {
    if (const auto url = getenv_or("COMPACS_LLAMA_SERVER_URL"); !url.empty()) {
        std::string host;
        int port = 0;
        if (parse_http_url(url, &host, &port)) {
            cfg->server_host = host;
            cfg->server_port = port;
            cfg->src_base_url = ConfigSource::Env;
        } else {
            std::cerr << "config: COMPACS_LLAMA_SERVER_URL invalid, ignoring: " << url << "\n";
        }
    }
    if (const auto model = getenv_or("COMPACS_EMBED_MODEL"); !model.empty()) {
        cfg->embed_model = model;
        cfg->src_embed_model = ConfigSource::Env;
    }
    if (const auto port = getenv_or("COMPACS_UI_PORT"); !port.empty()) {
        try {
            cfg->ui_port = std::stoi(port);
            cfg->src_ui_port = ConfigSource::Env;
        } catch (...) {
            std::cerr << "config: COMPACS_UI_PORT invalid, ignoring: " << port << "\n";
        }
    }
}

}  // namespace

const char *to_string(ConfigSource source) {
    switch (source) {
        case ConfigSource::Default:
            return "default";
        case ConfigSource::Yaml:
            return "yaml";
        case ConfigSource::Env:
            return "env";
    }
    return "default";
}

void AppConfig::log_effective(std::ostream &out) const {
    out << "=== COMPACS config (effective) ===\n";
    out << "  llama_base_url = " << llama_base_url() << " (" << to_string(src_base_url) << ")  # legacy\n";
    out << "  llama_embed_url = " << llama_embed_url() << " (" << to_string(src_embed_port) << ")\n";
    out << "  llama_gen_url = " << llama_gen_url() << " (" << to_string(src_gen_port) << ")\n";
    out << "  models.embed_model = " << embed_model << " (" << to_string(src_embed_model) << ")\n";
    out << "  models.generator = " << model_generator << " (reserved)\n";
    out << "  models.embedder = " << model_embedder << " (reserved)\n";
    out << "  server.gen_port = " << server_gen_port << " (" << to_string(src_gen_port) << ")\n";
    out << "  server.embed_port = " << server_embed_port << " (" << to_string(src_embed_port) << ")\n";
    out << "  retrieval.vector_store = " << vector_store << " (" << to_string(src_vector_store) << ")\n";
    out << "  retrieval.top_k = " << top_k << " (" << to_string(src_top_k) << ")\n";
    out << "  retrieval.similarity_threshold = " << similarity_threshold << " ("
        << to_string(src_similarity_threshold) << ")  # cosine distance, keep if distance < threshold\n";
    out << "  retrieval.context_chunks = " << context_chunks << " (" << to_string(src_context_chunks) << ")\n";
    out << "  retrieval.chunk_chars = " << chunk_chars << " (" << to_string(src_chunk_chars) << ")\n";
    out << "  generation.temperature = " << temperature << " (" << to_string(src_temperature) << ")\n";
    out << "  generation.num_predict = " << num_predict << " (" << to_string(src_num_predict) << ")\n";
    out << "  generation.num_ctx = " << num_ctx << " (reserved)\n";
    out << "  timeouts.connect/completion/embed = " << timeout_connect_sec << "/"
        << timeout_completion_read_sec << "/" << timeout_embed_read_sec << " ("
        << to_string(src_timeout_connect) << "/" << to_string(src_timeout_completion) << "/"
        << to_string(src_timeout_embed) << ")\n";
    out << "  ui.port = " << ui_port << " (" << to_string(src_ui_port) << ")\n";
    out << "  ui.host = " << kUiHost << " (fixed)\n";
    out << "  ui.title = " << ui_title << " (" << to_string(src_ui_title) << ")\n";
    out << "  ui.size = " << ui_width << "x" << ui_height << " (" << to_string(src_ui_width) << "/"
        << to_string(src_ui_height) << ")\n";
    const std::string sys =
        prompt_system.size() > 48 ? prompt_system.substr(0, 45) + "..." : prompt_system;
    out << "  prompt.system = " << sys << " (" << to_string(src_prompt_system) << ")\n";
    out << "==================================\n";
}

bool write_starter_config_yaml(const std::filesystem::path &path, std::string *error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) {
            *error = "cannot write " + path.string();
        }
        return false;
    }
    out << R"yaml(# COMPACS Desktop — runtime config (env > this file > code defaults).
# Priority: COMPACS_* env vars override YAML. No secrets — offline desktop only.
#
# similarity_threshold: cosine DISTANCE (1 - similarity). Keep hit if distance < threshold.
# Same semantics as mcp-layer json_store / _similarity.py and ClickHouse cosineDistance.
# Stand config.yml value 0.35 maps 1:1 (no conversion): distance < 0.35 ⇔ similarity > 0.65.
# Desktop code default without YAML is still 0.7 (looser).

models:
  # reserved: not read by main.cpp, used by launcher
  generator: models/llama3.2-3b-instruct-q4_K_M.gguf
  embedder: models/nomic-embed-text.gguf
  # API model id for /v1/embeddings (read by main.cpp)
  embed_model: nomic-embed-text

server:
  host: 127.0.0.1
  # legacy single-URL field (log); desktop uses embed_port + gen_port
  port: 8081
  # dual-channel llama-server (offline desktop)
  gen_port: 8082
  embed_port: 8081
  timeouts:
    connect_sec: 5
    completion_read_sec: 300
    embed_read_sec: 60

retrieval:
  vector_store: vectors.bin
  # stand-aligned (mcp-layer config.yml rag.top_k)
  top_k: 12
  # cosine distance; start 0.55 (raise toward 0.7 if too many empty hits)
  similarity_threshold: 0.55
  # full chunk text into prompt (stored avg ~1335 chars)
  context_chunks: 6
  chunk_chars: 1400
  query_max_chars: 2048

generation:
  temperature: 0.1
  # stand-aligned (ollama.num_predict)
  num_predict: 400
  # reserved: not read by main.cpp, used by launcher (-c)
  num_ctx: 8192

prompt:
  system: |
    Answer ONLY from the context chunks below. If not found, reply: NOT FOUND in documentation.

  not_found: NOT FOUND in documentation

ui:
  # host is fixed to 127.0.0.1 in code (not configurable)
  port: 8765
  title: COMPACS RAG
  width: 1024
  height: 720
  assets_dir: assets
)yaml";
    return true;
}

bool load_app_config(const std::filesystem::path &exe_dir, AppConfig *out, std::string *error) {
    if (!out) {
        if (error) {
            *error = "null config";
        }
        return false;
    }
    *out = AppConfig{};
    const auto path = exe_dir / "config.yaml";

    if (!std::filesystem::exists(path)) {
        std::string write_err;
        if (!write_starter_config_yaml(path, &write_err)) {
            if (error) {
                *error = write_err;
            }
            return false;
        }
        std::cerr << "WARNING: config.yaml not found — created starter at " << path
                  << " (stand-aligned values). This session uses CODE defaults until restart.\n";
        apply_env(out);
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = "cannot open " + path.string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    try {
        apply_yaml_map(parse_simple_yaml(buffer.str()), out);
    } catch (const ParseError &ex) {
        if (error) {
            *error = std::string("config.yaml: ") + ex.what();
        }
        return false;
    } catch (const std::exception &ex) {
        if (error) {
            *error = std::string("config.yaml: ") + ex.what();
        }
        return false;
    }

    apply_env(out);
    return true;
}

}  // namespace compacs
