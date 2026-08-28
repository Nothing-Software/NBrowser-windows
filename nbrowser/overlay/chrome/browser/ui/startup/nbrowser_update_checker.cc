// Copyright 2026 The NBrowser Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/nbrowser_update_checker.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/values.h"
#include "base/version.h"
#include "base/win/windows_version.h"
#include "chrome/browser/browser_process.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace chrome::startup::nbrowser_update {

AvailableUpdate::AvailableUpdate() = default;
AvailableUpdate::AvailableUpdate(const AvailableUpdate&) = default;
AvailableUpdate& AvailableUpdate::operator=(const AvailableUpdate&) = default;
AvailableUpdate::~AvailableUpdate() = default;

namespace {

// GitHub's REST endpoint for the single most recently published release of
// the NBrowser-windows repository. Anonymous requests are capped at
// 60/hour, far more than the "once per browser launch" call pattern here
// needs.
constexpr char kLatestReleaseUrl[] =
    "https://api.github.com/repos/Nothing-Software/NBrowser-windows/"
    "releases/latest";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("nbrowser_update_check", R"(
        semantics {
          sender: "NBrowser Update Checker"
          description:
            "Once per browser launch, queries the public GitHub Releases "
            "API for the NBrowser-windows repository to learn the latest "
            "published NBrowser version. If it is newer than the running "
            "version, an infobar offers the user an update."
          trigger: "Once, at browser startup, right after the first "
            "browser window's startup infobars are populated."
          data: "No user data, cookies, or identifiers are sent - the "
            "request is a plain GET to a fixed, parameter-free REST "
            "endpoint."
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

// Returns the architecture suffix NBrowser's packaging script (package.py)
// embeds in installer filenames, for the architecture this machine's OS
// actually runs - not what this binary happens to be built for. See
// OSInfo::GetArchitecture()'s own documentation for why that distinction
// matters on ARM64 hosts.
std::string_view GetTargetCpuSuffix() {
  switch (base::win::OSInfo::GetArchitecture()) {
    case base::win::OSInfo::X86_ARCHITECTURE:
      return "x86";
    case base::win::OSInfo::ARM64_ARCHITECTURE:
      return "arm64";
    case base::win::OSInfo::X64_ARCHITECTURE:
    case base::win::OSInfo::IA64_ARCHITECTURE:
    case base::win::OSInfo::OTHER_ARCHITECTURE:
      return "x64";
  }
  NOTREACHED();
}

// Sends a single GET to `url` and invokes `callback` with the response
// body, or std::nullopt on any network/HTTP failure. Mirrors the fetch
// pattern used by version_history_client.cc.
void FetchUrl(GURL url,
             base::OnceCallback<void(std::optional<std::string>)> callback) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = std::move(url);
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->priority = net::IDLE;

  auto url_loader = network::SimpleURLLoader::Create(std::move(request),
                                                      kTrafficAnnotation);
  url_loader->SetRetryOptions(
      /*max_retries=*/3,
      network::SimpleURLLoader::RetryMode::RETRY_ON_NETWORK_CHANGE);

  network::SimpleURLLoader* url_loader_raw = url_loader.get();
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory =
      g_browser_process->shared_url_loader_factory();
  url_loader_raw->DownloadToString(
      url_loader_factory.get(),
      // Bind `url_loader` into the callback so the request stays alive
      // until it completes, then let it go once the body (or failure) has
      // been captured.
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> url_loader,
             base::OnceCallback<void(std::optional<std::string>)> callback,
             std::optional<std::string> body) {
            std::move(callback).Run(std::move(body));
          },
          std::move(url_loader), std::move(callback)),
      /*max_body_size=*/1024 * 1024);
}

// Picks the download URL of the installer asset named `expected_name` out
// of a GitHub release "assets" array, or an invalid GURL if none matches.
GURL FindAssetDownloadUrl(const base::ListValue& assets,
                          std::string_view expected_name) {
  for (const base::Value& asset : assets) {
    if (!asset.is_dict()) {
      continue;
    }
    const std::string* name = asset.GetDict().FindString("name");
    const std::string* download_url =
        asset.GetDict().FindString("browser_download_url");
    if (name && *name == expected_name && download_url) {
      return GURL(*download_url);
    }
  }
  return GURL();
}

// Interprets the already-parsed release JSON, comparing it against
// `current_version` and resolving this machine's installer asset.
std::optional<AvailableUpdate> InterpretRelease(
    const base::Version& current_version,
    const base::DictValue& release) {
  const std::string* tag_name = release.FindString("tag_name");
  const base::ListValue* assets = release.FindList("assets");
  if (!tag_name || !assets) {
    return std::nullopt;
  }

  // NBrowser tags releases as "{chromium_version}-{release_revision}."
  // "{packaging_revision}" (e.g. "151.0.7922.173-1.1"). Only the part
  // before the first '-' is a Chromium version, and per NBrowser's
  // one-release-per-Chromium-version policy that is the only part ever
  // compared against the running version.
  const size_t dash = tag_name->find('-');
  if (dash == std::string::npos) {
    return std::nullopt;
  }
  base::Version release_version(tag_name->substr(0, dash));
  if (!release_version.IsValid() ||
      release_version.CompareTo(current_version) <= 0) {
    return std::nullopt;
  }

  const std::string expected_asset_name = base::StrCat(
      {"nbrowser_", *tag_name, "_installer_", GetTargetCpuSuffix(), ".exe"});
  GURL download_url = FindAssetDownloadUrl(*assets, expected_asset_name);
  if (!download_url.is_valid()) {
    return std::nullopt;
  }

  AvailableUpdate update;
  update.tag_name = *tag_name;
  update.installer_download_url = std::move(download_url);
  return update;
}

void OnJsonParsed(base::Version current_version,
                  CheckForUpdateCallback callback,
                  data_decoder::DataDecoder::ValueOrError result) {
  if (!result.has_value() || !result->is_dict()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(
      InterpretRelease(current_version, result->GetDict()));
}

void OnReleaseFetched(base::Version current_version,
                      CheckForUpdateCallback callback,
                      std::optional<std::string> raw_data) {
  if (!raw_data) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  data_decoder::DataDecoder::ParseJsonIsolated(
      *raw_data, base::BindOnce(&OnJsonParsed, std::move(current_version),
                                std::move(callback)));
}

}  // namespace

void CheckForUpdate(const base::Version& current_version,
                    CheckForUpdateCallback callback) {
  FetchUrl(GURL(kLatestReleaseUrl),
          base::BindOnce(&OnReleaseFetched, current_version,
                          std::move(callback)));
}

}  // namespace chrome::startup::nbrowser_update
