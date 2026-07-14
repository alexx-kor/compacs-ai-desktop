// export_vectors — convert chunks.json to COMPACS1 vectors.bin (format_vectors.hpp).
#include "../../format_vectors.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using compacs::vectors::kDefaultDim;
using compacs::vectors::kMagic;
using compacs::vectors::kVersion;

std::string read_file(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
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

std::optional<std::int64_t> json_get_int(const std::string &json, const std::string &key) {
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
    if (pos >= json.size()) {
        return std::nullopt;
    }
    char *end = nullptr;
    const auto value = std::strtoll(json.c_str() + pos, &end, 10);
    if (end == json.c_str() + pos) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::vector<float>> json_extract_embedding_array(const std::string &json) {
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
    std::vector<float> values;
    values.reserve(kDefaultDim);
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
        values.push_back(static_cast<float>(value));
        pos = static_cast<std::size_t>(end - json.c_str());
    }
    if (values.empty()) {
        return std::nullopt;
    }
    return values;
}

std::vector<std::string> extract_row_objects(const std::string &json) {
    const std::string key = "\"rows\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) {
        throw std::runtime_error("chunks.json: missing \"rows\" array");
    }
    pos = json.find('[', pos);
    if (pos == std::string::npos) {
        throw std::runtime_error("chunks.json: malformed \"rows\" array");
    }
    ++pos;

    std::vector<std::string> rows;
    while (pos < json.size()) {
        while (pos < json.size()) {
            const char ch = json[pos];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',') {
                ++pos;
                continue;
            }
            break;
        }
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        if (json[pos] != '{') {
            throw std::runtime_error("chunks.json: expected object in rows array");
        }
        const std::size_t start = pos;
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        for (; pos < json.size(); ++pos) {
            const char ch = json[pos];
            if (in_string) {
                if (escape) {
                    escape = false;
                } else if (ch == '\\') {
                    escape = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }
            if (ch == '"') {
                in_string = true;
                continue;
            }
            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                --depth;
                if (depth == 0) {
                    rows.push_back(json.substr(start, pos - start + 1));
                    ++pos;
                    break;
                }
            }
        }
        if (depth != 0) {
            throw std::runtime_error("chunks.json: unterminated row object");
        }
    }
    if (rows.empty()) {
        throw std::runtime_error("chunks.json: rows array is empty");
    }
    return rows;
}

struct ExportRow {
    std::uint32_t id = 0;
    std::uint32_t page = 0;
    std::string source;
    std::string text;
    std::vector<float> embedding;
};

ExportRow parse_row(const std::string &object_json, std::size_t index) {
    ExportRow row;
    const auto id_str = json_get_string(object_json, "id");
    if (!id_str || id_str->empty()) {
        throw std::runtime_error("row " + std::to_string(index) + ": missing id");
    }
    char *end = nullptr;
    const unsigned long id_val = std::strtoul(id_str->c_str(), &end, 10);
    if (end == id_str->c_str() || *end != '\0') {
        throw std::runtime_error("row " + std::to_string(index) + ": invalid id \"" + *id_str + "\"");
    }
    row.id = static_cast<std::uint32_t>(id_val);

    if (const auto page = json_get_int(object_json, "page")) {
        if (*page < 0) {
            throw std::runtime_error("row " + std::to_string(index) + ": negative page");
        }
        row.page = static_cast<std::uint32_t>(*page);
    }

    const auto source = json_get_string(object_json, "source");
    if (!source || source->empty()) {
        throw std::runtime_error("row " + std::to_string(index) + ": missing source");
    }
    row.source = *source;

    const auto text = json_get_string(object_json, "text");
    const auto chunk = json_get_string(object_json, "chunk");
    if (text && !text->empty()) {
        row.text = *text;
    } else if (chunk && !chunk->empty()) {
        row.text = *chunk;
    } else {
        throw std::runtime_error("row " + std::to_string(index) + ": missing text/chunk");
    }

    const auto embedding = json_extract_embedding_array(object_json);
    if (!embedding) {
        throw std::runtime_error("row " + std::to_string(index) + ": missing embedding");
    }
    if (embedding->size() != kDefaultDim) {
        throw std::runtime_error("row " + std::to_string(index) + ": embedding dim " +
                                 std::to_string(embedding->size()) + " (expected " +
                                 std::to_string(kDefaultDim) + ")");
    }
    row.embedding = std::move(*embedding);
    return row;
}

void write_u32(std::ostream &out, std::uint32_t value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void write_u16(std::ostream &out, std::uint16_t value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void write_record(std::ostream &out, const ExportRow &row) {
    if (row.source.size() > 0xFFFF) {
        throw std::runtime_error("source too long for u16 length: id " + std::to_string(row.id));
    }
    const auto source_len = static_cast<std::uint16_t>(row.source.size());
    const auto text_len = static_cast<std::uint32_t>(row.text.size());

    write_u32(out, row.id);
    write_u32(out, row.page);
    write_u16(out, source_len);
    write_u32(out, text_len);
    out.write(row.source.data(), static_cast<std::streamsize>(row.source.size()));
    out.write(row.text.data(), static_cast<std::streamsize>(row.text.size()));
    for (const float value : row.embedding) {
        out.write(reinterpret_cast<const char *>(&value), sizeof(float));
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    const std::string input_path = (argc > 1 && argv[1] && argv[1][0] != '\0') ? argv[1] : "chunks.json";
    const std::string output_path = (argc > 2 && argv[2] && argv[2][0] != '\0') ? argv[2] : "vectors.bin";

    try {
        const std::string json = read_file(input_path);
        const auto row_objects = extract_row_objects(json);

        std::vector<ExportRow> rows;
        rows.reserve(row_objects.size());
        for (std::size_t i = 0; i < row_objects.size(); ++i) {
            rows.push_back(parse_row(row_objects[i], i + 1));
        }

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "cannot open output: " << output_path << "\n";
            return 1;
        }

        out.write(kMagic, 8);
        write_u32(out, kVersion);
        write_u32(out, static_cast<std::uint32_t>(rows.size()));
        write_u32(out, static_cast<std::uint32_t>(kDefaultDim));
        write_u32(out, 0);

        for (const auto &row : rows) {
            write_record(out, row);
        }
        out.flush();
        if (!out) {
            std::cerr << "write failed: " << output_path << "\n";
            return 1;
        }

        std::ifstream check(output_path, std::ios::binary | std::ios::ate);
        const auto file_size = check ? static_cast<std::uint64_t>(check.tellg()) : 0ULL;
        std::cout << "COMPACS1 export OK\n"
                  << "  input:  " << input_path << "\n"
                  << "  output: " << output_path << "\n"
                  << "  records: " << rows.size() << "\n"
                  << "  dim: " << kDefaultDim << "\n"
                  << "  file_size: " << file_size << " bytes\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "export_vectors error: " << ex.what() << "\n";
        return 1;
    }
}
