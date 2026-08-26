#ifndef CHROME_BROWSER_UI_WEBUI_UNGOOGLED_FIRST_RUN_H_
#define CHROME_BROWSER_UI_WEBUI_UNGOOGLED_FIRST_RUN_H_

#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/nbrowser_first_run/grit/nbrowser_first_run_strings.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/l10n/l10n_util.h"

// CSS for the whole page. Uses the standard chrome://resources text styling
// baseline, then the page's own rules; no brand accent color is used (the
// NBrowser identity is strictly monochrome), so light/dark theming, the
// fixed-dark hero band, the ring/spokes watermark (echoing the product mark
// itself) and per-card icons do the visual work instead of color.
constexpr char kNbrowserFirstRunCss[] = R"(
 @import url(chrome://resources/css/text_defaults_md.css);
 :root{color-scheme:light dark}
 *{box-sizing:border-box}
 html{background:#fff;color:#202124;line-height:1.5}
 body{margin:0}
 a{color:#202124;text-decoration-color:rgba(32,33,36,.4)}
 a:hover{text-decoration-color:currentColor}
 h1,h2,h3{font-weight:500;margin:0 0 .5em}
 h1{font-size:2em}
 h2{font-size:1.5em;display:flex;align-items:center;gap:.6em}
 h3{font-size:1.125em}
 p{margin:0 0 1em}
 main{max-width:76em;margin:0 auto;padding:0 1.5em 1em}
 section.hero{position:relative;overflow:hidden;text-align:center;padding:3.5em 1.5em;margin:0 -1.5em 2em;
   background:#101114;color:#f1f2f3;border-radius:0 0 1em 1em}
 .hero-mark{position:absolute;top:50%;right:-6em;width:26em;height:26em;margin-top:-13em;
   opacity:.07;pointer-events:none}
 .hero-mark,.hero-mark *{stroke:#fff}
 .hero-logo{position:relative;display:block;margin:0 auto 1.5em;height:3.75em;width:3.75em}
 section.hero .lead{position:relative;color:#b7bac0}
 .lead{font-size:1.0625em;color:#5f6368;max-width:34em;margin:0 auto}
 section.block{padding:2.5em 0;border-top:1px solid #eee}
 .section-index{flex:none;display:inline-flex;align-items:center;justify-content:center;
   width:1.9em;height:1.9em;border-radius:50%;border:1px solid #dcdcdc;color:#9aa0a6;
   font-size:.5em;font-weight:600;letter-spacing:.02em}
 .cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(15em,1fr));gap:1.25em;margin-top:1.5em}
 .card{background:#fafafa;border:1px solid #eee;border-radius:.75em;padding:1.25em 1.375em;
   transition:transform .22s cubic-bezier(.2,.7,.3,1),box-shadow .22s cubic-bezier(.2,.7,.3,1),border-color .22s}
 .card:hover{transform:translateY(-.2em);box-shadow:0 .75em 1.75em -.5em rgba(20,20,22,.16);border-color:#dadce0}
 .card-icon{display:flex;align-items:center;justify-content:center;width:2.35em;height:2.35em;
   border-radius:50%;border:1px solid #dcdcdc;margin-bottom:.85em;color:#5f6368}
 .card-icon svg{width:1.15em;height:1.15em}
 .card h3{margin-bottom:.375em}
 .card p{margin:0;color:#5f6368;font-size:.9375em}
 .card-roadmap{background:transparent;border-style:dashed}
 .card-roadmap:hover{border-style:dashed;transform:none;box-shadow:none}
 .badge{display:inline-block;font-size:.6875em;font-weight:600;letter-spacing:.02em;text-transform:uppercase;color:#5f6368;border:1px solid #dcdcdc;border-radius:1em;padding:.15em .625em;margin-bottom:.625em}
 footer{max-width:76em;margin:0 auto;padding:1em 1.5em 3em;text-align:center}
 footer p{margin:.375em 0;font-size:.8125em;color:#9aa0a6}
 footer a.quiet-link{color:inherit;text-decoration:none;border-bottom:1px dotted currentColor}
 footer a.quiet-link:hover{color:#5f6368}
 @media(prefers-color-scheme:dark){
  html{background:#202124;color:#e8eaed}
  a{color:#e8eaed;text-decoration-color:rgba(232,234,237,.4)}
  .lead{color:#9aa0a6}
  section.block{border-top-color:#3f4042}
  .section-index{border-color:#5f6368;color:#80868b}
  .card{background:#292a2d;border-color:#3f4042}
  .card:hover{border-color:#5f6368;box-shadow:0 .75em 1.75em -.5em rgba(0,0,0,.4)}
  .card-icon{border-color:#5f6368;color:#9aa0a6}
  .card p{color:#9aa0a6}
  .badge{color:#9aa0a6;border-color:#5f6368}
  footer p{color:#80868b}
 }
 /* Motion is opt-in: elements render fully visible by default (no-JS and
    reduced-motion users alike). Only once script confirms motion is allowed
    does body.js-reveal switch .reveal elements to an animate-on-scroll
    state, driven by the IntersectionObserver below. */
 body.js-reveal .reveal{opacity:0;transform:translateY(16px);
   transition:opacity .55s cubic-bezier(.2,.7,.3,1),transform .55s cubic-bezier(.2,.7,.3,1);
   transition-delay:var(--d,0s)}
 body.js-reveal .reveal.is-visible{opacity:1;transform:translateY(0)}
 body.js-reveal .hero-logo{opacity:0;transform:scale(.85);
   transition:opacity .5s cubic-bezier(.2,.7,.3,1) .1s,transform .5s cubic-bezier(.2,.7,.3,1) .1s}
 body.js-reveal .hero-logo.is-visible{opacity:1;transform:scale(1)}
)";

// Scroll-reveal script, served as a real sub-resource (script.js) rather
// than inlined - lets the page CSP stay at script-src 'self' instead of
// 'unsafe-inline', matching the pattern used elsewhere in chrome/browser/ui/webui
// (e.g. arc_power_control_ui.cc, bluetooth_internals_ui.cc).
constexpr char kNbrowserFirstRunScript[] = R"(
(function(){
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  document.body.classList.add('js-reveal');
  var targets = document.querySelectorAll('.reveal, .hero-logo');
  if (!('IntersectionObserver' in window)) {
    targets.forEach(function(t){ t.classList.add('is-visible'); });
    return;
  }
  var io = new IntersectionObserver(function(entries){
    entries.forEach(function(entry){
      if (entry.isIntersecting) {
        entry.target.classList.add('is-visible');
        io.unobserve(entry.target);
      }
    });
  }, {threshold: 0.15, rootMargin: '0px 0px -8% 0px'});
  targets.forEach(function(t){ io.observe(t); });
})();
)";

class UFRDataSource : public content::URLDataSource {
 public:
  UFRDataSource() {}
  UFRDataSource(const UFRDataSource&) = delete;
  UFRDataSource& operator=(const UFRDataSource&) = delete;
  std::string GetSource() override { return "nbrowser-first-run"; }
  std::string GetMimeType(const GURL& url) override {
    std::string path = content::URLDataSource::URLToRequestPath(url);
    return path == "script.js" ? "text/javascript" : "text/html";
  }
  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    // ScriptSrc: needed to load the scroll-reveal script.js sub-resource.
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
    std::string source = base::StrCat({
R"(<!doctype html>
<meta charset="utf-8">
<title>)", S(IDS_NBROWSER_FIRST_RUN_TITLE), R"(</title>
<meta name="color-scheme" content="light dark">
<style>
)", kNbrowserFirstRunCss, R"(
</style>
<base target="_blank">
<main>
 <section class="hero reveal">
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
 <section class="block reveal">
  <h2><span class="section-index">01</span>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_HEADING), R"(</h2>
  <p class="lead">)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_INTRO), R"(</p>
  <div class="cards">
   <div class="card reveal" style="--d:.04s">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="6" cy="15" r="2"/><circle cx="6" cy="5" r="2"/><line x1="7.6" y1="6.4" x2="17" y2="16"/><line x1="7.6" y1="13.6" x2="17" y2="4"/></svg></div>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_CUT_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_CUT_BODY), R"(</p>
   </div>
   <div class="card reveal" style="--d:.1s">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="7.5" cy="10" r="6"/><circle cx="12.5" cy="10" r="6"/></svg></div>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_TERRITORIES_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_TERRITORIES_BODY), R"(</p>
   </div>
   <div class="card reveal" style="--d:.16s">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><path d="M6.5 10.3l2.2 2.2 4.3-4.6"/></svg></div>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_PRAGMATIC_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_PHILOSOPHY_PRAGMATIC_BODY), R"(</p>
   </div>
  </div>
 </section>
 <section class="block reveal">
  <h2><span class="section-index">02</span>)", S(IDS_NBROWSER_FIRST_RUN_FEATURES_HEADING), R"(</h2>
  <div class="cards cards-compact">
   <div class="card reveal" style="--d:.04s">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><line x1="5" y1="5" x2="15" y2="15"/></svg></div>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CUT_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CUT_BODY), R"(</p>
   </div>
   <div class="card reveal" style="--d:.08s">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M15.5 6.2A6.3 6.3 0 1 0 16.6 12"/><path d="M15.6 2.8v4h-4"/></svg></div>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_UPDATES_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_UPDATES_BODY), R"(</p>
   </div>
   <div class="card reveal" style="--d:.12s">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="4.5" y="9" width="11" height="7.5" rx="1.4"/><path d="M7 9V6.3a3 3 0 0 1 6 0"/></svg></div>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_OPEN_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_OPEN_BODY), R"(</p>
   </div>
   <div class="card reveal" style="--d:.16s">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="2.5" y="6.5" width="15" height="7" rx="3.5"/><circle cx="13.5" cy="10" r="2.6"/></svg></div>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CONTROLS_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_CONTROLS_BODY), R"(</p>
   </div>
   <div class="card card-roadmap reveal" style="--d:.2s">
    <span class="badge">)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_ROADMAP_BADGE), R"(</span>
    <h3>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_ROADMAP_TITLE), R"(</h3>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_FEATURE_ROADMAP_BODY), R"(</p>
   </div>
  </div>
 </section>
 <section class="block reveal">
  <h2><span class="section-index">03</span>)", S(IDS_NBROWSER_FIRST_RUN_EXTENSIONS_HEADING), R"(</h2>
  <div class="cards">
   <div class="card reveal">
    <div class="card-icon"><svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="3.5" y="3.5" width="13" height="13" rx="2"/><circle cx="13" cy="3.5" r="2"/></svg></div>
    <p>)", S(IDS_NBROWSER_FIRST_RUN_EXTENSIONS_BODY), R"(</p>
   </div>
  </div>
 </section>
</main>
<footer class="reveal">
 <p>)", S(IDS_NBROWSER_FIRST_RUN_FOOTER_REVISIT), R"(</p>
 <p><a class="quiet-link" href="https://web.tribute.tg/d/FIQ">)", S(IDS_NBROWSER_FIRST_RUN_FOOTER_DONATE), R"(</a></p>
</footer>
<script src="script.js"></script>
)"});
    std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(std::move(source)));
  }
};

class UngoogledFirstRun;
class UngoogledFirstRunUIConfig : public content::DefaultWebUIConfig<UngoogledFirstRun> {
  public:
   UngoogledFirstRunUIConfig() : DefaultWebUIConfig("chrome", "nbrowser-first-run") {}
};

class UngoogledFirstRun : public content::WebUIController {
 public:
  UngoogledFirstRun(content::WebUI* web_ui) : content::WebUIController(web_ui) {
    content::URLDataSource::Add(Profile::FromWebUI(web_ui), std::make_unique<UFRDataSource>());
  }
  UngoogledFirstRun(const UngoogledFirstRun&) = delete;
  UngoogledFirstRun& operator=(const UngoogledFirstRun&) = delete;
};

#endif  // CHROME_BROWSER_UI_WEBUI_UNGOOGLED_FIRST_RUN_H_
