/*
 * SPDX-FileCopyrightText: 2026 Yun Chan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_HANGUL_CANDIDATEPOLICY_H_
#define _FCITX5_HANGUL_CANDIDATEPOLICY_H_

namespace fcitx {

// Korean completion owns the normal candidate surface. Persistent Hanja mode
// is only effective when completion is disabled; Hanja remains available as an
// explicit one-shot conversion.
constexpr bool usePersistentHanjaCandidates(bool hanjaMode,
                                            bool wordCompletion) {
    return hanjaMode && !wordCompletion;
}

// Word completion turns the Hanja control into an explicit one-shot action.
// Clear a legacy persistent value so settings and status surfaces cannot imply
// that the current Korean input mode is Hanja.
constexpr bool shouldClearLegacyHanjaMode(bool hanjaMode,
                                          bool wordCompletion) {
    return hanjaMode && wordCompletion;
}

// Android maps IME_FLAG_NO_PERSONALIZED_LEARNING to Sensitive. Keeping the
// complete flag policy explicit here makes the native privacy boundary
// testable without collecting editor or application identity.
constexpr bool allowKoreanCompletion(bool password, bool sensitive,
                                     bool noSpellCheck) {
    return !password && !sensitive && !noSpellCheck;
}

// Next-word candidates are only an explicit surface after a literal word
// boundary. No candidate may be generated in sensitive editors or inserted by
// this policy itself.
constexpr bool allowKoreanNextWord(bool completionEnabled, bool spaceBoundary,
                                   bool password, bool sensitive,
                                   bool noSpellCheck) {
    return completionEnabled && spaceBoundary &&
           allowKoreanCompletion(password, sensitive, noSpellCheck);
}

} // namespace fcitx

#endif // _FCITX5_HANGUL_CANDIDATEPOLICY_H_
