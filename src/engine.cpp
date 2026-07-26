/*
 * SPDX-FileCopyrightText: 2010~2021 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "engine.h"
#include "candidatepolicy.h"
#include <algorithm>
#include <cstdlib>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <hangul.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

static const char *keyboardId[] = {"2",  "2y", "39", "3f", "3s",
                                   "3y", "32", "ro", "ahn"};

constexpr auto MAX_LENGTH = 40;
constexpr size_t COMPLETION_CANDIDATE_SIZE = 20;

namespace fcitx {

namespace {

const KeyList &selectionKeys() {
    static const KeyList selectionKeys{
        Key(FcitxKey_1), Key(FcitxKey_2), Key(FcitxKey_3), Key(FcitxKey_4),
        Key(FcitxKey_5), Key(FcitxKey_6), Key(FcitxKey_7), Key(FcitxKey_8),
        Key(FcitxKey_9), Key(FcitxKey_0)};
    return selectionKeys;
}

std::string ustringToUTF8(const std::u32string &ustr) {
    std::string result;
    for (auto c : ustr) {
        result += utf8::UCS4ToUTF8(c);
    }
    return result;
}

std::u32string ucsToUString(const ucschar *str) {
    std::u32string result;
    while (*str) {
        result.push_back(*str);
        str++;
    }
    return result;
}

bool isModernHangulWord(const std::string &str, size_t minimumLength) {
    size_t length = 0;
    for (auto c : utf8::MakeUTF8CharRange(str)) {
        if (c < 0xAC00 || c > 0xD7A3) {
            return false;
        }
        length++;
    }
    return length >= minimumLength;
}

std::string subUTF8String(const std::string &str, int p1, int p2) {
    int limit;
    int pos;
    int n;

    if (str.empty()) {
        return "";
    }

    limit = str.size() + 1;

    p1 = std::max(0, p1);
    p2 = std::max(0, p2);

    pos = std::min(p1, p2);
    n = std::abs(p2 - p1);

    if (pos + n > limit) {
        n = limit - pos;
    }

    auto begin = utf8::nextNChar(str.begin(), pos);
    auto end = utf8::nextNChar(begin, n);

    return std::string(begin, end);
}

HanjaTable *loadTable() {
    const auto &sp = fcitx::StandardPaths::global();
    auto hanjaTxt =
        sp.locate(fcitx::StandardPathsType::Data, "libhangul/hanja/hanja.txt");
    HanjaTable *table = nullptr;
    if (!hanjaTxt.empty()) {
        table = hanja_table_load(hanjaTxt.string().c_str());
    }
    return table ? table : hanja_table_load(nullptr);
}

CompletionDictionary loadCompletionDictionary() {
    CompletionDictionary dictionary;
    const auto file = StandardPaths::global().locate(
        StandardPathsType::PkgData, "hangul/completion.txt",
        StandardPathsMode::System);
    if (!file.empty()) {
        dictionary.load(file);
    }
    return dictionary;
}

} // namespace

enum class CandidateMode : uint8_t { None, Hanja, Completion };

class HangulCandidate : public CandidateWord {
public:
    HangulCandidate(HangulEngine *engine, int idx, std::string text)
        : engine_(engine), idx_(idx) {
        setText(Text(std::move(text)));
    }

    void select(InputContext *inputContext) const override;

private:
    HangulEngine *engine_;
    int idx_;
};

class HangulCompletionCandidate : public CandidateWord {
public:
    HangulCompletionCandidate(HangulEngine *engine, std::string text)
        : engine_(engine), value_(std::move(text)) {
        setText(Text(value_));
    }

    void select(InputContext *inputContext) const override;

private:
    HangulEngine *engine_;
    std::string value_;
};

class HangulState : public InputContextProperty {
public:
    HangulState(HangulEngine *engine, InputContext *ic)
        : engine_(engine), ic_(ic) {
        configure();
    }

    void configure() {
        context_.reset(hangul_ic_new(
            keyboardId[static_cast<int>(*engine_->config().keyboard)]));
#if defined(FCITX_HANGUL_VERSION_0_2)
        hangul_ic_set_option(context_.get(), HANGUL_IC_OPTION_AUTO_REORDER,
                             *engine_->config().autoReorder);
        hangul_ic_set_option(context_.get(),
                             HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE,
                             *engine_->config().combiOnDoubleStroke);
        hangul_ic_set_option(context_.get(),
                             HANGUL_IC_OPTION_NON_CHOSEONG_COMBI,
                             *engine_->config().nonChoseongCombi);
#else
        hangul_ic_connect_callback(
            context_.get(), "transition",
            reinterpret_cast<void *>(&HangulState::onTransitionCallback), this);
#endif
    }

#if !defined(FCITX_HANGUL_VERSION_0_2)

    static bool onTransitionCallback(HangulInputContext * /*unused*/, ucschar c,
                                     const ucschar * /*unused*/, void *data) {
        auto *that = static_cast<HangulState *>(data);
        return that->onTransition(c);
    }

    bool onTransition(ucschar c) {
        if (!*engine_->config().autoReorder) {
            if (hangul_is_choseong(c)) {
                if (hangul_ic_has_jungseong(context_.get()) ||
                    hangul_ic_has_jongseong(context_.get())) {
                    return false;
                }
            }

            if (hangul_is_jungseong(c)) {
                if (hangul_ic_has_jongseong(context_.get())) {
                    return false;
                }
            }
        }

        return true;
    }
#endif

    std::string currentCompletionPrefix() const {
        std::u32string prefix = completionCommittedPrefix_;
        prefix.append(preedit_);
        const auto *hicPreedit =
            hangul_ic_get_preedit_string(context_.get());
        if (hicPreedit) {
            prefix.append(ucsToUString(hicPreedit));
        }
        return ustringToUTF8(prefix);
    }

    bool persistentHanjaMode() const {
        return usePersistentHanjaCandidates(
            *engine_->config().hanjaMode,
            *engine_->config().wordCompletion);
    }

    void updateCompletion() {
        cleanup();

        const CapabilityFlags noCompletionFlags{
            CapabilityFlag::Password, CapabilityFlag::Sensitive,
            CapabilityFlag::NoSpellCheck};
        if (!*engine_->config().wordCompletion ||
            persistentHanjaMode() ||
            ic_->capabilityFlags().testAny(noCompletionFlags) ||
            engine_->completionDictionary().empty()) {
            return;
        }

        const auto prefix = currentCompletionPrefix();
        if (!isModernHangulWord(prefix, 2)) {
            return;
        }

        completionCandidates_ = engine_->completionDictionary().suggest(
            prefix, COMPLETION_CANDIDATE_SIZE);
        // A bar containing only the text already being composed is not a useful
        // completion surface. Keep normal input untouched in this case.
        if (completionCandidates_.size() < 2) {
            completionCandidates_.clear();
            return;
        }
        candidateMode_ = CandidateMode::Completion;
    }

    void updateLookupTable(bool checkSurrounding) {
        std::string hanjaKey;
        LookupMethod lookupMethod = LookupMethod::LOOKUP_METHOD_PREFIX;

        hanjaList_.reset();
        completionCandidates_.clear();
        candidateMode_ = CandidateMode::None;

        const auto *hic_preedit = hangul_ic_get_preedit_string(context_.get());
        std::u32string preedit = preedit_;
        preedit.append(ucsToUString(hic_preedit));
        if (!preedit.empty()) {
            auto utf8 = ustringToUTF8(preedit);
            if (*engine_->config().wordCommit || persistentHanjaMode()) {
                hanjaKey = std::move(utf8);
                lookupMethod = LookupMethod::LOOKUP_METHOD_PREFIX;
            } else {
                auto cursorPos = ic_->surroundingText().cursor();
                auto substr =
                    subUTF8String(ic_->surroundingText().text(),
                                  static_cast<int>(cursorPos) - 64, cursorPos);

                if (!substr.empty()) {
                    hanjaKey = substr + utf8;
                } else {
                    hanjaKey = std::move(utf8);
                }
                lookupMethod = LookupMethod::LOOKUP_METHOD_SUFFIX;
            }
        } else if (checkSurrounding) {

            if (!ic_->capabilityFlags().test(CapabilityFlag::SurroundingText) ||
                !ic_->surroundingText().isValid()) {
                return;
            }
            const auto &surroundingStr = ic_->surroundingText().text();
            auto cursorPos = ic_->surroundingText().cursor();
            auto anchorPos = ic_->surroundingText().anchor();
            if (cursorPos != anchorPos) {
                // If we have selection in surrounding text, we use that.
                hanjaKey = subUTF8String(surroundingStr, cursorPos, anchorPos);
                lookupMethod = LookupMethod::LOOKUP_METHOD_EXACT;
            } else {
                hanjaKey =
                    subUTF8String(surroundingStr,
                                  static_cast<int>(cursorPos) - 64, cursorPos);
                lookupMethod = LookupMethod::LOOKUP_METHOD_SUFFIX;
            }
        }

        if (!hanjaKey.empty()) {
            hanjaList_.reset(lookupTable(hanjaKey, lookupMethod));
            lastLookupMethod_ = lookupMethod;
            if (hanjaList_) {
                candidateMode_ = CandidateMode::Hanja;
            }
        }
    }

    HanjaList *lookupTable(const std::string &key, LookupMethod method) {
        HanjaList *list = nullptr;

        if (key.empty()) {
            return nullptr;
        }

        decltype(&hanja_table_match_exact) func = nullptr;

        switch (method) {
        case LookupMethod::LOOKUP_METHOD_EXACT:
            func = &hanja_table_match_exact;
            break;
        case LookupMethod::LOOKUP_METHOD_PREFIX:
            func = &hanja_table_match_prefix;
            break;
        case LookupMethod::LOOKUP_METHOD_SUFFIX:
            func = &hanja_table_match_suffix;
            break;
        }
        if (!func) {
            return nullptr;
        }

        if (auto *symbolTable = engine_->symbolTable()) {
            list = func(symbolTable, key.data());
        }

        if (!list) {
            list = func(engine_->table(), key.data());
        }

        return list;
    }

    void keyEvent(KeyEvent &keyEvent) {
        if (keyEvent.isRelease()) {
            return;
        }

        if (keyEvent.key().checkKeyList(
                *engine_->config().hanjaModeToggleKey)) {
            if (*engine_->config().wordCompletion) {
                toggleHanjaCandidates();
            } else {
                if (!hanjaList_) {
                    updateLookupTable(true);
                } else {
                    cleanup();
                }
                updateUI();
            }
            keyEvent.filterAndAccept();
            return;
        }

        auto sym = keyEvent.key().sym();

        if (sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R) {
            return;
        }

        KeyStates s;
        for (const auto *keyList :
             {&*engine_->config().hanjaModeToggleKey,
              &*engine_->config().prevPageKey, &*engine_->config().nextPageKey,
              &*engine_->config().prevCandidateKey,
              &*engine_->config().nextCandidateKey}) {
            for (auto key : *keyList) {
                s |= key.states();
            }
        }

        struct {
            KeyState state;
            KeySym left, right;
        } modifiers[] = {
            {KeyState::Ctrl, FcitxKey_Control_L, FcitxKey_Control_R},
            {KeyState::Alt, FcitxKey_Alt_L, FcitxKey_Alt_R},
            {KeyState::Shift, FcitxKey_Shift_L, FcitxKey_Shift_R},
            {KeyState::Super, FcitxKey_Super_L, FcitxKey_Super_R},
            {KeyState::Hyper, FcitxKey_Hyper_L, FcitxKey_Hyper_R},
        };

        for (auto &modifier : modifiers) {
            if (s & modifier.state) {
                if (sym == modifier.left || sym == modifier.right) {
                    return;
                }
            }
        }

        // Handle candidate selection.
        auto candList = ic_->inputPanel().candidateList();
        if (candList && !candList->empty()) {
            if (keyEvent.key().checkKeyList(*engine_->config().prevPageKey)) {
                candList->toPageable()->prev();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }
            if (keyEvent.key().checkKeyList(*engine_->config().nextPageKey)) {
                candList->toPageable()->next();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }

            if (keyEvent.key().checkKeyList(
                    *engine_->config().prevCandidateKey)) {
                candList->toCursorMovable()->prevCandidate();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }
            if (keyEvent.key().checkKeyList(
                    *engine_->config().nextCandidateKey)) {
                candList->toCursorMovable()->nextCandidate();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }

            auto idx = keyEvent.key().keyListIndex(selectionKeys());
            if (idx >= 0) {
                if (idx < candList->size()) {
                    candList->candidate(idx).select(ic_);
                }
                keyEvent.filterAndAccept();
                return;
            }

            if (keyEvent.key().check(FcitxKey_Return)) {
                auto idx = candList->cursorIndex();
                if (candidateMode_ == CandidateMode::Hanja) {
                    idx = std::max(idx, 0);
                }

                if (idx >= 0 && idx < candList->size()) {
                    candList->candidate(idx).select(ic_);
                    keyEvent.filterAndAccept();
                    return;
                }
            }

            if (!persistentHanjaMode()) {
                cleanup();
            }
        }

        s = KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Shift,
                      KeyState::Super, KeyState::Hyper};
        if (keyEvent.key().states() & s) {
            flush();
            updateUI();
            return;
        }

        bool keyUsed = false;
        if (keyEvent.key().check(FcitxKey_BackSpace)) {
            keyUsed = hangul_ic_backspace(context_.get());
            if (!keyUsed) {
                unsigned int preedit_len = preedit_.size();
                if (preedit_len > 0) {
                    preedit_.pop_back();
                    keyUsed = true;
                }
            }
            if (!keyUsed && !completionCommittedPrefix_.empty()) {
                // The key still needs to reach Android so the already committed
                // prefix is deleted there as well. Mirror that deletion only in
                // our candidate state.
                completionCommittedPrefix_.pop_back();
            }
        } else {
            if (preedit_.size() >= MAX_LENGTH) {
                flush();
            }

            // revert capslock
            if (keyEvent.rawKey().states().test(KeyState::CapsLock)) {
                if (sym >= 'A' && sym <= 'z') {
                    if (charutils::isupper(sym)) {
                        sym = static_cast<KeySym>(charutils::tolower(sym));
                    } else {
                        sym = static_cast<KeySym>(charutils::toupper(sym));
                    }
                }
            }

            keyUsed = hangul_ic_process(context_.get(), sym);
            bool notFlush = false;

            const ucschar *str = hangul_ic_get_commit_string(context_.get());
            if (*engine_->config().wordCommit || persistentHanjaMode()) {
                const ucschar *hic_preedit;

                hic_preedit = hangul_ic_get_preedit_string(context_.get());
                preedit_.append(ucsToUString(str));
                if (hic_preedit == nullptr || hic_preedit[0] == 0) {
                    if (!preedit_.empty()) {
                        auto commit = ustringToUTF8(preedit_);
                        if (!commit.empty()) {
                            ic_->commitString(commit);
                        }
                    }
                    preedit_.clear();
                }
            } else {
                if (str != nullptr && str[0] != 0) {
                    auto committed = ucsToUString(str);
                    completionCommittedPrefix_.append(committed);
                    auto commit = ustringToUTF8(committed);
                    if (!commit.empty()) {
                        ic_->commitString(commit);
                    }
                }
            }

            if (!keyUsed && !notFlush) {
                flush();
            }
        }

        if (persistentHanjaMode()) {
            updateLookupTable(false);
        } else {
            updateCompletion();
        }

        updateUI();
        if (keyUsed) {
            keyEvent.filterAndAccept();
        }
    }

    void reset() {
        preedit_.clear();
        completionCommittedPrefix_.clear();
        hangul_ic_reset(context_.get());
        cleanup();
        updateUI();
    }

    void toggleHanjaCandidates() {
        if (candidateMode_ == CandidateMode::Hanja) {
            cleanup();
        } else {
            // Completion commits the stable part of a Hangul word directly to
            // Android. Flush the final composing syllable too, then perform a
            // surrounding-text lookup so selecting Hanja replaces the entire
            // word instead of duplicating its committed prefix.
            flush();
            updateLookupTable(true);
        }
        updateUI();
    }

    void cleanup() {
        hanjaList_.reset();
        completionCandidates_.clear();
        candidateMode_ = CandidateMode::None;
    }

    void flush() {
        cleanup();

        const auto *str = hangul_ic_flush(context_.get());

        preedit_ += ucsToUString(str);

        if (preedit_.empty()) {
            completionCommittedPrefix_.clear();
            return;
        }

        auto utf8 = ustringToUTF8(preedit_);
        completionCommittedPrefix_.clear();
        if (!utf8.empty()) {
            ic_->commitString(utf8);
        }

        preedit_.clear();
    }

    void updateUI() {
        const ucschar *hic_preedit =
            hangul_ic_get_preedit_string(context_.get());

        ic_->inputPanel().reset();

        std::string pre1 = ustringToUTF8(preedit_);
        std::string pre2;
        if (hic_preedit) {
            pre2 = ustringToUTF8(ucsToUString(hic_preedit));
        }

        if (!pre1.empty() || !pre2.empty()) {
            Text text;
            text.append(pre1);
            text.append(pre2, TextFormatFlag::HighLight);
            text.setCursor(pre1.size() + pre2.size());
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
                ic_->inputPanel().setClientPreedit(text);
            } else {
                ic_->inputPanel().setPreedit(text);
            }
        }
        ic_->updatePreedit();

        setLookupTable();

        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void setLookupTable() {
        if (candidateMode_ == CandidateMode::Completion) {
            if (completionCandidates_.empty()) {
                return;
            }
            auto candidate = std::make_unique<CommonCandidateList>();
            candidate->setSelectionKey(selectionKeys());
            candidate->setCursorIncludeUnselected(true);
            candidate->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);
            candidate->setPageSize(
                engine_->instance()->globalConfig().defaultPageSize());
            for (const auto &value : completionCandidates_) {
                candidate->append<HangulCompletionCandidate>(engine_, value);
            }
            ic_->inputPanel().setCandidateList(std::move(candidate));
            return;
        }

        if (candidateMode_ != CandidateMode::Hanja || !hanjaList_) {
            return;
        }
        HanjaList *list = hanjaList_.get();
        if (list) {
            auto candidate = std::make_unique<CommonCandidateList>();
            candidate->setSelectionKey(selectionKeys());
            candidate->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);
            candidate->setPageSize(
                engine_->instance()->globalConfig().defaultPageSize());
            auto n = hanja_list_get_size(list);
            for (auto i = 0; i < n; i++) {
                const char *value = hanja_list_get_nth_value(list, i);
                candidate->append<HangulCandidate>(engine_, i, value);
            }
            if (n) {
                candidate->setGlobalCursorIndex(0);
                ic_->inputPanel().setCandidateList(std::move(candidate));
            }
        }
    }

    void select(int pos) {
        const char *key;
        const char *value;
        const ucschar *hic_preedit;
        int key_len;
        int preedit_len;
        int hic_preedit_len;

        key = hanja_list_get_nth_key(hanjaList_.get(), pos);
        value = hanja_list_get_nth_value(hanjaList_.get(), pos);
        hic_preedit = hangul_ic_get_preedit_string(context_.get());

        if (!key || !value || !hic_preedit) {
            reset();
            return;
        }

        key_len = fcitx::utf8::length(std::string(key));
        preedit_len = preedit_.size();
        hic_preedit_len = ucsToUString(hic_preedit).size();

        bool surrounding = false;
        if (lastLookupMethod_ == LookupMethod::LOOKUP_METHOD_PREFIX) {
            if (preedit_len == 0 && hic_preedit_len == 0) {
                /* remove surrounding_text */
                if (key_len > 0) {
                    ic_->deleteSurroundingText(-key_len, key_len);
                    surrounding = true;
                }
            } else {
                /* remove preedit text */
                if (key_len > 0) {
                    long n = std::min(key_len, preedit_len);
                    preedit_.erase(0, n);
                    key_len -= preedit_len;
                }

                /* remove hic preedit text */
                if (key_len > 0) {
                    hangul_ic_reset(context_.get());
                    key_len -= hic_preedit_len;
                }
            }
        } else {
            /* remove hic preedit text */
            if (hic_preedit_len > 0) {
                hangul_ic_reset(context_.get());
                key_len -= hic_preedit_len;
            }

            /* remove preedit text */
            if (key_len > preedit_len) {
                preedit_.erase(0, preedit_len);
                key_len -= preedit_len;
            } else if (key_len > 0) {
                preedit_.erase(0, key_len);
                key_len = 0;
            }

            /* remove surrounding_text */
            if (LookupMethod::LOOKUP_METHOD_EXACT != lastLookupMethod_ &&
                key_len > 0) {
                ic_->deleteSurroundingText(-key_len, key_len);
                surrounding = true;
            }
        }

        ic_->commitString(value);
        if (surrounding) {
            cleanup();
        }
        updateLookupTable(false);
        updateUI();
    }

    void selectCompletion(const std::string &value) {
        const auto currentPrefix = currentCompletionPrefix();
        if (candidateMode_ != CandidateMode::Completion ||
            value.compare(0, currentPrefix.size(), currentPrefix) != 0) {
            cleanup();
            updateUI();
            return;
        }

        const auto committedPrefix =
            ustringToUTF8(completionCommittedPrefix_);
        const auto suffix = CompletionDictionary::suffixAfterCommittedPrefix(
            value, committedPrefix);
        if (!suffix) {
            cleanup();
            updateUI();
            return;
        }

        hangul_ic_reset(context_.get());
        preedit_.clear();
        cleanup();

        completionCommittedPrefix_.clear();
        if (!suffix->empty()) {
            ic_->commitString(*suffix);
        }
        updateUI();
    }

private:
    HangulEngine *engine_;
    InputContext *ic_;
    UniqueCPtr<HangulInputContext, &hangul_ic_delete> context_;
    UniqueCPtr<HanjaList, &hanja_list_delete> hanjaList_;
    std::u32string preedit_;
    std::u32string completionCommittedPrefix_;
    std::vector<std::string> completionCandidates_;
    CandidateMode candidateMode_ = CandidateMode::None;
    LookupMethod lastLookupMethod_;
};

HangulEngine::HangulEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &ic) { return new HangulState(this, &ic); }),
      table_(loadTable()), completionDictionary_(loadCompletionDictionary()) {
    if (!table_) {
        throw std::runtime_error("Failed to load hanja table.");
    }

    auto file = StandardPaths::global().locate(StandardPathsType::PkgData,
                                               "hangul/symbol.txt");
    if (!file.empty()) {
        symbolTable_.reset(hanja_table_load(file.string().c_str()));
    }

    reloadConfig();
    action_.connect<SimpleAction::Activated>([this](InputContext *ic) {
        if (*config_.wordCompletion) {
            state(ic)->toggleHanjaCandidates();
            return;
        }
        config_.hanjaMode.setValue(!*config_.hanjaMode);
        updateAction(ic);
    });
    instance_->userInterfaceManager().registerAction("hangul", &action_);

    instance_->inputContextManager().registerProperty("hangulState", &factory_);
}

void HangulEngine::activate(const InputMethodEntry & /*entry*/,
                            InputContextEvent &event) {
    event.inputContext()->statusArea().addAction(StatusGroup::InputMethod,
                                                 &action_);
    updateAction(event.inputContext());
}

void HangulEngine::deactivate(const InputMethodEntry &entry,
                              InputContextEvent &event) {
    if (event.type() == EventType::InputContextSwitchInputMethod) {
        auto *state = event.inputContext()->propertyFor(&factory_);
        state->flush();
    }
    reset(entry, event);
}

void HangulEngine::keyEvent(const InputMethodEntry & /*entry*/,
                            KeyEvent &keyEvent) {
    if (keyEvent.isRelease()) {
        return;
    }
    auto *state = keyEvent.inputContext()->propertyFor(&factory_);
    state->keyEvent(keyEvent);
}

void HangulEngine::reset(const InputMethodEntry & /*entry*/,
                         InputContextEvent &event) {
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->reset();
}

void HangulEngine::reloadConfig() { readAsIni(config_, "conf/hangul.conf"); }

void HangulEngine::setConfig(const fcitx::RawConfig &rawConfig) {
    config_.load(rawConfig, true);
    instance_->inputContextManager().foreach([this](InputContext *ic) {
        state(ic)->configure();
        updateAction(ic);
        return true;
    });
    safeSaveAsIni(config_, "conf/hangul.conf");
}

HangulState *HangulEngine::state(InputContext *ic) {
    return ic->propertyFor(&factory_);
}

void HangulCandidate::select(InputContext *inputContext) const {
    auto *state = engine_->state(inputContext);
    state->select(idx_);
}

void HangulCompletionCandidate::select(InputContext *inputContext) const {
    auto *state = engine_->state(inputContext);
    state->selectCompletion(value_);
}
} // namespace fcitx

FCITX_ADDON_FACTORY_V2(hangul, fcitx::HangulEngineFactory);
