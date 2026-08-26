#ifndef CHROME_BROWSER_UI_WEBUI_UNGOOGLED_FIRST_RUN_H_
#define CHROME_BROWSER_UI_WEBUI_UNGOOGLED_FIRST_RUN_H_

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/spellchecker/spellcheck_service.h"
#include "chrome/browser/ui/webui/nbrowser_first_run/grit/nbrowser_first_run_strings.h"
#include "components/language/core/browser/language_prefs.h"
#include "components/language/core/browser/pref_names.h"
#include "components/live_caption/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/spellcheck/browser/pref_names.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace {

// prepopulate_id values from
// third_party/search_engines_data/resources/definitions/prepopulated_engines.json
// (restored for Google by windows-nbrowser-restore-google-search.patch;
// Yandex's regional variants - yandex_ru, yandex_com, etc. - all share id 15).
constexpr int kGooglePrepopulateId = 1;
constexpr int kYandexPrepopulateId = 15;
constexpr int kDuckDuckGoPrepopulateId = 92;
constexpr int kBravePrepopulateId = 109;

// Auto-configuration applied when the user turns on "enhanced translator" /
// "auto-translate pages" on screen 2, per the product decision: translate to
// Russian, always auto-translate English pages, never prompt to translate
// Russian pages (into themselves).
constexpr char kTranslateTargetLanguage[] = "ru";
constexpr char kTranslateAutoSourceLanguage[] = "en";

// Local State (not per-profile - matches how settings::LanguagesHandler
// stores the UI locale on Windows) flags for screen 0, the language picker.
// Registered by windows-nbrowser-first-run-language-prefs.patch.
// kLanguageChosen persists once the user has been through screen 0.
// kPendingRelaunch is transient: set right before we restart the browser to
// apply a language change, consumed once by the chrome_browser_main.cc hook
// (see windows-nbrowser-first-run-url.patch) to reopen this page one more
// time even though the real first-run sentinel already exists by then.
constexpr char kLanguageChosenPref[] = "nbrowser.first_run_language_chosen";
constexpr char kPendingRelaunchPref[] = "nbrowser.first_run_pending_relaunch";

struct LangOption {
  const char* code;
  const char* native_name;
};
constexpr LangOption kLangOptions[] = {
    {"en-US", "English (United States)"},
    {"ru", "Русский"},
    {"de", "Deutsch"},
    {"fr", "Français"},
    {"es", "Español"},
    {"zh-CN", "中文（简体）"},
};

std::string EngineKeyForPrepopulateId(int id) {
  switch (id) {
    case kGooglePrepopulateId: return "google";
    case kYandexPrepopulateId: return "yandex";
    case kDuckDuckGoPrepopulateId: return "duckduckgo";
    case kBravePrepopulateId: return "brave";
    default: return std::string();
  }
}

// Resolves a first-run engine key ("brave"/"duckduckgo"/"yandex") to a real
// TemplateURL, adding a minimal prepopulated-style entry if this build's
// country-specific engine list doesn't already include it (ungoogled-chromium
// derived builds can trim the prepopulated list per country). Google isn't
// handled here - it's restored directly into prepopulated_engines.json, so a
// plain keyword lookup is always enough for it.
TemplateURL* ResolveSearchEngine(TemplateURLService* service,
                                 const std::string& key) {
  struct KnownEngine {
    const char16_t* name;
    const char16_t* keyword;
    const char* alt_keyword;  // second keyword to also try, or nullptr
    const char* search_url;
    const char* suggest_url;
    const char* favicon_url;
    int prepopulate_id;
  };
  static constexpr KnownEngine kBrave = {
      u"Brave", u"search.brave.com", nullptr,
      "https://search.brave.com/search?q={searchTerms}",
      "https://search.brave.com/api/suggest?q={searchTerms}",
      "https://cdn.search.brave.com/serp/favicon.ico", kBravePrepopulateId};
  static constexpr KnownEngine kDuckDuckGo = {
      u"DuckDuckGo", u"duckduckgo.com", nullptr,
      "https://duckduckgo.com/?q={searchTerms}",
      "https://duckduckgo.com/ac/?q={searchTerms}&type=list",
      "https://duckduckgo.com/favicon.ico", kDuckDuckGoPrepopulateId};
  static constexpr KnownEngine kYandex = {
      u"\u042F\u043D\u0434\u0435\u043A\u0441", u"yandex.ru", "yandex.com",
      "https://yandex.ru/search/?text={searchTerms}",
      "https://suggest.yandex.ru/suggest-ff.cgi?part={searchTerms}",
      "https://yastatic.net/lego/_/pDu9OWAQKB0s2J9IojKpiS_Eho.ico",
      kYandexPrepopulateId};

  const KnownEngine* known = nullptr;
  if (key == "brave") known = &kBrave;
  else if (key == "duckduckgo") known = &kDuckDuckGo;
  else if (key == "yandex") known = &kYandex;
  if (!known)
    return nullptr;

  if (TemplateURL* existing = service->GetTemplateURLForKeyword(known->keyword))
    return existing;
  if (known->alt_keyword) {
    if (TemplateURL* alt = service->GetTemplateURLForKeyword(
            base::UTF8ToUTF16(std::string(known->alt_keyword))))
      return alt;
  }

  TemplateURLData data;
  data.SetShortName(known->name);
  data.SetKeyword(known->keyword);
  data.SetURL(known->search_url);
  data.suggestions_url = known->suggest_url;
  data.favicon_url = GURL(known->favicon_url);
  data.prepopulate_id = known->prepopulate_id;
  data.safe_for_autoreplace = true;
  return service->Add(std::make_unique<TemplateURL>(data));
}

// Fixes the prepopulated "google.com" entry's search_url/suggest_url in
// place if they still use {google:baseURL}-style macros, which resolve
// through google_util::kGoogleHomepageURL - a compiled-in constant that
// ungoogled-chromium's domain substitution build step rewrites to a
// non-resolving *.qjz9zk host. Chromium hardcodes Google as the universal
// prepopulated fallback default search engine (independent of locale), so a
// fresh profile already has this broken entry as its default *before* the
// user ever touches the wizard - a plain click handler that only fires on
// an explicit radio change would miss that case. Idempotent (no-ops once
// already fixed), so safe to call unconditionally on every page render.
void EnsureGoogleSearchUrlIsUsable(TemplateURLService* service) {
  TemplateURL* google = service->GetTemplateURLForKeyword(u"google.com");
  if (!google || google->url().find("{google:baseURL}") == std::string::npos)
    return;
  service->ResetTemplateURL(
      google, u"Google", u"google.com",
      "https://www.google.com/search?q={searchTerms}&ie={inputEncoding}",
      "https://www.google.com/complete/search?client=chrome&q={searchTerms}");
}

// CSS for the whole page. Uses the standard chrome://resources text styling
// baseline, then the page's own rules; no brand accent color is used (the
// NBrowser identity is strictly monochrome), so light/dark theming, the
// fixed-dark hero band, the ring/spokes watermark (echoing the product mark
// itself) and per-card icons do the visual work instead of color.
constexpr char kNbrowserFirstRunCss[] = R"(
:root{
 --bg:#f4f4f5;--surface:#fff;--surface-sunken:#fafafa;--border:#e4e4e7;--border-strong:#d0d0d5;
 --text:#101114;--text-muted:#6c6e76;--text-faint:#8f9198;
 --hero-bg:#101114;--hero-text:#fff;--hero-muted:#a1a3ab;--hero-mark-bg:#1c1d21;
 --btn-bg:#101114;--btn-text:#fff;--btn-bg-hover:#26272c;
 --btn-invert-bg:#fff;--btn-invert-text:#101114;--btn-invert-hover:#e8e8ea;
 --track-off:#d4d4d9;--track-on:#101114;--knob:#fff;--knob-on:#fff;--hover:#f2f2f4;--focus:#101114;
 color-scheme:light dark;
}
@media(prefers-color-scheme:dark){
 :root{
  --bg:#0a0a0c;--surface:#15161a;--surface-sunken:#101114;--border:#26272c;--border-strong:#34353b;
  --text:#f2f2f3;--text-muted:#9a9ca4;--text-faint:#7c7e86;
  --hero-bg:#101114;--hero-text:#fff;--hero-muted:#a1a3ab;--hero-mark-bg:#1e1f24;
  --btn-bg:#f2f2f3;--btn-text:#101114;--btn-bg-hover:#fff;
  --btn-invert-bg:#f2f2f3;--btn-invert-text:#101114;--btn-invert-hover:#fff;
  --track-off:#35363c;--track-on:#f2f2f3;--knob:#fff;--knob-on:#101114;--hover:#1c1d21;--focus:#f2f2f3;
 }
}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--text);
 font-family:system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;
 font-size:14px;line-height:1.5;-webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility}
a{color:var(--text);text-decoration:underline;text-underline-offset:2px}
a:hover{color:var(--text-muted)}
h1,h2,h3{font-weight:500;margin:0 0 .5em}
p{margin:0 0 1em}

/* ---------- wizard shell ---------- */
.wizard{min-height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;
 gap:28px;padding:48px 24px}
.stage{position:relative;width:100%;max-width:76em;overflow:hidden;
 transition:height 340ms cubic-bezier(.22,.61,.36,1)}
.screen{position:absolute;top:0;left:0;width:100%;opacity:0;visibility:hidden;pointer-events:none;
 transform:translateY(14px);
 transition:opacity 300ms ease,transform 340ms cubic-bezier(.22,.61,.36,1),visibility 340ms}
.screen[data-state="active"]{opacity:1;visibility:visible;pointer-events:auto;transform:none}
/* Screen 6's stage width snaps instantly (not animated, see comment on .stage
   above) - it fades in fast instead so the swap reads as one clean cut rather
   than a stretch or a slow reveal. */
.screen[data-screen="6"]{transition-duration:200ms}
.screen[data-state="done"]{transform:translateY(-14px)}
.screen[data-state="off"],.screen[hidden]{display:none}

/* ---------- hero (screen 1) ---------- */
.hero{max-width:560px;margin:0 auto;background:var(--hero-bg);color:var(--hero-text);
 border:1px solid rgba(255,255,255,.06);border-radius:16px;padding:56px 44px 44px;text-align:center}
.hero__mark{width:60px;height:60px;margin:0 auto 28px}
.hero__mark svg{width:100%;height:100%;display:block}
.hero__title{margin:0;font-size:26px;line-height:1.25;font-weight:600;letter-spacing:-.01em;
 text-wrap:pretty}
.hero__sub{margin:12px auto 0;max-width:380px;color:var(--hero-muted);font-size:14px;text-wrap:pretty}
.actions.actions--hero{margin-top:32px;display:flex;justify-content:center}

/* ---------- language picker (screen 0) ---------- */
.lang-title{margin:0;display:flex;flex-direction:column;align-items:center;gap:.15em;
 font-size:26px;line-height:1.25;font-weight:600;letter-spacing:-.01em;text-wrap:pretty}
.lang-wheel{position:relative;width:7.5em;height:1.3em;overflow:hidden}
.lang-wheel__word{position:absolute;left:0;right:0;top:0;text-align:center;transform:translateY(0);opacity:1;
 transition:transform .5s cubic-bezier(.22,.61,.36,1),opacity .5s ease}
.lang-wheel__word--enter{transform:translateY(100%);opacity:0}
.lang-wheel__word--leave{transform:translateY(-100%);opacity:0}
.lang-picker{margin:20px auto 0;max-width:280px}
.lang-select{width:100%;appearance:none;border:1px solid rgba(255,255,255,.16);border-radius:10px;
 padding:11px 36px 11px 14px;font:inherit;font-size:14px;background:var(--hero-mark-bg);color:var(--hero-text);
 cursor:pointer;
 background-image:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="none" stroke="%23a1a3ab" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 7.5l5 5 5-5"/></svg>');
 background-repeat:no-repeat;background-position:right 12px center;background-size:16px}
.lang-select:focus-visible{outline:2px solid var(--hero-text);outline-offset:2px}
.wizard.is-entering .screen--welcome[data-state="active"] [data-reveal]{
 animation:reveal 520ms cubic-bezier(.22,.61,.36,1) both}
.wizard.is-entering .screen--welcome[data-state="active"] .hero__mark{
 animation:reveal 520ms cubic-bezier(.22,.61,.36,1) both}
.wizard.is-entering .screen--welcome[data-state="active"] [data-reveal]:nth-of-type(1){animation-delay:120ms}
.wizard.is-entering .screen--welcome[data-state="active"] [data-reveal]:nth-of-type(2){animation-delay:200ms}
.wizard.is-entering .screen--welcome[data-state="active"] [data-reveal]:nth-of-type(3){animation-delay:280ms}
@keyframes reveal{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}

/* ---------- card (screens 2-5) ---------- */
.card{max-width:560px;margin:0 auto;background:var(--surface);border:1px solid var(--border);
 border-radius:16px;padding:36px 36px 28px}
.title{margin:0;font-size:21px;line-height:1.3;font-weight:600;letter-spacing:-.01em;text-wrap:pretty}
.text{margin:10px 0 0;color:var(--text-muted);text-wrap:pretty}
.list{margin:16px 0 0;padding:0;list-style:none;display:grid;gap:10px}
.list li{position:relative;padding-left:18px;color:var(--text-muted);text-wrap:pretty}
.list li::before{content:"";position:absolute;left:2px;top:9px;width:4px;height:4px;border-radius:50%;
 background:var(--border-strong)}
.notice{margin-top:22px;display:flex;gap:12px;padding:14px 16px;border:1px solid var(--border);
 border-radius:12px;background:var(--surface-sunken)}
.notice__icon{flex:0 0 auto;width:18px;height:18px;margin-top:2px;color:var(--text-muted)}
.notice__text{margin:0;font-size:13px;color:var(--text-muted);text-wrap:pretty}
.options{margin-top:24px;display:grid;gap:2px;border:1px solid var(--border);border-radius:12px;
 overflow:hidden}
.option{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:14px 16px;
 background:var(--surface);cursor:pointer;transition:background 140ms ease,opacity 140ms ease}
.option+.option{box-shadow:0 -1px 0 var(--border)}
.option:hover{background:var(--hover)}
.option[data-disabled="true"]{cursor:default;opacity:.5}
.option[data-disabled="true"]:hover{background:var(--surface)}
.option__label{font-size:14px;color:var(--text);text-wrap:pretty}
.option__hint{display:block;margin-top:2px;font-size:12px;color:var(--text-faint)}
.switch{position:relative;flex:0 0 auto;width:46px;height:28px}
.switch__input{position:absolute;inset:0;margin:0;opacity:0;cursor:pointer}
.switch__track{position:absolute;inset:0;border-radius:999px;background:var(--track-off);
 transition:background 200ms ease;pointer-events:none}
.switch__knob{position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:50%;
 background:var(--knob);box-shadow:0 1px 3px rgba(0,0,0,.28);
 transition:transform 200ms cubic-bezier(.22,.61,.36,1)}
.switch__input:checked+.switch__track{background:var(--track-on)}
.switch__input:checked+.switch__track .switch__knob{transform:translateX(18px);background:var(--knob-on)}
.switch__input:focus-visible+.switch__track{outline:2px solid var(--focus);outline-offset:2px}
.engines{margin-top:24px;display:grid;gap:8px}
.engine{position:relative;display:flex;align-items:center;gap:14px;padding:14px 16px;
 border:1px solid var(--border);border-radius:12px;background:var(--surface);cursor:pointer;
 transition:border-color 160ms ease,background 160ms ease}
.engine:hover{background:var(--hover)}
.engine[data-selected="true"]{border-color:var(--border-strong)}
.engine__input{position:absolute;opacity:0;width:0;height:0}
.engine__radio{flex:0 0 auto;width:18px;height:18px;border-radius:50%;
 border:1.5px solid var(--border-strong);transition:border-color 160ms ease,box-shadow 160ms ease}
.engine[data-selected="true"] .engine__radio{border-color:var(--text);
 box-shadow:inset 0 0 0 4px var(--surface),inset 0 0 0 12px var(--text)}
.engine__input:focus-visible+.engine__radio{outline:2px solid var(--focus);outline-offset:2px}
.engine__body{display:flex;flex-direction:column;min-width:0}
.engine__name{font-size:14px}
.engine__host{font-size:12px;color:var(--text-faint)}
.engine__badge{margin-left:auto;flex:0 0 auto;font-size:11px;letter-spacing:.02em;color:var(--text-muted);
 padding:4px 9px;border:1px solid var(--border);border-radius:999px;background:var(--surface-sunken);
 opacity:0;transform:translateY(-4px) scale(.96);
 transition:opacity 200ms ease,transform 240ms cubic-bezier(.22,.61,.36,1);pointer-events:none}
.engine[data-selected="true"] .engine__badge{opacity:1;transform:none}

/* ---------- buttons & steps ---------- */
.actions{margin-top:26px;display:flex;justify-content:flex-end;gap:8px}
.btn{appearance:none;border:1px solid transparent;border-radius:10px;padding:11px 22px;font:inherit;
 font-size:14px;font-weight:500;background:var(--btn-bg);color:var(--btn-text);cursor:pointer;
 transition:background 160ms ease,transform 120ms ease}
.btn:hover{background:var(--btn-bg-hover)}
.btn:active{transform:translateY(1px)}
.btn:focus-visible{outline:2px solid var(--focus);outline-offset:2px}
.btn--invert{background:var(--btn-invert-bg);color:var(--btn-invert-text)}
.btn--invert:hover{background:var(--btn-invert-hover)}
.btn--ghost{background:transparent;color:var(--text-muted);border-color:var(--border);margin-right:auto}
.btn--ghost:hover{background:var(--hover);color:var(--text)}
.steps{display:flex;gap:8px;align-items:center}
.steps__dot{width:6px;height:6px;border-radius:50%;background:var(--border-strong);
 transition:background 200ms ease,width 240ms cubic-bezier(.22,.61,.36,1)}
.steps__dot[data-state="active"]{width:20px;border-radius:999px;background:var(--text)}
.steps__dot[data-state="done"]{background:var(--text-faint)}
.steps__dot[hidden]{display:none}

/* ---------- screen 6: recap (recolored legacy welcome content) ---------- */
.legacy-page{padding:0 1.5em 1em}
section.hero{position:relative;overflow:hidden;text-align:center;padding:3.5em 1.5em;margin:0 -1.5em 2em;
 max-width:none;background:var(--hero-bg);color:var(--hero-text);border-radius:0 0 1em 1em}
.hero-mark{position:absolute;top:50%;right:calc(-8em - 10px);width:27em;height:27em;margin-top:-13.5em;
 opacity:.07;pointer-events:none}
.hero-mark,.hero-mark *{stroke:var(--hero-text)}
.hero-logo{position:relative;display:block;margin:0 auto 1.5em;height:3.75em;width:3.75em}
section.hero .lead{position:relative;color:var(--hero-muted)}
.lead{font-size:1.0625em;color:var(--text-muted);max-width:34em;margin:0 auto}
section.block{padding:2.5em 0;border-top:1px solid var(--border)}
.legacy-page h2{font-size:1.5em;display:flex;align-items:center;gap:.6em}
.section-index{flex:none;display:inline-flex;align-items:center;justify-content:center;
 width:1.9em;height:1.9em;border-radius:50%;border:1px solid var(--border-strong);color:var(--text-faint);
 font-size:.5em;font-weight:600;letter-spacing:.02em}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(15em,1fr));gap:1.25em;margin-top:1.5em}
.tile{background:var(--surface-sunken);border:1px solid var(--border);border-radius:.75em;
 padding:1.25em 1.375em;
 transition:transform .22s cubic-bezier(.2,.7,.3,1),box-shadow .22s cubic-bezier(.2,.7,.3,1),
 border-color .22s}
.tile:hover{transform:translateY(-.2em);box-shadow:0 .75em 1.75em -.5em rgba(20,20,22,.16);
 border-color:var(--border-strong)}
.tile-icon{display:flex;align-items:center;justify-content:center;width:2.35em;height:2.35em;
 border-radius:50%;border:1px solid var(--border-strong);margin-bottom:.85em;color:var(--text-muted)}
.tile-icon svg{width:1.15em;height:1.15em}
.tile h3{margin-bottom:.375em}
.tile p{margin:0;color:var(--text-muted);font-size:.9375em}
.tile-roadmap{background:transparent;border-style:dashed}
.tile-roadmap:hover{border-style:dashed;transform:none;box-shadow:none}
.badge{display:inline-block;font-size:.6875em;font-weight:600;letter-spacing:.02em;text-transform:uppercase;
 color:var(--text-muted);border:1px solid var(--border-strong);border-radius:1em;padding:.15em .625em;
 margin-bottom:.625em}
footer{padding:1em 1.5em 3em;text-align:center}
footer p{margin:.375em 0;font-size:.8125em;color:var(--text-faint)}
footer a.quiet-link{color:inherit;text-decoration:none;border-bottom:1px dotted currentColor}
footer a.quiet-link:hover{color:var(--text-muted)}

@media(prefers-reduced-motion:reduce){*{animation-duration:1ms!important;transition-duration:1ms!important}}
@media(max-width:520px){
 .wizard{padding:28px 16px;justify-content:flex-start}
 .hero{padding:40px 24px 32px}
 .card{padding:26px 22px 22px}
 .hero__title{font-size:22px}
 .title{font-size:19px}
 .engine__badge{font-size:10px;padding:3px 7px}
}
)";

// Scroll-reveal script, served as a real sub-resource (script.js) rather
// than inlined - lets the page CSP stay at script-src 'self' instead of
// 'unsafe-inline', matching the pattern used elsewhere in chrome/browser/ui/webui
// (e.g. arc_power_control_ui.cc, bluetooth_internals_ui.cc). Handles wizard
// screen navigation, toggle/engine state, and mirrors real toggles to their
// browser prefs via chrome.send (see NbrowserFirstRunHandler below).
constexpr char kNbrowserFirstRunScript[] = R"(
(function () {
  'use strict';

  var wizard = document.getElementById('wizard');
  var stage = document.getElementById('stage');
  if (!wizard || !stage) return;

  function allScreens() {
    return Array.prototype.slice.call(stage.querySelectorAll('.screen'));
  }

  function visibleScreens() {
    return allScreens().filter(function (el) {
      return !el.hidden && el.getAttribute('data-state') !== 'off';
    });
  }

  var index = 0;

  function stepDot(n) {
    return document.querySelector('.steps__dot[data-step="' + n + '"]');
  }

  function syncSteps(list) {
    allScreens().forEach(function (screen) {
      var dot = stepDot(screen.getAttribute('data-screen'));
      if (dot) dot.hidden = list.indexOf(screen) === -1;
    });
    list.forEach(function (screen, i) {
      var dot = stepDot(screen.getAttribute('data-screen'));
      if (!dot) return;
      dot.setAttribute('data-state', i === index ? 'active' : (i < index ? 'done' : 'pending'));
    });
  }

  function measure(screen) {
    var prev = screen.style.cssText;
    screen.style.cssText += ';visibility:hidden;opacity:0;position:absolute;transition:none;transform:none;display:block;';
    var h = screen.offsetHeight;
    screen.style.cssText = prev;
    return h;
  }

  function render(animateHeight) {
    var list = visibleScreens();
    if (!list.length) return;
    if (index > list.length - 1) index = list.length - 1;

    list.forEach(function (screen, i) {
      screen.setAttribute('data-state', i === index ? 'active' : (i < index ? 'done' : 'pending'));
    });

    var active = list[index];
    var back = active.querySelector('[data-back]');
    if (back) back.hidden = index === 0;
    var h = measure(active);
    if (!animateHeight) {
      var t = stage.style.transition;
      stage.style.transition = 'none';
      stage.style.height = h + 'px';
      void stage.offsetHeight;
      stage.style.transition = t;
    } else {
      stage.style.height = h + 'px';
    }

    syncSteps(list);
  }

  function go(delta) {
    var list = visibleScreens();
    var next = index + delta;
    if (next < 0 || next > list.length - 1) return;
    /* Screen 6 is much taller/wider than screens 1-5, so crossfading it with
       the outgoing screen (both visible at once, mid-resize) still reads as
       a jolt even with the resize itself snapped instantly. For that one
       boundary, fully fade the current screen out first, then swap and fade
       the recap in - never both on screen resizing at the same time. Plain
       screen-to-screen navigation keeps the normal simultaneous crossfade. */
    var enteringOrLeavingRecap =
        list[index].getAttribute('data-screen') === '6' ||
        list[next].getAttribute('data-screen') === '6';
    if (enteringOrLeavingRecap) {
      list[index].setAttribute('data-state', 'done');
      setTimeout(function () {
        index = next;
        render(false);
      }, 320);
    } else {
      index = next;
      render(true);
    }
  }

  stage.addEventListener('click', function (e) {
    if (!e.target.closest) return;
    if (e.target.closest('[data-next]')) go(1);
    else if (e.target.closest('[data-back]')) go(-1);
  });

  /* Real toggles carry data-pref; the "enhanced translator" toggle doesn't
     (the feature isn't implemented yet), so it stays purely visual. */
  stage.addEventListener('change', function (e) {
    var input = e.target;
    if (input.classList && input.classList.contains('switch__input')) {
      var row = input.closest('.option');
      if (row) row.setAttribute('data-checked', input.checked ? 'true' : 'false');
      var pref = input.getAttribute('data-pref');
      if (pref) chrome.send('nbrowserFirstRunSetPref', [pref, input.checked]);
      updateDependents(input);
    }
    if (input.classList && input.classList.contains('engine__input')) {
      selectEngine(input);
      var engineKey = input.getAttribute('data-engine-key');
      if (engineKey) chrome.send('nbrowserFirstRunSetSearchEngine', [engineKey]);
    }
  });

  function selectEngine(input) {
    var group = document.getElementById('engines');
    if (!group) return;
    Array.prototype.forEach.call(group.querySelectorAll('.engine'), function (row) {
      var radio = row.querySelector('.engine__input');
      var on = radio === input;
      row.setAttribute('data-selected', on ? 'true' : 'false');
      if (radio) radio.checked = on;
    });
  }

  function initSwitches() {
    Array.prototype.forEach.call(stage.querySelectorAll('.switch__input'), function (input) {
      var row = input.closest('.option');
      if (row) row.setAttribute('data-checked', input.checked ? 'true' : 'false');
    });
  }

  function initEngines() {
    var checked = document.querySelector('.engine__input:checked');
    if (checked) selectEngine(checked);
  }

  /* Rows can declare data-depends-on="pref-key" - they're only interactable
     while the toggle with that data-pref is checked, mirroring how Instant
     Translation is nested under Live Caption in chrome://settings/accessibility. */
  function updateDependents(changedInput) {
    var pref = changedInput.getAttribute('data-pref');
    if (!pref) return;
    Array.prototype.forEach.call(
        stage.querySelectorAll('[data-depends-on="' + pref + '"]'), function (row) {
      var input = row.querySelector('.switch__input');
      var enabled = changedInput.checked;
      row.setAttribute('data-disabled', enabled ? 'false' : 'true');
      if (input) {
        input.disabled = !enabled;
        if (!enabled && input.checked) {
          input.checked = false;
          row.setAttribute('data-checked', 'false');
        }
      }
    });
  }

  function initDependents() {
    Array.prototype.forEach.call(stage.querySelectorAll('[data-depends-on]'), function (row) {
      var pref = row.getAttribute('data-depends-on');
      var parent = stage.querySelector('.switch__input[data-pref="' + pref + '"]');
      var enabled = !parent || parent.checked;
      var input = row.querySelector('.switch__input');
      row.setAttribute('data-disabled', enabled ? 'false' : 'true');
      if (input) input.disabled = !enabled;
    });
  }

  document.addEventListener('keydown', function (e) {
    if (e.key === 'ArrowRight') go(1);
    else if (e.key === 'ArrowLeft') go(-1);
  });

  window.addEventListener('resize', function () { render(false); });

  /* Screen 0: vertical "wheel" of hellos in different languages, purely
     decorative - it just signals "this is a language picker" before you
     even read the title. */
  function initLangWheel() {
    var el = document.getElementById('lang-wheel');
    if (!el || matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    var words = ['Hello', 'Привет', 'Hallo', 'Bonjour', 'Hola', '你好'];
    var i = 0;
    var current = el.querySelector('.lang-wheel__word');
    setInterval(function () {
      i = (i + 1) % words.length;
      var next = document.createElement('span');
      next.className = 'lang-wheel__word lang-wheel__word--enter';
      next.textContent = words[i];
      el.appendChild(next);
      var previous = current;
      /* Force a synchronous layout flush so the browser commits the
         --enter starting position (translateY(100%), invisible) before we
         switch away from it below. Without this, a rAF callback sometimes
         gets coalesced with the append into the same style recalc - no
         "before" frame ever gets painted, so the transition has nothing to
         animate from and the word just snaps in instead of sliding. */
      void next.offsetWidth;
      previous.classList.add('lang-wheel__word--leave');
      next.classList.remove('lang-wheel__word--enter');
      setTimeout(function () {
        if (previous.parentNode) previous.parentNode.removeChild(previous);
      }, 550);
      current = next;
    }, 1800);
  }

  /* Screen 0: picking a language different from the one the browser already
     runs in writes it and restarts to apply it - the wizard reopens
     automatically in the new language (see the "pending relaunch" local_state
     flag in chrome_browser_main.cc). Re-picking the language the browser is
     already in (e.g. the OS's language, which the browser already launched
     with) just advances to the next screen - there is nothing to apply, so
     the button reads "Continue" instead of "Restart". */
  function initLangScreen() {
    var btn = document.getElementById('lang-restart-btn');
    var select = document.getElementById('lang-select');
    if (!btn || !select) return;
    var currentLocale = btn.getAttribute('data-current-locale');
    var labelContinue = btn.getAttribute('data-label-continue');
    var labelRestart = btn.getAttribute('data-label-restart');

    function syncLabel() {
      btn.textContent = select.value === currentLocale ? labelContinue : labelRestart;
    }
    syncLabel();
    select.addEventListener('change', syncLabel);

    btn.addEventListener('click', function () {
      if (select.value === currentLocale) {
        go(1);
        return;
      }
      chrome.send('nbrowserFirstRunSetLanguageAndRestart', [select.value]);
    });
  }

  initSwitches();
  initEngines();
  initDependents();
  initLangWheel();
  initLangScreen();
  render(false);

  /* Appearance animation for the first screen, right after the page opens. */
  wizard.classList.add('is-entering');

  /* Public hook for C++: show/hide a screen and jump to it. */
  window.NBrowserFirstRun = {
    setScreenVisible: function (screenNumber, visible) {
      var screen = stage.querySelector('.screen[data-screen="' + screenNumber + '"]');
      if (!screen) return;
      screen.hidden = !visible;
      screen.setAttribute('data-state', visible ? 'pending' : 'off');
      index = 0;
      render(false);
    },
    goTo: function (screenNumber) {
      var list = visibleScreens();
      for (var i = 0; i < list.length; i++) {
        if (list[i].getAttribute('data-screen') === String(screenNumber)) {
          index = i;
          render(true);
          return;
        }
      }
    }
  };
})();
)";

// Bridges the wizard's real toggles/search-engine choice to actual browser
// prefs via chrome.send - so flipping a switch here is identical to flipping
// it in chrome://settings, not a first-run-only imitation. The "enhanced
// translator" toggle on screen 2 is the one exception: that feature isn't
// implemented yet, so it isn't wired to anything here.
class NbrowserFirstRunHandler : public content::WebUIMessageHandler {
 public:
  NbrowserFirstRunHandler() = default;
  NbrowserFirstRunHandler(const NbrowserFirstRunHandler&) = delete;
  NbrowserFirstRunHandler& operator=(const NbrowserFirstRunHandler&) = delete;

  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "nbrowserFirstRunSetPref",
        base::BindRepeating(&NbrowserFirstRunHandler::HandleSetPref,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "nbrowserFirstRunSetSearchEngine",
        base::BindRepeating(&NbrowserFirstRunHandler::HandleSetSearchEngine,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "nbrowserFirstRunSetLanguageAndRestart",
        base::BindRepeating(
            &NbrowserFirstRunHandler::HandleSetLanguageAndRestart,
            base::Unretained(this)));
  }

 private:
  void HandleSetPref(const base::ListValue& args) {
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool())
      return;
    const std::string& pref_key = args[0].GetString();
    bool enabled = args[1].GetBool();
    PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();

    if (pref_key == "live-caption") {
      prefs->SetBoolean(prefs::kLiveCaptionEnabled, enabled);
      // Instant Translation is a sub-setting of Live Caption in the real
      // settings UI - it can't be on while Live Caption itself is off.
      if (!enabled)
        prefs->SetBoolean(prefs::kLiveTranslateEnabled, false);
    } else if (pref_key == "live-translate") {
      if (prefs->GetBoolean(prefs::kLiveCaptionEnabled))
        prefs->SetBoolean(prefs::kLiveTranslateEnabled, enabled);
    } else if (pref_key == "spellcheck") {
      prefs->SetBoolean(spellcheck::prefs::kSpellCheckEnable, enabled);
    } else if (pref_key == "preload-pages") {
      prefetch::SetPreloadPagesState(
          prefs, enabled ? prefetch::PreloadPagesState::kStandardPreloading
                        : prefetch::PreloadPagesState::kNoPreloading);
    } else if (pref_key == "enhanced-translate") {
      prefs->SetBoolean(translate::prefs::kOfferTranslateEnabled, enabled);
      translate::TranslatePrefs translate_prefs(prefs);
      if (enabled) {
        translate_prefs.SetRecentTargetLanguage(kTranslateTargetLanguage);
      } else {
        // "Auto-translate pages" is a sub-setting of this toggle - it can't
        // stay configured while offering translate itself is off.
        translate_prefs.RemoveLanguagePairFromAlwaysTranslateList(
            kTranslateAutoSourceLanguage);
        translate_prefs.UnblockLanguage(kTranslateTargetLanguage);
      }
    } else if (pref_key == "auto-translate-en") {
      if (prefs->GetBoolean(translate::prefs::kOfferTranslateEnabled)) {
        translate::TranslatePrefs translate_prefs(prefs);
        if (enabled) {
          translate_prefs.AddLanguagePairToAlwaysTranslateList(
              kTranslateAutoSourceLanguage, kTranslateTargetLanguage);
          translate_prefs.BlockLanguage(kTranslateTargetLanguage);
        } else {
          translate_prefs.RemoveLanguagePairFromAlwaysTranslateList(
              kTranslateAutoSourceLanguage);
          translate_prefs.UnblockLanguage(kTranslateTargetLanguage);
        }
      }
    }
  }

  void HandleSetSearchEngine(const base::ListValue& args) {
    if (args.empty() || !args[0].is_string())
      return;
    const std::string& key = args[0].GetString();
    TemplateURLService* service =
        TemplateURLServiceFactory::GetForProfile(Profile::FromWebUI(web_ui()));
    if (!service)
      return;
    if (key == "none") {
      service->SetUserSelectedDefaultSearchProvider(nullptr);
      return;
    }
    if (key == "google") {
      EnsureGoogleSearchUrlIsUsable(service);
      if (TemplateURL* google = service->GetTemplateURLForKeyword(u"google.com"))
        service->SetUserSelectedDefaultSearchProvider(google);
      return;
    }
    if (TemplateURL* engine = ResolveSearchEngine(service, key))
      service->SetUserSelectedDefaultSearchProvider(engine);
  }

  void HandleSetLanguageAndRestart(const base::ListValue& args) {
    if (args.empty() || !args[0].is_string())
      return;
    const std::string& locale = args[0].GetString();
    // The UI locale is a Local State (install-wide) pref on Windows, not a
    // per-profile one - matches settings::LanguagesHandler.
    PrefService* local_state = g_browser_process->local_state();
    local_state->SetString(language::prefs::kApplicationLocale, locale);
    local_state->SetBoolean(kLanguageChosenPref, true);
    local_state->SetBoolean(kPendingRelaunchPref, true);

    // Also move the chosen language to the top of Settings -> Languages ->
    // "Sites in your languages" (the accept-languages list), the same way a
    // user who reordered it there by hand would end up. Without this, sites
    // keep being requested in English after switching the whole browser UI
    // to another language, because changing the UI locale alone never
    // touches the separate accept-languages pref. English is already first
    // there by default, so choosing it back is a no-op.
    if (locale != "en-US" && locale != "en") {
      PrefService* profile_prefs = Profile::FromWebUI(web_ui())->GetPrefs();
      language::LanguagePrefs language_prefs(profile_prefs);
      std::vector<std::string> languages;
      language_prefs.GetUserSelectedLanguagesList(&languages);
      std::erase(languages, locale);
      languages.insert(languages.begin(), locale);
      language_prefs.SetUserSelectedLanguagesList(languages);

      // Normally only done once, for the first accept-language a profile is
      // ever created with (see profile_impl.cc) - re-run it now that the
      // accept-languages list has actually changed, so the new language also
      // lands in Settings -> Languages -> spell check, checked, regardless of
      // whether the "spellcheck" toggle on the previous screen ends up on or
      // off (that toggle only controls spellchecking overall, not which
      // dictionaries are considered enabled).
      SpellcheckService::EnableFirstUserLanguageForSpellcheck(profile_prefs);
    }

    chrome::AttemptRestart();
  }
};

class UFRDataSource : public content::URLDataSource {
 public:
  explicit UFRDataSource(Profile* profile) : profile_(profile) {}
  UFRDataSource(const UFRDataSource&) = delete;
  UFRDataSource& operator=(const UFRDataSource&) = delete;
  std::string GetSource() override { return "nbrowser-first-run"; }
  std::string GetMimeType(const GURL& url) override {
    std::string path = content::URLDataSource::URLToRequestPath(url);
    return path == "script.js" ? "text/javascript" : "text/html";
  }
  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    // ScriptSrc: needed to load the script.js sub-resource.
    if (directive == network::mojom::CSPDirectiveName::ScriptSrc)
      return "script-src 'self'";
    return std::string();
  }
  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter& wc_getter,
                        GotDataCallback callback) override {
    std::string path = content::URLDataSource::URLToRequestPath(url);
    if (path == "script.js") {
      std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(
          std::string(kNbrowserFirstRunScript)));
      return;
    }

    auto S = [](int id) { return l10n_util::GetStringUTF8(id); };
    auto Checked = [](bool v) { return v ? " checked" : ""; };

    bool language_chosen =
        g_browser_process->local_state()->GetBoolean(kLanguageChosenPref);
    std::string current_locale = g_browser_process->GetApplicationLocale();

    PrefService* prefs = profile_->GetPrefs();
    bool live_caption_on = prefs->GetBoolean(prefs::kLiveCaptionEnabled);
    bool live_translate_on = prefs->GetBoolean(prefs::kLiveTranslateEnabled);
    bool spellcheck_on = prefs->GetBoolean(spellcheck::prefs::kSpellCheckEnable);
    bool preload_on = prefetch::GetPreloadPagesState(*prefs) !=
                      prefetch::PreloadPagesState::kNoPreloading;
    translate::TranslatePrefs translate_prefs(prefs);
    bool offer_translate_on = translate_prefs.IsOfferTranslateEnabled();
    bool auto_translate_on = translate_prefs.IsLanguagePairOnAlwaysTranslateList(
        kTranslateAutoSourceLanguage, kTranslateTargetLanguage);

    std::string current_engine_key = "none";
    if (TemplateURLService* service =
            TemplateURLServiceFactory::GetForProfile(profile_)) {
      // Chromium hardcodes Google as the prepopulated fallback default, so a
      // fresh profile already has the broken macro'd entry active before the
      // user ever interacts with this page - fix it here unconditionally,
      // not just in response to an explicit radio click.
      EnsureGoogleSearchUrlIsUsable(service);
      if (const TemplateURL* current = service->GetDefaultSearchProvider()) {
        std::string key = EngineKeyForPrepopulateId(current->prepopulate_id());
        if (!key.empty())
          current_engine_key = key;
      }
    }
    auto EngineChecked = [&](const char* key) {
      return current_engine_key == key ? " checked" : "";
    };

    // Screen 2 (translator/captions) is only relevant to Russian-language
    // users, so it's dropped entirely (not just visually hidden) elsewhere.
    bool show_translate_screen =
        l10n_util::GetLanguage(g_browser_process->GetApplicationLocale()) == "ru";

    std::string language_options;
    for (const LangOption& opt : kLangOptions) {
      base::StrAppend(&language_options,
                      {"<option value=\"", opt.code, "\"",
                       current_locale == opt.code ? " selected" : "", ">",
                       opt.native_name, "</option>"});
    }

    std::string source = base::StrCat({
R"(<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>)", S(IDS_NBROWSER_FIRST_RUN_TITLE), R"(</title>
<meta name="color-scheme" content="light dark">
<style>
)", kNbrowserFirstRunCss, R"(
</style>
<base target="_blank">
<main class="wizard" id="wizard">
 <div class="stage" id="stage">

  <section class="screen screen--welcome" data-screen="0")", language_chosen ? " hidden" : "", R"( data-state=")", language_chosen ? "off" : "active", R"(">
   <div class="hero">
    <div class="hero__mark" aria-hidden="true">
     <svg viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M128 6C195.379 6 250 60.6213 250 128C250 195.379 195.379 250 128 250C60.6213 250 6 195.379 6 128C6 60.6213 60.6213 6 128 6Z" fill="white" stroke="white" stroke-width="12"/>
      <path d="M175.893 148L117.292 249.5" stroke="black" stroke-width="24"/>
      <ellipse cx="126.942" cy="128" rx="52.8926" ry="52.459" stroke="black" stroke-width="24"/>
      <path d="M74.4451 131.148L49.0844 36.5" stroke="black" stroke-width="24"/>
      <path d="M128 75.541H237.5" stroke="black" stroke-width="24"/>
      <circle cx="128" cy="128" r="122" stroke="white" stroke-width="12"/>
     </svg>
    </div>
    <h1 class="hero__title lang-title" data-reveal>
     <span class="lang-wheel" id="lang-wheel" aria-hidden="true"><span class="lang-wheel__word">Hello</span></span>
     <span>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_LANG_TITLE), R"(</span>
    </h1>
    <div class="lang-picker" data-reveal>
     <select class="lang-select" id="lang-select">
)", language_options, R"(
     </select>
    </div>
    <div class="actions actions--hero" data-reveal>
     <button class="btn btn--invert" type="button" id="lang-restart-btn"
         data-current-locale=")", current_locale, R"("
         data-label-continue=")", S(IDS_NBROWSER_FIRST_RUN_WIZARD_CONTINUE), R"("
         data-label-restart=")", S(IDS_NBROWSER_FIRST_RUN_WIZARD_LANG_RESTART), R"(">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_LANG_RESTART), R"(</button>
    </div>
   </div>
  </section>

  <section class="screen screen--welcome" data-screen="1" data-state=")", language_chosen ? "active" : "pending", R"(">
   <div class="hero">
    <div class="hero__mark" id="brand-mark" aria-hidden="true">
     <svg viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M128 6C195.379 6 250 60.6213 250 128C250 195.379 195.379 250 128 250C60.6213 250 6 195.379 6 128C6 60.6213 60.6213 6 128 6Z" fill="white" stroke="white" stroke-width="12"/>
      <path d="M175.893 148L117.292 249.5" stroke="black" stroke-width="24"/>
      <ellipse cx="126.942" cy="128" rx="52.8926" ry="52.459" stroke="black" stroke-width="24"/>
      <path d="M74.4451 131.148L49.0844 36.5" stroke="black" stroke-width="24"/>
      <path d="M128 75.541H237.5" stroke="black" stroke-width="24"/>
      <circle cx="128" cy="128" r="122" stroke="white" stroke-width="12"/>
     </svg>
    </div>
    <h1 class="hero__title" data-reveal>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_WELCOME_TITLE), R"(</h1>
    <p class="hero__sub" data-reveal>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_WELCOME_SUBTITLE), R"(</p>
    <div class="actions actions--hero" data-reveal>
     <button class="btn btn--invert" type="button" data-next>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_CONTINUE), R"(</button>
    </div>
   </div>
  </section>

  <section class="screen" data-screen="2")", show_translate_screen ? "" : " hidden", R"(>
   <div class="card">
    <h2 class="title">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_TITLE), R"(</h2>
    <p class="text">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_INTRO), R"(</p>
    <ul class="list">
     <li>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_BULLET_IMPROVED), R"(</li>
     <li>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_BULLET_CONTEXT_MENU), R"(</li>
     <li>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_BULLET_APPLY), R"(</li>
    </ul>

    <div class="notice" role="note">
     <svg class="notice__icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M10.3 3.9 1.9 18.4a2 2 0 0 0 1.7 3h16.8a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0Z"></path>
      <path d="M12 9v4.5"></path>
      <path d="M12 17.2h.01"></path>
     </svg>
     <p class="notice__text">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_NOTICE), R"(</p>
    </div>

    <div class="options">
     <label class="option">
      <span class="option__label">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_ENHANCED_LABEL), R"( <span class="option__hint">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_ENHANCED_HINT), R"(</span></span>
      <span class="switch">
       <input class="switch__input" type="checkbox" data-pref="enhanced-translate")", Checked(offer_translate_on), R"(>
       <span class="switch__track" aria-hidden="true"><span class="switch__knob"></span></span>
      </span>
     </label>
     <label class="option" data-depends-on="enhanced-translate">
      <span class="option__label">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_AUTO_LABEL), R"(</span>
      <span class="switch">
       <input class="switch__input" type="checkbox" data-pref="auto-translate-en")", Checked(auto_translate_on), offer_translate_on ? "" : " disabled", R"(>
       <span class="switch__track" aria-hidden="true"><span class="switch__knob"></span></span>
      </span>
     </label>
     <label class="option">
      <span class="option__label">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_CAPTIONS_LABEL), R"(</span>
      <span class="switch">
       <input class="switch__input" type="checkbox" data-pref="live-caption")", Checked(live_caption_on), R"(>
       <span class="switch__track" aria-hidden="true"><span class="switch__knob"></span></span>
      </span>
     </label>
     <label class="option" data-depends-on="live-caption">
      <span class="option__label">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_INSTANT_LABEL), R"( <span class="option__hint">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_TRANSLATE_INSTANT_HINT), R"(</span></span>
      <span class="switch">
       <input class="switch__input" type="checkbox" data-pref="live-translate")", Checked(live_translate_on), live_caption_on ? "" : " disabled", R"(>
       <span class="switch__track" aria-hidden="true"><span class="switch__knob"></span></span>
      </span>
     </label>
    </div>

    <div class="actions">
     <button class="btn btn--ghost" type="button" data-back>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_BACK), R"(</button>
     <button class="btn" type="button" data-next>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_CONTINUE), R"(</button>
    </div>
   </div>
  </section>

  <section class="screen" data-screen="3">
   <div class="card">
    <h2 class="title">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_SPELLCHECK_TITLE), R"(</h2>
    <p class="text">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_SPELLCHECK_TEXT), R"(</p>

    <div class="options">
     <label class="option">
      <span class="option__label">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_SPELLCHECK_LABEL), R"(</span>
      <span class="switch">
       <input class="switch__input" type="checkbox" data-pref="spellcheck")", Checked(spellcheck_on), R"(>
       <span class="switch__track" aria-hidden="true"><span class="switch__knob"></span></span>
      </span>
     </label>
    </div>

    <div class="actions">
     <button class="btn btn--ghost" type="button" data-back>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_BACK), R"(</button>
     <button class="btn" type="button" data-next>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_CONTINUE), R"(</button>
    </div>
   </div>
  </section>

  <section class="screen" data-screen="4">
   <div class="card">
    <h2 class="title">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_PRELOAD_TITLE), R"(</h2>
    <p class="text">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_PRELOAD_TEXT), R"(</p>

    <div class="notice" role="note">
     <svg class="notice__icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M10.3 3.9 1.9 18.4a2 2 0 0 0 1.7 3h16.8a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0Z"></path>
      <path d="M12 9v4.5"></path>
      <path d="M12 17.2h.01"></path>
     </svg>
     <p class="notice__text">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_PRELOAD_NOTICE), R"(</p>
    </div>

    <div class="options">
     <label class="option">
      <span class="option__label">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_PRELOAD_LABEL), R"(</span>
      <span class="switch">
       <input class="switch__input" type="checkbox" data-pref="preload-pages")", Checked(preload_on), R"(>
       <span class="switch__track" aria-hidden="true"><span class="switch__knob"></span></span>
      </span>
     </label>
    </div>

    <div class="actions">
     <button class="btn btn--ghost" type="button" data-back>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_BACK), R"(</button>
     <button class="btn" type="button" data-next>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_CONTINUE), R"(</button>
    </div>
   </div>
  </section>

  <section class="screen" data-screen="5">
   <div class="card">
    <h2 class="title">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_TITLE), R"(</h2>
    <p class="text">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_TEXT), R"(</p>

    <div class="engines" id="engines" role="radiogroup">
     <label class="engine" data-engine="google">
      <input class="engine__input" type="radio" name="search-engine" data-engine-key="google")", EngineChecked("google"), R"(>
      <span class="engine__radio" aria-hidden="true"></span>
      <span class="engine__body">
       <span class="engine__name">Google</span>
       <span class="engine__host">google.com</span>
      </span>
      <span class="engine__badge">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_DEFAULT_BADGE), R"(</span>
     </label>
     <label class="engine" data-engine="brave">
      <input class="engine__input" type="radio" name="search-engine" data-engine-key="brave")", EngineChecked("brave"), R"(>
      <span class="engine__radio" aria-hidden="true"></span>
      <span class="engine__body">
       <span class="engine__name">Brave</span>
       <span class="engine__host">search.brave.com</span>
      </span>
      <span class="engine__badge">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_DEFAULT_BADGE), R"(</span>
     </label>
     <label class="engine" data-engine="duckduckgo">
      <input class="engine__input" type="radio" name="search-engine" data-engine-key="duckduckgo")", EngineChecked("duckduckgo"), R"(>
      <span class="engine__radio" aria-hidden="true"></span>
      <span class="engine__body">
       <span class="engine__name">DuckDuckGo</span>
       <span class="engine__host">duckduckgo.com</span>
      </span>
      <span class="engine__badge">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_DEFAULT_BADGE), R"(</span>
     </label>
     <label class="engine" data-engine="yandex">
      <input class="engine__input" type="radio" name="search-engine" data-engine-key="yandex")", EngineChecked("yandex"), R"(>
      <span class="engine__radio" aria-hidden="true"></span>
      <span class="engine__body">
       <span class="engine__name">Yandex</span>
       <span class="engine__host">yandex.ru</span>
      </span>
      <span class="engine__badge">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_DEFAULT_BADGE), R"(</span>
     </label>
     <label class="engine" data-engine="none">
      <input class="engine__input" type="radio" name="search-engine" data-engine-key="none")", EngineChecked("none"), R"(>
      <span class="engine__radio" aria-hidden="true"></span>
      <span class="engine__body">
       <span class="engine__name">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_NONE_NAME), R"(</span>
       <span class="engine__host">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_NONE_HINT), R"(</span>
      </span>
      <span class="engine__badge">)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_ENGINE_DEFAULT_BADGE), R"(</span>
     </label>
    </div>

    <div class="actions">
     <button class="btn btn--ghost" type="button" data-back>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_BACK), R"(</button>
     <button class="btn" type="button" data-next>)", S(IDS_NBROWSER_FIRST_RUN_WIZARD_CONTINUE), R"(</button>
    </div>
   </div>
  </section>

  <section class="screen" data-screen="6">
   <div class="legacy-page">
    <section class="hero">
     <svg class="hero-mark" viewBox="0 0 256 256" fill="none" aria-hidden="true">
      <circle cx="128" cy="128" r="122" stroke-width="12"/>
      <path d="M175.893 148L130.9 226" stroke-width="20"/>
      <path d="M74.4451 131.148L55.8 61.7" stroke-width="20"/>
      <path d="M128 75.541H210.8" stroke-width="20"/>
      <ellipse cx="126.942" cy="128" rx="52.8926" ry="52.459" stroke-width="20"/>
     </svg>
     <svg class="hero-logo" width="256" height="256" viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M128 6C195.379 6 250 60.6213 250 128C250 195.379 195.379 250 128 250C60.6213 250 6 195.379 6 128C6 60.6213 60.6213 6 128 6Z" fill="white" stroke="white" stroke-width="12"/>
      <path d="M175.893 148L117.292 249.5" stroke="black" stroke-width="24"/>
      <ellipse cx="126.942" cy="128" rx="52.8926" ry="52.459" stroke="black" stroke-width="24"/>
      <path d="M74.4451 131.148L49.0844 36.5" stroke="black" stroke-width="24"/>
      <path d="M128 75.541H237.5" stroke="black" stroke-width="24"/>
      <circle cx="128" cy="128" r="122" stroke="white" stroke-width="12"/>
     </svg>
     <h1>)", S(IDS_NBROWSER_FIRST_RUN_HERO_HEADING), R"(</h1>
     <p class="lead">)", S(IDS_NBROWSER_FIRST_RUN_HERO_SUBTEXT), R"(</p>
    </section>
    <section class="block">
     <h2><span class="section-index">01</span>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_HEADING), R"(</h2>
     <p class="lead">)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_INTRO), R"(</p>
     <div class="cards">
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="6" cy="15" r="2"/><circle cx="6" cy="5" r="2"/><line x1="7.6" y1="6.4" x2="17" y2="16"/><line x1="7.6" y1="13.6" x2="17" y2="4"/></svg></div>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_CUT_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_CUT_BODY), R"(</p>
      </div>
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="7.5" cy="10" r="6"/><circle cx="12.5" cy="10" r="6"/></svg></div>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_TERRITORIES_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_TERRITORIES_BODY), R"(</p>
      </div>
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><path d="M6.5 10.3l2.2 2.2 4.3-4.6"/></svg></div>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_PRAGMATIC_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_PRAGMATIC_BODY), R"(</p>
      </div>
     </div>
    </section>
    <section class="block">
     <h2><span class="section-index">02</span>)", S(IDS_NBROWSER_FIRST_RUN_FEATURES_HEADING), R"(</h2>
     <div class="cards cards-compact">
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><line x1="5" y1="5" x2="15" y2="15"/></svg></div>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CUT_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CUT_BODY), R"(</p>
      </div>
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M15.5 6.2A6.3 6.3 0 1 0 16.6 12"/><path d="M15.6 2.8v4h-4"/></svg></div>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_UPDATES_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_UPDATES_BODY), R"(</p>
      </div>
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="4.5" y="9" width="11" height="7.5" rx="1.4"/><path d="M7 9V6.3a3 3 0 0 1 6 0"/></svg></div>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_OPEN_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_OPEN_BODY), R"(</p>
      </div>
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="2.5" y="6.5" width="15" height="7" rx="3.5"/><circle cx="13.5" cy="10" r="2.6"/></svg></div>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CONTROLS_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CONTROLS_BODY), R"(</p>
      </div>
      <div class="tile tile-roadmap">
       <span class="badge">)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_ROADMAP_BADGE), R"(</span>
       <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_ROADMAP_TITLE), R"(</h3>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_ROADMAP_BODY), R"(</p>
      </div>
     </div>
    </section>
    <section class="block">
     <h2><span class="section-index">03</span>)", S(IDS_NBROWSER_FIRST_RUN_EXTENSIONS_HEADING), R"(</h2>
     <div class="cards">
      <div class="tile">
       <div class="tile-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="3.5" y="3.5" width="13" height="13" rx="2"/><circle cx="13" cy="3.5" r="2"/></svg></div>
       <p>)", S(IDS_NBROWSER_FIRST_RUN_EXTENSIONS_BODY), R"(</p>
      </div>
     </div>
    </section>
   </div>
   <footer>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_FOOTER_REVISIT), R"(</p>
    <p><a class="quiet-link" href="https://web.tribute.tg/d/FIQ">)", S(IDS_NBROWSER_FIRST_RUN_FOOTER_DONATE), R"(</a></p>
   </footer>
  </section>

 </div>

 <nav class="steps" id="steps" aria-label="wizard steps">
  <span class="steps__dot" data-step="1"></span>
  <span class="steps__dot" data-step="2"></span>
  <span class="steps__dot" data-step="3"></span>
  <span class="steps__dot" data-step="4"></span>
  <span class="steps__dot" data-step="5"></span>
  <span class="steps__dot" data-step="6"></span>
 </nav>
</main>
<script src="script.js"></script>
)"});
    std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(std::move(source)));
  }

 private:
  Profile* profile_;
};

}  // namespace

class UngoogledFirstRun;
class UngoogledFirstRunUIConfig : public content::DefaultWebUIConfig<UngoogledFirstRun> {
  public:
   UngoogledFirstRunUIConfig() : DefaultWebUIConfig("chrome", "nbrowser-first-run") {}
};

class UngoogledFirstRun : public content::WebUIController {
 public:
  UngoogledFirstRun(content::WebUI* web_ui) : content::WebUIController(web_ui) {
    Profile* profile = Profile::FromWebUI(web_ui);
    content::URLDataSource::Add(profile, std::make_unique<UFRDataSource>(profile));
    web_ui->AddMessageHandler(std::make_unique<NbrowserFirstRunHandler>());
  }
  UngoogledFirstRun(const UngoogledFirstRun&) = delete;
  UngoogledFirstRun& operator=(const UngoogledFirstRun&) = delete;
};

#endif  // CHROME_BROWSER_UI_WEBUI_UNGOOGLED_FIRST_RUN_H_
