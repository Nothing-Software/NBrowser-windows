// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/nbrowser_update_infobar_delegate.h"

#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/types/pass_key.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/infobars/confirm_infobar_creator.h"
#include "chrome/browser/ui/startup/nbrowser_update_installer_launcher.h"
#include "chrome/browser/ui/startup/nbrowser_update_prefs.h"
#include "chrome/browser/ui/webui/nbrowser_ui_strings/grit/nbrowser_ui_strings.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_features.h"

// static
infobars::InfoBar* NBrowserUpdateInfoBarDelegate::Create(
    infobars::ContentInfoBarManager* infobar_manager,
    chrome::startup::nbrowser_update::AvailableUpdate update) {
  return infobar_manager->AddInfoBar(
      CreateConfirmInfoBar(std::make_unique<NBrowserUpdateInfoBarDelegate>(
          base::PassKey<NBrowserUpdateInfoBarDelegate>(), std::move(update))));
}

NBrowserUpdateInfoBarDelegate::NBrowserUpdateInfoBarDelegate(
    base::PassKey<NBrowserUpdateInfoBarDelegate>,
    chrome::startup::nbrowser_update::AvailableUpdate update)
    : update_(std::move(update)) {}

NBrowserUpdateInfoBarDelegate::~NBrowserUpdateInfoBarDelegate() = default;

infobars::InfoBarDelegate::InfoBarIdentifier
NBrowserUpdateInfoBarDelegate::GetIdentifier() const {
  return NBROWSER_UPDATE_AVAILABLE_INFOBAR_DELEGATE;
}

const gfx::VectorIcon& NBrowserUpdateInfoBarDelegate::GetVectorIcon() const {
  return features::IsRoundedIconsEnabled()
             ? kRocketLaunchIcon
             : kBrowserToolsUpdateChromeRefreshOldIcon;
}

bool NBrowserUpdateInfoBarDelegate::ShouldExpire(
    const NavigationDetails& details) const {
  return false;
}

void NBrowserUpdateInfoBarDelegate::InfoBarDismissed() {
  PrefService* local_state = g_browser_process->local_state();

  // The user declined this specific release. Remember its tag so this
  // infobar doesn't come back for the same release on a future launch - a
  // newer release will still prompt normally.
  local_state->SetString(
      chrome::startup::nbrowser_update::prefs::kUpdateIgnoredReleaseTag,
      update_.tag_name);

  // Clear the nav-menu dot immediately - it's only otherwise recomputed the
  // next time a full CheckForUpdate cycle runs (next launch, or the next
  // "Test" click), so without this it would keep showing this already-
  // declined release as if it were still unhandled.
  local_state->SetBoolean(
      chrome::startup::nbrowser_update::prefs::kUpdateAvailableDotVisible,
      false);

  ConfirmInfoBarDelegate::InfoBarDismissed();
}

std::u16string NBrowserUpdateInfoBarDelegate::GetMessageText() const {
  // Deliberately no version number interpolated into this string - see the
  // comment on IDS_NBROWSER_UPDATE_AVAILABLE_INFOBAR_TEXT in
  // nbrowser_ui_strings.grd for why it stays a placeholder-free constant.
  return l10n_util::GetStringUTF16(IDS_NBROWSER_UPDATE_AVAILABLE_INFOBAR_TEXT);
}

int NBrowserUpdateInfoBarDelegate::GetButtons() const {
  return BUTTON_OK;
}

std::u16string NBrowserUpdateInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  DCHECK_EQ(BUTTON_OK, button);
  return l10n_util::GetStringUTF16(
      IDS_NBROWSER_UPDATE_AVAILABLE_INFOBAR_BUTTON_LABEL);
}

bool NBrowserUpdateInfoBarDelegate::Accept() {
  // The user is acting on this update now - same reasoning as
  // InfoBarDismissed() for clearing the dot immediately rather than
  // leaving it stale until the next check cycle.
  g_browser_process->local_state()->SetBoolean(
      chrome::startup::nbrowser_update::prefs::kUpdateAvailableDotVisible,
      false);

  // Fire-and-forget: the user explicitly consented, the download runs in
  // the background, and the installer - once launched - takes over with
  // its own UI. There is nothing left for this infobar to report back.
  chrome::startup::nbrowser_update::DownloadAndLaunchInstaller(
      update_.installer_download_url, base::DoNothing());

  return ConfirmInfoBarDelegate::Accept();
}

bool NBrowserUpdateInfoBarDelegate::ShouldHideInFullscreen() const {
  return true;
}
