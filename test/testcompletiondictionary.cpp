/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "completiondictionary.h"
#include "candidatepolicy.h"
#include <cassert>
#include <chrono>
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

    const auto personalPath = std::filesystem::temp_directory_path() /
                              "personal-completion-dictionary-test.txt";
    {
        std::ofstream stream(personalPath);
        stream << "# fcitx5-android-personal-dictionary-v1\n"
                  "enabled\t1\n"
                  "word\tname\t안녕윤찬\n"
                  "word\tcompany\t안녕회사\n"
                  "word\tterm\t안녕하세요\n"
                  "word\tname\t안녕윤찬\n";
    }
    CompletionDictionary personal;
    assert(personal.loadPersonal(personalPath));
    assert(personal.size() == 3);
    assert((CompletionDictionary::suggestPrioritized(
                "안녕", personal, dictionary, 5) ==
            std::vector<std::string>{"안녕", "안녕윤찬", "안녕회사",
                                     "안녕하세요", "안녕하십니까"}));
    assert((CompletionDictionary::suggestPrioritized(
                "안녕", personal, dictionary, 3) ==
            std::vector<std::string>{"안녕", "안녕윤찬", "안녕회사"}));

    {
        std::ofstream stream(personalPath);
        stream << "# fcitx5-android-personal-dictionary-v1\n"
                  "enabled\t0\n"
                  "word\tname\t안녕윤찬\n";
    }
    assert(personal.loadPersonal(personalPath));
    assert(personal.empty());

    {
        std::ofstream stream(personalPath);
        stream << "# fcitx5-android-personal-dictionary-v1\n"
                  "enabled\t1\n"
                  "word\tunknown\t안녕윤찬\n";
    }
    assert(!personal.loadPersonal(personalPath));
    assert(personal.empty());
    assert((CompletionDictionary::suggestPrioritized(
                "안녕", personal, dictionary, 3) ==
            std::vector<std::string>{"안녕", "안녕하세요", "안녕하십니까"}));

    // The IME hot path must not reopen and reparse an unchanged file. Replacing
    // it with same-size data while restoring the stamp proves the cached value
    // is used until the path, mtime, or size changes.
    {
        std::ofstream stream(personalPath);
        stream << "# fcitx5-android-personal-dictionary-v1\n"
                  "enabled\t1\n"
                  "word\tname\t안녕윤찬\n";
    }
    const auto originalTime = std::filesystem::last_write_time(personalPath);
    PersonalDictionaryCache cache;
    assert((cache.dictionary(personalPath).suggest("안녕", 3) ==
            std::vector<std::string>{"안녕", "안녕윤찬"}));
    {
        std::ofstream stream(personalPath);
        stream << "# fcitx5-android-personal-dictionary-v1\n"
                  "enabled\t1\n"
                  "word\tname\t안녕민수\n";
    }
    std::filesystem::last_write_time(personalPath, originalTime);
    assert((cache.dictionary(personalPath).suggest("안녕", 3) ==
            std::vector<std::string>{"안녕", "안녕윤찬"}));

    std::filesystem::last_write_time(personalPath,
                                     originalTime + std::chrono::seconds(2));
    assert((cache.dictionary(personalPath).suggest("안녕", 3) ==
            std::vector<std::string>{"안녕", "안녕민수"}));

    {
        std::ofstream stream(personalPath);
        stream << "corrupt\n";
    }
    assert(cache.dictionary(personalPath).empty());
    // The corrupt result is also cached and remains fail-closed.
    assert(cache.dictionary(personalPath).empty());

    const auto secondPersonalPath = std::filesystem::temp_directory_path() /
                                    "personal-completion-cache-path-test.txt";
    {
        std::ofstream stream(secondPersonalPath);
        stream << "# fcitx5-android-personal-dictionary-v1\n"
                  "enabled\t1\n"
                  "word\tcompany\t안녕회사\n";
    }
    assert((cache.dictionary(secondPersonalPath).suggest("안녕", 3) ==
            std::vector<std::string>{"안녕", "안녕회사"}));
    std::filesystem::remove(secondPersonalPath);
    std::filesystem::remove(personalPath);

    const auto nextWordPath = std::filesystem::temp_directory_path() /
                              "next-word-dictionary-test.txt";
    {
        std::ofstream stream(nextWordPath);
        stream << "# previous<TAB>next...\n"
                  "오늘\t하루도\t날씨가\t하루도\n"
                  "오늘\t저녁에\t날씨가\r\n"
                  "정말\t감사합니다\t좋아요\n";
    }
    NextWordDictionary nextWords;
    assert(nextWords.load(nextWordPath));
    assert(nextWords.size() == 2);
    assert((nextWords.suggest("오늘", 10) ==
            std::vector<std::string>{"하루도", "날씨가", "저녁에"}));
    assert((nextWords.suggest("오늘", 2) ==
            std::vector<std::string>{"하루도", "날씨가"}));
    assert(nextWords.suggest("없는말", 5).empty());
    assert(nextWords.suggest("今天", 5).empty());
    assert(nextWords.suggest("오늘", 0).empty());

    {
        std::ofstream stream(nextWordPath);
        stream << "오늘\t좋아요\tChinese\n";
    }
    assert(!nextWords.load(nextWordPath));
    assert(nextWords.empty());
    std::filesystem::remove(nextWordPath);

    assert(!usePersistentHanjaCandidates(false, false));
    assert(usePersistentHanjaCandidates(true, false));
    assert(!usePersistentHanjaCandidates(false, true));
    assert(!usePersistentHanjaCandidates(true, true));

    assert(allowKoreanCompletion(false, false, false));
    assert(!allowKoreanCompletion(true, false, false));
    assert(!allowKoreanCompletion(false, true, false));
    assert(!allowKoreanCompletion(false, false, true));

    assert(allowKoreanNextWord(true, true, false, false, false));
    assert(!allowKoreanNextWord(false, true, false, false, false));
    assert(!allowKoreanNextWord(true, false, false, false, false));
    assert(!allowKoreanNextWord(true, true, true, false, false));
    assert(!allowKoreanNextWord(true, true, false, true, false));
    assert(!allowKoreanNextWord(true, true, false, false, true));

    return 0;
}
