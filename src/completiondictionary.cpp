/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "completiondictionary.h"
#include <fstream>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fcitx {

CompletionDictionary::CompletionDictionary(std::vector<std::string> words)
    : words_(std::move(words)) {}

bool CompletionDictionary::load(const std::filesystem::path &path) {
    std::ifstream stream(path);
    if (!stream) {
        words_.clear();
        return false;
    }

    std::vector<std::string> words;
    std::unordered_set<std::string> seen;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('\t');
        if (separator == std::string::npos || separator + 1 >= line.size()) {
            continue;
        }
        auto word = line.substr(separator + 1);
        if (!word.empty() && word.back() == '\r') {
            word.pop_back();
        }
        if (!word.empty() && seen.insert(word).second) {
            words.push_back(std::move(word));
        }
    }

    words_ = std::move(words);
    return !words_.empty();
}

std::vector<std::string>
CompletionDictionary::suggest(const std::string &prefix, size_t limit) const {
    if (prefix.empty() || limit == 0 || words_.empty()) {
        return {};
    }

    std::vector<std::string> result;
    result.reserve(limit);
    result.push_back(prefix);
    if (limit == 1) {
        return result;
    }

    std::unordered_set<std::string> seen{prefix};
    for (const auto &word : words_) {
        if (word.size() <= prefix.size() ||
            std::string_view(word).substr(0, prefix.size()) != prefix ||
            !seen.insert(word).second) {
            continue;
        }
        result.push_back(word);
        if (result.size() == limit) {
            break;
        }
    }
    return result;
}

std::optional<std::string> CompletionDictionary::suffixAfterCommittedPrefix(
    const std::string &candidate, const std::string &committedPrefix) {
    if (candidate.compare(0, committedPrefix.size(), committedPrefix) != 0) {
        return std::nullopt;
    }
    return candidate.substr(committedPrefix.size());
}

} // namespace fcitx
