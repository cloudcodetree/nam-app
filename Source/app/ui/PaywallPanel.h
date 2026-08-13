#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Paywall overlay (house overlay style): bottom-sheet panel — rounded 14,
// inkA(0.18) border, height-capped to 55% of the hosting bounds, scrollable
// if its content overflows. The panel occupies the FULL bounds AppShell
// gives it; a tap outside the sheet itself closes it (mirrors the
// tap-away-closes behaviour of the stack-picker overlay in
// StacksScreen.cpp and the I/O device picker in AppShellChrome.cpp).
//
// Presentation only: AppShell wires onBuy/onRestore/onClose to the host's
// Pro purchase services and supplies the reason text + (later) the store's
// localized price.
class PaywallPanel : public juce::Component {
public:
    PaywallPanel ();

    void setReason (juce::String reason);
    // Store's localized price ("UNLOCK · $9.99" default placeholder until a
    // getProductsInformation() round trip lands — deliberately deferred to a
    // later pass; the store still charges the real price regardless).
    void setPriceText (juce::String price);
    // Disables BUY/RESTORE while a purchase/restore call is in flight — a
    // second tap while one is outstanding would drop the first callback.
    void setBusy (bool busy);
    // Brief result line from the host's DoneFn (e.g. a restore/purchase
    // failure, or "No purchase found yet"). Restore reports asynchronously
    // and must not be assumed to have succeeded just because it returned.
    void setStatus (juce::String status);

    std::function<void ()> onBuy, onRestore, onClose;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    int computeContentHeight () const;

    juce::String reason_, priceText_, status_;
    bool busy_ = false;

    juce::Rectangle<int> panelRect_;
    // Content-local rects (y = 0 at the top of the scrollable content, not
    // the panel) — translated by the current scroll offset at paint/hit-test
    // time, same convention as StacksScreen's row rects.
    juce::Rectangle<int> buyRect_, restoreRect_, notNowRect_;
    int contentH_ = 0;
    float scroll_ = 0.0f, pressScroll_ = 0.0f;
    bool pressed_ = false, moved_ = false;
    juce::Point<int> pressPos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaywallPanel)
};
