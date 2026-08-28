// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/nbrowser_update_installer_launcher.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/install_static/install_util.h"
#include "chrome/installer/util/install_util.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace chrome::startup::nbrowser_update {

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("nbrowser_update_download", R"(
        semantics {
          sender: "NBrowser Update Installer Downloader"
          description:
            "Downloads the NBrowser installer for a release the user was "
            "just told is available and explicitly chose to install, from "
            "the NBrowser-windows GitHub Releases page."
          trigger: "The user clicked the accept button on the update "
            "infobar."
          data: "No user data, cookies, or identifiers are sent - the URL "
            "is a fixed, publicly-listed release asset download link "
            "returned by the update check."
          destination: WEBSITE
          user_data {
            type: NONE
          }
          internal {
            contacts {
              email: "dev@nothing-software.example"
            }
          }
          last_reviewed: "2026-08-28"
        }
        policy {
          cookies_allowed: NO
          setting: "This feature cannot be disabled by settings."
          policy_exception_justification:
            "Not implemented yet. NBrowser has no enterprise policy "
            "support."
        })");

// Installers built by package.py are well under this; it exists only as a
// sanity cap against a misbehaving or compromised server, not a realistic
// expected size.
constexpr int64_t kMaxInstallerBytes = 300 * 1024 * 1024;

bool LaunchElevated(base::CommandLine cmd) {
  // Blocks until the elevated process exits - Windows has no non-blocking
  // "runas" primitive - which is why this only ever runs on a MayBlock()
  // thread pool task, never on the calling sequence.
  return InstallUtil::ExecuteExeAsAdmin(cmd, /*exit_code=*/nullptr);
}

void LaunchDownloadedInstaller(DownloadAndLaunchInstallerCallback callback,
                               base::FilePath installer_path) {
  if (installer_path.empty()) {
    std::move(callback).Run(false);
    return;
  }

  base::CommandLine cmd(installer_path);

  // NBrowser installs per-user by default, which needs no elevation at
  // all; system-level installs are the exception, kept only for
  // completeness.
  if (install_static::IsSystemInstall()) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&LaunchElevated, std::move(cmd)), std::move(callback));
    return;
  }

  base::Process process = base::LaunchProcess(cmd, base::LaunchOptions());
  std::move(callback).Run(process.IsValid());
}

}  // namespace

void DownloadAndLaunchInstaller(const GURL& installer_download_url,
                                DownloadAndLaunchInstallerCallback callback) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = installer_download_url;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(std::move(request),
                                                      kTrafficAnnotation);
  network::SimpleURLLoader* url_loader_raw = url_loader.get();
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory =
      g_browser_process->shared_url_loader_factory();

  url_loader_raw->DownloadToTempFile(
      url_loader_factory.get(),
      // Bind `url_loader` into the callback so the request stays alive
      // until it completes.
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> url_loader,
             DownloadAndLaunchInstallerCallback callback,
             base::FilePath path) {
            LaunchDownloadedInstaller(std::move(callback), std::move(path));
          },
          std::move(url_loader), std::move(callback)),
      kMaxInstallerBytes);
}

}  // namespace chrome::startup::nbrowser_update
