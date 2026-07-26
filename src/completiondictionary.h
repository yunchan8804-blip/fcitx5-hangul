/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_HANGUL_COMPLETIONDICTIONARY_H_
#define _FCITX5_HANGUL_COMPLETIONDICTIONARY_H_

#include <cstddef>
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
    bool empty() const { return words_.empty(); }
    size_t size() const { return words_.size(); }

    std::vector<std::string> suggest(const std::string &prefix,
                                     size_t limit) const;

    static std::optional<std::string>
    suffixAfterCommittedPrefix(const std::string &candidate,
                               const std::string &committedPrefix);

private:
    std::vector<std::string> words_;
};

} // namespace fcitx

#endif // _FCITX5_HANGUL_COMPLETIONDICTIONARY_H_
