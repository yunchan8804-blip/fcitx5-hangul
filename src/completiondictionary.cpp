/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "completiondictionary.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
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

bool CompletionDictionary::loadPersonal(const std::filesystem::path &path) {
    words_.clear();
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > 64 * 1024) {
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    auto readLine = [&stream](std::string &line) {
        if (!std::getline(stream, line)) {
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return true;
    };

    std::string line;
    if (!readLine(line) ||
        line != "# fcitx5-android-personal-dictionary-v1") {
        return false;
    }
    if (!readLine(line) || (line != "enabled\t0" && line != "enabled\t1")) {
        return false;
    }
    const bool enabled = line == "enabled\t1";

    std::vector<std::string> words;
    std::unordered_set<std::string> seen;
    size_t entries = 0;
    while (readLine(line)) {
        if (++entries > 500) {
            return false;
        }
        const auto first = line.find('\t');
        const auto second = first == std::string::npos
                                ? std::string::npos
                                : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            line.find('\t', second + 1) != std::string::npos ||
            line.substr(0, first) != "word") {
            return false;
        }
        const auto category = line.substr(first + 1, second - first - 1);
        if (category != "name" && category != "company" && category != "term") {
            return false;
        }
        auto word = line.substr(second + 1);
        // Every modern precomposed Hangul syllable occupies three UTF-8 bytes.
        if (word.size() < 6 || word.size() > 96 || !isModernHangulWord(word)) {
            return false;
        }
        if (seen.insert(word).second) {
            words.push_back(std::move(word));
        }
    }
    if (!stream.eof()) {
        return false;
    }

    if (enabled) {
        words_ = std::move(words);
    }
    return true;
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

std::vector<std::string> CompletionDictionary::suggestPrioritized(
    const std::string &prefix, const CompletionDictionary &preferred,
    const CompletionDictionary &fallback, size_t limit) {
    if (!isModernHangulWord(prefix) || limit == 0) {
        return {};
    }

    std::vector<std::string> result;
    result.reserve(limit);
    result.push_back(prefix);
    if (limit == 1) {
        return result;
    }

    std::unordered_set<std::string> seen{prefix};
    const auto append = [&](const CompletionDictionary &dictionary) {
        for (const auto &word : dictionary.words_) {
            if (result.size() == limit) {
                return;
            }
            if (word.size() <= prefix.size() ||
                std::string_view(word).substr(0, prefix.size()) != prefix ||
                !seen.insert(word).second) {
                continue;
            }
            result.push_back(word);
        }
    };
    append(preferred);
    append(fallback);
    return result;
}

std::optional<std::string> CompletionDictionary::suffixAfterCommittedPrefix(
    const std::string &candidate, const std::string &committedPrefix) {
    if (candidate.compare(0, committedPrefix.size(), committedPrefix) != 0) {
        return std::nullopt;
    }
    return candidate.substr(committedPrefix.size());
}

bool PersonalDictionaryCache::sameSignature(const FileSignature &left,
                                            const FileSignature &right) {
    return left.path == right.path && left.exists == right.exists &&
           left.size == right.size && left.modified == right.modified;
}

const CompletionDictionary &
PersonalDictionaryCache::dictionary(const std::filesystem::path &path) {
    FileSignature current;
    current.path = path.lexically_normal();

    std::error_code error;
    current.exists = std::filesystem::exists(current.path, error);
    if (error) {
        signature_.reset();
        dictionary_ = CompletionDictionary();
        return dictionary_;
    }
    if (current.exists) {
        current.size = std::filesystem::file_size(current.path, error);
        if (error) {
            signature_.reset();
            dictionary_ = CompletionDictionary();
            return dictionary_;
        }
        current.modified =
            std::filesystem::last_write_time(current.path, error);
        if (error) {
            signature_.reset();
            dictionary_ = CompletionDictionary();
            return dictionary_;
        }
    }

    if (signature_ && sameSignature(*signature_, current)) {
        return dictionary_;
    }

    signature_ = current;
    dictionary_ = CompletionDictionary();
    if (current.exists) {
        // loadPersonal clears on every parse error. Cache that empty result for
        // this exact stamp so a corrupt file cannot add repeated work to typing.
        dictionary_.loadPersonal(current.path);
    }
    return dictionary_;
}

} // namespace fcitx
