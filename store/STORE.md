# Putting Mud Crawler on the Microsoft Store

Why the Store: when Microsoft publishes the app, **Microsoft signs the package for you**. Store installs don't trigger
SmartScreen or Smart App Control. You never buy or manage a certificate.

This is a brand-new Store product. Nothing from the v1 listing is reused.

## 1. Partner Center
1. Sign in at https://partner.microsoft.com/dashboard (your existing developer account works; there is only a one-time
   registration fee if you don't have one).
2. **Apps and games > New product > MSIX or PWA app**. Reserve a name.
   - The product name reserved for this app is **Mud Crawler**. The manifest's display name must match it exactly.
3. Open the new product: **Product management > Product identity**. Copy three values:
   - Package/Identity/Name
   - Package/Identity/Publisher  (looks like `CN=0123ABCD-...`)
   - Package/Properties/PublisherDisplayName

## 2. Build the package
On Linux (this project):
1. `cp store/identity.env.example store/identity.env` and paste the three values (plus the product name).
2. `make win && bash store/stage.sh`  -> creates `dist/MudCrawler-store-stage.zip`.

On your Windows PC:
1. Install the free **Windows SDK** (tick *Windows SDK Signing Tools for Desktop Apps*) so `makeappx.exe` exists.
2. Unzip `MudCrawler-store-stage.zip`, open PowerShell in that folder and run `.\pack.ps1`.
3. You get `MudCrawler-<date>-<time>.msix`. It is unsigned on purpose: the Store signs it.
4. Optional but recommended: run the **Windows App Certification Kit** (installed with the SDK) on the .msix first. It
   catches most reasons for a failed submission.

## 3. Submit
**Start your submission** in Partner Center, then fill in:
- **Pricing and availability:** free, choose markets.
- **Properties:** category (Games or Social/Utilities) and the **privacy policy URL**. Use:
  `https://steriledevelopment.github.io/MudCrawler/privacy.html`
  (a GitHub Pages page served from `docs/privacy.html` in the public repo). The same policy is inside the app under
  Settings > Privacy. To change the wording, edit `store/gen-privacy.py`, run it, then commit and push: it rewrites the
  in-app text, `store/PRIVACY.md` and the web page together.
- **Age rating:** answer the questionnaire (a MUD client can show user-generated text; say so honestly).
- **Packages:** upload the .msix. When asked about the `runFullTrust` restricted capability, a plain justification is:
  "Classic desktop (Win32) application. It needs full trust to open network sockets to MUD servers and to read and
  write its own settings, maps and logs."
- **Store listing:** description, at least one screenshot (1366x768 or larger), the logo, support email
  (steriledevelopment@gmail.com), optional website.
- **Notes for certification:** give testers a MUD to connect to (host, port, a test character if a login is needed).
  Without that they can't see the app work and may fail it.

Certification usually takes a few business days. If it fails, the report says why; fix and resubmit.

## Things to decide / watch for
- **Donate button.** Store policies limit how apps ask for money. A PayPal donation link may be questioned. If a
  review flags it, ship the Store version without the button (it is one section of the About page).
- **TLS.** Not implemented yet (plain telnet). Fine to submit, but say so in the description if you list features.
- **Data folder in a Store install.** Packaged apps get a private copy of AppData, so *Open data folder* in
  About may show a different location than where the files really are. Easy to adjust later.
- **Updates.** Each update needs a higher version. The manifest is `2.0.0.0`; the Store reserves the last number, so
  bump the third (`2.0.1.0`, `2.1.0.0`, ...).
- The unsigned test builds I send you (`.bin`) are separate and keep triggering SmartScreen; that is expected.
