#include "app/android/AndroidAudioApp.h"

// Pro-unlock billing (split TU per no-god-files rule). Message thread only.
// Cache semantics: seed from disk at startup, then let the store overwrite
// in BOTH directions — cached Pro without store confirmation is honored
// (offline flight), but a store answer of "not owned" clears it.
//
// Adapted from the task brief against the REAL JUCE 9.0.1
// juce_product_unlocking API (build/_deps/juce-src/modules/
// juce_product_unlocking/in_app_purchases/juce_InAppPurchases.h), which
// differs from the brief's assumed shape:
//   - There is no Listener::purchasesListReceived(); the restore-list
//     callback is Listener::purchasesListRestored(const Array<PurchaseInfo>&,
//     bool success, const String& statusDescription).
//   - PurchaseInfo is nested as InAppPurchases::Listener::PurchaseInfo (a
//     Purchase + Array<Download*>), not InAppPurchases::PurchaseInfo.
//   - Purchase::productIds is a StringArray (a purchase can cover more than
//     one product/subscription bundle), not a single productId string.

static constexpr const char* kProProductId = "pro_unlock";

juce::File AndroidAudioApp::entitlementCacheFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/entitlement.json");
}

void AndroidAudioApp::persistEntitlement(bool pro) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("pro", pro);
    const auto f = entitlementCacheFile();
    const bool dirOk = f.getParentDirectory().createDirectory().wasOk();
    const bool wroteOk = dirOk && f.replaceWithText(juce::JSON::toString(juce::var(obj)));
    // Fail safe, not stale: if the write didn't land, delete rather than
    // leave a possibly-stale cache behind — an absent cache seeds not-pro
    // (corrected upward by the next successful store round-trip), while a
    // stale pro=true cache would wrongly re-grant Pro after a refund.
    if (!wroteOk) f.deleteFile();
}

struct AndroidAudioApp::BillingListener : juce::InAppPurchases::Listener {
    explicit BillingListener(AndroidAudioApp& o) : owner(o) {}
    AndroidAudioApp& owner;

    // Product (price/title/description) is a sibling of Listener nested
    // directly under InAppPurchases, not under Listener itself, so unlike
    // PurchaseInfo it does NOT resolve unqualified via base-class lookup —
    // needs the explicit juce::InAppPurchases:: qualification here.
    void productsInfoReturned(const juce::Array<juce::InAppPurchases::Product>& products) override {
        for (const auto& p : products)
            if (p.identifier == kProProductId && owner.shell_ != nullptr)
                owner.shell_->setProPriceText(p.price);
    }

    void purchasesListRestored(const juce::Array<PurchaseInfo>& purchases, bool success,
                               const juce::String& /*statusDescription*/) override {
        if (!success) return;   // keep cached state on query failure
        bool owned = false;
        for (const auto& p : purchases)
            if (p.purchase.productIds.contains(kProProductId)) owned = true;
        owner.entitlements_.setPro(owned);
        owner.persistEntitlement(owned);
        if (owner.shell_ != nullptr) owner.shell_->refreshProState();
    }

    void productPurchaseFinished(const PurchaseInfo& info, bool success,
                                 const juce::String& statusDescription) override {
        const bool ownsPro = success && info.purchase.productIds.contains(kProProductId);
        if (ownsPro) {
            owner.entitlements_.setPro(true);
            owner.persistEntitlement(true);
        }
        if (owner.purchaseDone_) {
            if (owner.purchaseTimeout_) owner.purchaseTimeout_->cancel();
            auto done = std::move(owner.purchaseDone_);
            owner.purchaseDone_ = nullptr;
            done(ownsPro, statusDescription);
        }
        if (owner.shell_ != nullptr) owner.shell_->refreshProState();
    }
};

// JUCE 9's JuceBillingClient.java connectAndExecute() drops the queued
// runnable when onBillingSetupFinished() returns non-OK, and
// launchBillingFlow()'s returned BillingResult is ignored — for
// BILLING_UNAVAILABLE / not-signed-into-Play devices (the emulator has no
// Play Services), productPurchaseFinished() never fires. Without this,
// purchaseDone_ (and the paywall's busy state, which deliberately survives
// close/reopen) would be wedged for the rest of the session. Message
// thread only, one-shot: stopTimer() first thing in the callback, and the
// listener cancels us on a normal resolution.
struct AndroidAudioApp::PurchaseTimeoutImpl : AndroidAudioApp::PurchaseTimeout,
                                              private juce::Timer {
    explicit PurchaseTimeoutImpl(AndroidAudioApp& o) : owner(o) {}
    AndroidAudioApp& owner;

    void arm() override { startTimer(45000); }
    void cancel() override { stopTimer(); }

    void timerCallback() override {
        stopTimer();
        if (!owner.purchaseDone_) return;   // already resolved by the listener
        auto done = std::move(owner.purchaseDone_);
        owner.purchaseDone_ = nullptr;
        // Em dash via fromUTF8 (non-ASCII glyphs house rule).
        done(false, juce::String::fromUTF8("Store unavailable \xE2\x80\x94 try again later"));
    }
};

void AndroidAudioApp::initBilling() {
    // Seed from cache so an offline launch keeps Pro unlocked.
    const auto parsed = juce::JSON::parse(entitlementCacheFile().loadFileAsString());
    if (auto* obj = parsed.getDynamicObject()) entitlements_.setPro(bool(obj->getProperty("pro")));
    // setProServices() (called just before this, in the ctor) already wired
    // isPro_ against a default-constructed entitlements_ (pro=false); push
    // the real cache-seeded value now so PlayScreen's lock glyphs reflect a
    // returning Pro user immediately, not just after the async store round
    // trip (which never lands at all if the device is offline).
    if (shell_ != nullptr) shell_->refreshProState();
    billingListener_ = std::make_unique<BillingListener>(*this);
    auto* iap = juce::InAppPurchases::getInstance();
    iap->addListener(billingListener_.get());
    iap->restoreProductsBoughtList(false);            // async ownership query
    iap->getProductsInformation({ kProProductId });   // async: paywall price label
}

void AndroidAudioApp::purchasePro(std::function<void(bool, juce::String)> done) {
    // Refuse to start a second purchase while one is still unresolved: a
    // fresh purchasePro() call would overwrite purchaseDone_ and silently
    // strand the first caller's callback forever (nothing else ever invokes
    // the one it replaced). The paywall UI already guards against a second
    // tap, but this is the host-side backstop for any other caller.
    if (purchaseDone_) {
        if (done) done(false, "A purchase is already in progress");
        return;
    }
    purchaseDone_ = std::move(done);
    if (purchaseTimeout_ == nullptr)
        purchaseTimeout_ = std::make_unique<AndroidAudioApp::PurchaseTimeoutImpl>(*this);
    purchaseTimeout_->arm();
    juce::InAppPurchases::getInstance()->purchaseProduct(kProProductId);
}

void AndroidAudioApp::restorePurchases(std::function<void(bool, juce::String)> done) {
    // The listener's purchasesListRestored applies the result; report
    // completion optimistically after it fires. For the explicit button,
    // re-query and answer from the refreshed state.
    juce::InAppPurchases::getInstance()->restoreProductsBoughtList(true);
    juce::MessageManager::callAsync([this, done = std::move(done)] {
        done(entitlements_.isPro(),
             entitlements_.isPro() ? "Pro restored" : "No purchase found yet");
    });
}
