// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_INSTALLER_LAUNCHER_H_
#define CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_INSTALLER_LAUNCHER_H_

#include "base/functional/callback_forward.h"

class GURL;

namespace chrome::startup::nbrowser_update {

using DownloadAndLaunchInstallerCallback = base::OnceCallback<void(bool)>;

// Downloads the installer at `installer_download_url` to a temporary file
// and, on success, launches it exactly as if the user had double-clicked
// it themselves - no silent-install switches, no elevation unless this is
// a system-level NBrowser install (in which case the installer is launched
// through the same admin-elevation path setup.exe already uses elsewhere).
// Invokes `callback` with whether the download and launch both succeeded.
//
// For the common case - a per-user install, which is NBrowser's default -
// `callback` fires as soon as the installer process has started, without
// waiting for it to finish. For a system-level install, Windows offers no
// non-blocking equivalent of the "runas" elevation prompt, so `callback`
// instead fires once the elevated installer process has exited (this still
// happens off the calling sequence, so it never blocks the UI).
//
// Only ever called after the user has explicitly accepted the update
// infobar - never automatically.
void DownloadAndLaunchInstaller(const GURL& installer_download_url,
                                DownloadAndLaunchInstallerCallback callback);

}  // namespace chrome::startup::nbrowser_update

#endif  // CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_INSTALLER_LAUNCHER_H_
