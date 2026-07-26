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

} // namespace fcitx

#endif // _FCITX5_HANGUL_CANDIDATEPOLICY_H_
