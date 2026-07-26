/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "completiondictionary.h"
#include "candidatepolicy.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace fcitx;

int main() {
    CompletionDictionary dictionary({"안녕하세요", "안녕하십니까",
                                     "안녕하세요", "안녕", "감사합니다",
                                     "你好", "안녕世界"});

    const auto suggestions = dictionary.suggest("안녕", 5);
    assert((suggestions == std::vector<std::string>{
                               "안녕", "안녕하세요", "안녕하십니까"}));

    const auto limited = dictionary.suggest("안녕", 2);
    assert((limited ==
            std::vector<std::string>{"안녕", "안녕하세요"}));

    assert(dictionary.suggest("감사", 5) ==
           std::vector<std::string>({"감사", "감사합니다"}));
    assert(dictionary.suggest("없는말", 5) ==
           std::vector<std::string>({"없는말"}));
    assert(dictionary.suggest("", 5).empty());
    assert(dictionary.suggest("你", 5).empty());
    assert(dictionary.suggest("안녕", 0).empty());

    assert(CompletionDictionary::suffixAfterCommittedPrefix("안녕하세요", "안") ==
           "녕하세요");
    assert(CompletionDictionary::suffixAfterCommittedPrefix("안녕하세요", "") ==
           "안녕하세요");
    assert(CompletionDictionary::suffixAfterCommittedPrefix("안녕", "안녕") ==
           "");
    assert(!CompletionDictionary::suffixAfterCommittedPrefix("안녕하세요", "감사"));

    const auto path =
        std::filesystem::temp_directory_path() / "completion-dictionary-test.txt";
    {
        std::ofstream stream(path);
        stream << "# comment\n000001\t안녕하세요\ninvalid\n"
                  "000002\t감사합니다\r\n000003\t안녕하세요\n"
                  "000004\t中文\n000005\t한글中文\n";
    }
    CompletionDictionary loaded;
    assert(loaded.load(path));
    assert(loaded.size() == 2);
    assert((loaded.suggest("감사", 5) ==
            std::vector<std::string>{"감사", "감사합니다"}));
    std::filesystem::remove(path);

    assert(!usePersistentHanjaCandidates(false, false));
    assert(usePersistentHanjaCandidates(true, false));
    assert(!usePersistentHanjaCandidates(false, true));
    assert(!usePersistentHanjaCandidates(true, true));

    return 0;
}
