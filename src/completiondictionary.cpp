/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "completiondictionary.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fcitx {

CompletionDictionary::CompletionDictionary(std::vector<std::string> words) {
    std::copy_if(std::make_move_iterator(words.begin()),
                 std::make_move_iterator(words.end()),
                 std::back_inserter(words_), [](const auto &word) {
                     return isModernHangulWord(word);
                 });
}

bool CompletionDictionary::isModernHangulWord(const std::string &word) {
    if (word.empty()) {
        return false;
    }

    size_t offset = 0;
    while (offset < word.size()) {
        if (offset + 2 >= word.size()) {
            return false;
        }
        const auto first = static_cast<unsigned char>(word[offset]);
        const auto second = static_cast<unsigned char>(word[offset + 1]);
        const auto third = static_cast<unsigned char>(word[offset + 2]);
        if ((first & 0xF0) != 0xE0 || (second & 0xC0) != 0x80 ||
            (third & 0xC0) != 0x80) {
            return false;
        }
        const auto codepoint = ((first & 0x0F) << 12) |
                               ((second & 0x3F) << 6) | (third & 0x3F);
        if (codepoint < 0xAC00 || codepoint > 0xD7A3) {
            return false;
        }
        offset += 3;
    }
    return true;
}

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
        if (isModernHangulWord(word) && seen.insert(word).second) {
            words.push_back(std::move(word));
        }
    }

    words_ = std::move(words);
    return !words_.empty();
}

std::vector<std::string>
CompletionDictionary::suggest(const std::string &prefix, size_t limit) const {
    if (!isModernHangulWord(prefix) || limit == 0 || words_.empty()) {
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
