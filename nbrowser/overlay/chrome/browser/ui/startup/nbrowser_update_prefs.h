// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_PREFS_H_
#define CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_PREFS_H_

namespace chrome::startup::nbrowser_update::prefs {

// String Local State pref: the release tag (e.g. "151.0.7922.173-1.1") of
// the last update the user dismissed the infobar for, so that exact
// release is never prompted for again - a newer one still will be.
// Registered directly in RegisterLocalState() in
// chrome/browser/prefs/browser_prefs.cc, alongside NBrowser's other
// non-subsystem-owned Local State prefs.
inline constexpr char kUpdateIgnoredReleaseTag[] =
    "nbrowser.update_ignored_release_tag";

// Bool Local State pref: whether the last completed update check (from any
// caller - the startup infobar or the chrome://settings/help page) found a
// newer release. Drives the green dot next to "About NBrowser" in the
// settings nav menu (see settings_ui.cc's "nbrowserUpdateAvailable"
// loadTimeData boolean and settings_menu.ts/.html). Set/cleared in
// nbrowser_update_prompt.cc's OnUpdateCheckComplete, reflecting only the
// most recent check - it is not itself per-release like the pref above.
inline constexpr char kUpdateAvailableDotVisible[] =
    "nbrowser.update_available_dot_visible";

}  // namespace chrome::startup::nbrowser_update::prefs

#endif  // CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_PREFS_H_
