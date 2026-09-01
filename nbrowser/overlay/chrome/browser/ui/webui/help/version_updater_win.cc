// Copyright 2026 The Nothing Software Authors
// Use of this source code is governed by a BSD-style license
//
// NBrowser: this file fully replaces upstream's version_updater_win.cc.
// Upstream's VersionUpdaterWin drives the chrome://settings/help "About"
// page through Google Update's COM interfaces (IGoogleUpdate3Web), which
// aren't registered on an unbranded build - see
// chrome/browser/google/google_update_win.h and this class's own removed
// "There is no supported integration with Google Update for Chromium"
// comment. This version wires the exact same VersionUpdater interface -
// and therefore the exact same UI: status text, spinner, "Relaunch" button
// - to NBrowser's own GitHub Releases checker instead. See
// nbrowser/overlay/chrome/browser/ui/startup/nbrowser_update_checker.h and
// nbrowser_update_installer_launcher.h for the actual network/install
// logic; this file only translates their results into
// VersionUpdater::Status for the page that's already there.

#include "chrome/browser/ui/webui/help/version_updater.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/startup/nbrowser_update_checker.h"
#include "chrome/browser/ui/startup/nbrowser_update_installer_launcher.h"
#include "chrome/browser/ui/startup/nbrowser_update_prefs.h"
#include "chrome/browser/ui/startup/nbrowser_update_prompt.h"
#include "chrome/browser/ui/webui/nbrowser_ui_strings/grit/nbrowser_ui_strings.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// Windows implementation of version update functionality, used by the WebUI
// About/Help page. See the file comment above for why this doesn't use
// Google Update.
class VersionUpdaterWin : public VersionUpdater {
 public:
  explicit VersionUpdaterWin(content::WebContents* web_contents)
      : web_contents_(web_contents ? web_contents->GetWeakPtr()
                                   : base::WeakPtr<content::WebContents>()) {}

  VersionUpdaterWin(const VersionUpdaterWin&) = delete;
  VersionUpdaterWin& operator=(const VersionUpdaterWin&) = delete;

  ~VersionUpdaterWin() override = default;

  // VersionUpdater:
  void CheckForUpdate(StatusCallback callback, PromoteCallback) override {
    // Disconnect from any previous attempt to avoid redundant callbacks.
    weak_factory_.InvalidateWeakPtrs();
    callback_ = std::move(callback);

    callback_.Run(CHECKING, 0, false, false, std::string(), 0,
                  std::u16string());
    chrome::startup::nbrowser_update::CheckForUpdate(
        version_info::GetVersion(),
        base::BindOnce(&VersionUpdaterWin::OnCheckComplete,
                       weak_factory_.GetWeakPtr()));
  }

 private:
  void OnCheckComplete(
      std::optional<chrome::startup::nbrowser_update::AvailableUpdate>
          update) {
    if (!update) {
      // No newer release on GitHub - already up to date. NBrowser has no
      // background self-update, so unlike upstream there's no separate
      // "pending restart" state to check for here.
      callback_.Run(UPDATED, 0, false, false, std::string(), 0,
                    std::u16string());
      return;
    }

    if (!g_browser_process->local_state()->GetBoolean(
            chrome::startup::nbrowser_update::prefs::
                kUpdateAutoInstallEnabled)) {
      // The user hasn't opted into unattended updates - don't silently
      // download and run an installer just because this page was opened.
      // Defer to the same consent-gated infobar the startup path uses
      // (which re-checks GitHub itself); this page just reflects that an
      // update is pending rather than tracking its own separate progress.
      if (web_contents_) {
        chrome::startup::nbrowser_update::MaybeShowUpdatePrompt(
            web_contents_.get());
      }
      callback_.Run(DEFERRED, 0, false, false, std::string(), 0,
                    l10n_util::GetStringUTF16(
                        IDS_NBROWSER_UPDATE_AVAILABLE_ABOUT_PAGE_TEXT));
      return;
    }

    callback_.Run(UPDATING, 0, false, false, std::string(), 0,
                  std::u16string());
    chrome::startup::nbrowser_update::DownloadAndLaunchInstaller(
        update->installer_download_url,
        base::BindOnce(&VersionUpdaterWin::OnInstallerLaunched,
                       weak_factory_.GetWeakPtr()));
  }

  void OnInstallerLaunched(bool success) {
    if (!success) {
      callback_.Run(
          FAILED, 0, false, false, std::string(), 0,
          l10n_util::GetStringUTF16(
              IDS_NBROWSER_UPDATE_INSTALLER_LAUNCH_FAILED));
      return;
    }

    // The installer has taken over (it runs as its own visible process,
    // separate from this one) and, for the common per-user install, is
    // typically done within seconds. NEARLY_UPDATED is the closest existing
    // status to "an update is ready, relaunch to pick it up" - clicking the
    // resulting "Relaunch" button restarts this process, which by then
    // picks up whatever the installer just wrote in place.
    callback_.Run(NEARLY_UPDATED, 0, false, false, std::string(), 0,
                  std::u16string());
  }

  // Callback used to communicate update status to the client.
  StatusCallback callback_;

  // The chrome://settings/help tab this instance serves - used to attach
  // the consent infobar (see OnCheckComplete()) to the same tab when the
  // user hasn't opted into unattended updates. May be null (per the base
  // class's Create() contract) or have since been closed; both are handled
  // as "don't show anything" rather than a crash.
  base::WeakPtr<content::WebContents> web_contents_;

  // Used for callbacks.
  base::WeakPtrFactory<VersionUpdaterWin> weak_factory_{this};
};

}  // namespace

std::unique_ptr<VersionUpdater> VersionUpdater::Create(
    content::WebContents* web_contents) {
  return std::make_unique<VersionUpdaterWin>(web_contents);
}
