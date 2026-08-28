// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_CHECKER_H_
#define CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_CHECKER_H_

#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/version.h"
#include "url/gurl.h"

namespace chrome::startup::nbrowser_update {

// A published NBrowser release that is newer than the version currently
// running, together with enough information to let the user download and
// launch its installer for this machine's architecture.
struct AvailableUpdate {
  AvailableUpdate();
  AvailableUpdate(const AvailableUpdate&);
  AvailableUpdate& operator=(const AvailableUpdate&);
  ~AvailableUpdate();

  // Full release tag as published on GitHub, e.g. "151.0.7922.173-1.1".
  // Identifies this exact release for the "don't show this one again" pref
  // - not compared as a version number (the Chromium-version prefix already
  // was, to determine an update is available at all; see CheckForUpdate).
  std::string tag_name;

  // Direct download URL of the installer asset matching this machine's
  // architecture (x64/x86/arm64), resolved from the release's asset list.
  GURL installer_download_url;
};

using CheckForUpdateCallback =
    base::OnceCallback<void(std::optional<AvailableUpdate>)>;

// Queries the NBrowser-windows GitHub Releases API exactly once and invokes
// `callback` with the latest published release, but only if all of the
// following hold: the release's version is strictly newer than
// `current_version`, its tag parses as a valid version, and it ships an
// installer asset for the running machine's architecture. Otherwise invokes
// `callback` with std::nullopt - including on any network, parsing, or
// version-mismatch failure, all of which are treated identically as "no
// update available" rather than surfaced as errors.
//
// This performs a single anonymous HTTPS GET with no retries beyond
// SimpleURLLoader's default network-change retry, no polling, and no
// telemetry. Callers are expected to invoke this at most once per browser
// launch (see AddInfoBarsIfNecessary in infobar_utils.cc, which already
// guarantees single-invocation-per-launch for the whole startup infobar
// block this is called from).
void CheckForUpdate(const base::Version& current_version,
                    CheckForUpdateCallback callback);

// TEMPORARY TEST-ONLY HOOK - remove together with the "Test" button on
// chrome://settings/help (see the NBROWSER_TEST_ONLY-marked block in
// about_handler.cc/.h and about_page.ts/.html.ts/about_page_browser_proxy.ts)
// once a real GitHub release exists to test against.
//
// While enabled, CheckForUpdate() reports a synthetic update (still
// asynchronously, still through the exact same code every real caller
// uses) instead of querying GitHub, so the full flow - startup infobar,
// the chrome://settings/help status block, and the nav menu's update dot -
// can be exercised without publishing a release. Not persisted: resets to
// disabled on every browser restart.
void SetMockUpdateForTesting(bool available);

}  // namespace chrome::startup::nbrowser_update

#endif  // CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_CHECKER_H_
