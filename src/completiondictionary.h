/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_HANGUL_COMPLETIONDICTIONARY_H_
#define _FCITX5_HANGUL_COMPLETIONDICTIONARY_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fcitx {

class CompletionDictionary {
public:
    CompletionDictionary() = default;
    explicit CompletionDictionary(std::vector<std::string> words);

    bool load(const std::filesystem::path &path);
    bool loadPersonal(const std::filesystem::path &path);
    bool empty() const { return words_.empty(); }
    size_t size() const { return words_.size(); }

    std::vector<std::string> suggest(const std::string &prefix,
                                     size_t limit) const;

    static std::vector<std::string>
    suggestPrioritized(const std::string &prefix,
                       const CompletionDictionary &preferred,
                       const CompletionDictionary &fallback, size_t limit);

    static std::optional<std::string>
    suffixAfterCommittedPrefix(const std::string &candidate,
                               const std::string &committedPrefix);

    static bool isModernHangulWord(const std::string &word);

private:
    std::vector<std::string> words_;
};

/** Reloads the strict personal dictionary only when its path or file stamp changes. */
class PersonalDictionaryCache {
public:
    const CompletionDictionary &dictionary(const std::filesystem::path &path);

private:
    struct FileSignature {
        std::filesystem::path path;
        bool exists = false;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified{};
    };

    static bool sameSignature(const FileSignature &left,
                              const FileSignature &right);

    std::optional<FileSignature> signature_;
    CompletionDictionary dictionary_;
};

} // namespace fcitx

#endif // _FCITX5_HANGUL_COMPLETIONDICTIONARY_H_
