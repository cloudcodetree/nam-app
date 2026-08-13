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
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(obj)));
}

struct AndroidAudioApp::BillingListener : juce::InAppPurchases::Listener {
    explicit BillingListener(AndroidAudioApp& o) : owner(o) {}
    AndroidAudioApp& owner;

    void purchasesListRestored(const juce::Array<PurchaseInfo>& purchases, bool success,
                               const juce::String& /*statusDescription*/) override {
        if (!success) return;   // keep cached state on query failure
        bool owned = false;
        for (const auto& p : purchases)
            if (p.purchase.productIds.contains(kProProductId)) owned = true;
        owner.entitlements_.setPro(owned);
        owner.persistEntitlement(owned);
#if 0   // wired in Task 4: AppShell::refreshProState lands with the UI work
        if (owner.shell_ != nullptr) owner.shell_->refreshProState();
#endif
    }

    void productPurchaseFinished(const PurchaseInfo& info, bool success,
                                 const juce::String& statusDescription) override {
        const bool ownsPro = success && info.purchase.productIds.contains(kProProductId);
        if (ownsPro) {
            owner.entitlements_.setPro(true);
            owner.persistEntitlement(true);
        }
        if (owner.purchaseDone_) {
            auto done = std::move(owner.purchaseDone_);
            owner.purchaseDone_ = nullptr;
            done(ownsPro, statusDescription);
        }
#if 0   // wired in Task 4: AppShell::refreshProState lands with the UI work
        if (owner.shell_ != nullptr) owner.shell_->refreshProState();
#endif
    }
};

void AndroidAudioApp::initBilling() {
    // Seed from cache so an offline launch keeps Pro unlocked.
    const auto parsed = juce::JSON::parse(entitlementCacheFile().loadFileAsString());
    if (auto* obj = parsed.getDynamicObject()) entitlements_.setPro(bool(obj->getProperty("pro")));
    billingListener_ = std::make_unique<BillingListener>(*this);
    auto* iap = juce::InAppPurchases::getInstance();
    iap->addListener(billingListener_.get());
    iap->restoreProductsBoughtList(false);   // async ownership query
}

void AndroidAudioApp::purchasePro(std::function<void(bool, juce::String)> done) {
    purchaseDone_ = std::move(done);
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
