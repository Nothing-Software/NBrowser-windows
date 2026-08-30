// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_PROMPT_H_
#define CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_PROMPT_H_

namespace content {
class WebContents;
}

namespace chrome::startup::nbrowser_update {

// Checks GitHub Releases for a newer NBrowser version and, if one exists
// and the user hasn't already dismissed that exact release, shows the
// update infobar on `web_contents`. Safe to call with a `web_contents` that
// gets destroyed before the (asynchronous) check completes - the infobar
// is simply not shown in that case, rather than accessed unsafely.
//
// Called at most once per browser launch, from AddInfoBarsIfNecessary() in
// infobar_utils.cc, which already guarantees single-invocation-per-launch
// for the whole startup infobar block this is called from - see the
// `static bool infobars_shown` guard there.
void MaybeShowUpdatePrompt(content::WebContents* web_contents);

}  // namespace chrome::startup::nbrowser_update

#endif  // CHROME_BROWSER_UI_STARTUP_NBROWSER_UPDATE_PROMPT_H_
