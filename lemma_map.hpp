// Russian form → lemma map for BM25 / query normalization (lemma_map.tsv).
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace compacs::rag {

class LemmaMap {
public:
    bool load_file(const std::filesystem::path &path, std::string *error = nullptr) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error) {
                *error = "cannot open " + path.string();
            }
            return false;
        }
        map_.clear();
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            const auto tab = line.find('\t');
            if (tab == std::string::npos) {
                continue;
            }
            const std::string form = line.substr(0, tab);
            const std::string lemma = line.substr(tab + 1);
            if (!form.empty() && !lemma.empty()) {
                map_[form] = lemma;
            }
        }
        loaded_ = !map_.empty();
        return loaded_;
    }

    bool loaded() const { return loaded_; }
    std::size_t size() const { return map_.size(); }

    std::string normalize_token(std::string token) const {
        for (char &ch : token) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            } else if (ch >= 'А' && ch <= 'Я') {
                ch = static_cast<char>(ch - 'А' + 'а');
            }
        }
        if (const auto it = map_.find(token); it != map_.end()) {
            return it->second;
        }
        return token;
    }

private:
    std::unordered_map<std::string, std::string> map_;
    bool loaded_ = false;
};

}  // namespace compacs::rag
