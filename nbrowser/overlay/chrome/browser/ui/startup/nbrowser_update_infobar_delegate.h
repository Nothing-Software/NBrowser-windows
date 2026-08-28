// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_INFOBAR_DELEGATE_H_
#define CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_INFOBAR_DELEGATE_H_

#include "base/types/pass_key.h"
#include "chrome/browser/ui/startup/nbrowser_update_checker.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/confirm_infobar_delegate.h"

// The delegate for the infobar shown when a newer NBrowser release is
// available on GitHub. Ownership of the delegate is given to the infobar
// itself, the lifetime of which is bound to the containing WebContents.
//
// Accepting downloads and launches the release's installer (see
// nbrowser_update_installer_launcher.h). Dismissing it - including via the
// infobar's own close button, not just a dedicated "not now" button -
// records the release's tag in Local State so this exact release is never
// prompted for again, while a later release still will be.
class NBrowserUpdateInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  // Creates an NBrowser update infobar and delegate and adds the infobar to
  // `infobar_manager`.
  static infobars::InfoBar* Create(
      infobars::ContentInfoBarManager* infobar_manager,
      chrome::startup::nbrowser_update::AvailableUpdate update);

  NBrowserUpdateInfoBarDelegate(const NBrowserUpdateInfoBarDelegate&) = delete;
  NBrowserUpdateInfoBarDelegate& operator=(
      const NBrowserUpdateInfoBarDelegate&) = delete;

  NBrowserUpdateInfoBarDelegate(
      base::PassKey<NBrowserUpdateInfoBarDelegate>,
      chrome::startup::nbrowser_update::AvailableUpdate update);
  ~NBrowserUpdateInfoBarDelegate() override;

 private:
  // ConfirmInfoBarDelegate:
  infobars::InfoBarDelegate::InfoBarIdentifier GetIdentifier() const override;
  const gfx::VectorIcon& GetVectorIcon() const override;
  bool ShouldExpire(const NavigationDetails& details) const override;
  void InfoBarDismissed() override;
  std::u16string GetMessageText() const override;
  int GetButtons() const override;
  std::u16string GetButtonLabel(InfoBarButton button) const override;
  bool Accept() override;
  bool ShouldHideInFullscreen() const override;

  const chrome::startup::nbrowser_update::AvailableUpdate update_;
};

#endif  // CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_INFOBAR_DELEGATE_H_
