// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/nbrowser_update_prompt.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/startup/nbrowser_update_checker.h"
#include "chrome/browser/ui/startup/nbrowser_update_infobar_delegate.h"
#include "chrome/browser/ui/startup/nbrowser_update_prefs.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_contents.h"

namespace chrome::startup::nbrowser_update {

namespace {

void OnUpdateCheckComplete(base::WeakPtr<content::WebContents> web_contents,
                           std::optional<AvailableUpdate> update) {
  // The user already declined exactly this release on an earlier launch -
  // treat it the same as "no update" for both the infobar and the nav dot.
  const bool already_declined =
      update &&
      update->tag_name == g_browser_process->local_state()->GetString(
                              prefs::kUpdateIgnoredReleaseTag);
  g_browser_process->local_state()->SetBoolean(
      prefs::kUpdateAvailableDotVisible, update && !already_declined);

  if (!update || already_declined || !web_contents) {
    return;
  }

  infobars::ContentInfoBarManager* infobar_manager =
      infobars::ContentInfoBarManager::FromWebContents(web_contents.get());
  if (!infobar_manager) {
    return;
  }

  NBrowserUpdateInfoBarDelegate::Create(infobar_manager, std::move(*update));
}

}  // namespace

void MaybeShowUpdatePrompt(content::WebContents* web_contents) {
  if (!web_contents) {
    return;
  }

  CheckForUpdate(version_info::GetVersion(),
                base::BindOnce(&OnUpdateCheckComplete,
                                web_contents->GetWeakPtr()));
}

}  // namespace chrome::startup::nbrowser_update
