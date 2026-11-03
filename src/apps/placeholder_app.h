#pragma once
#include "ui/app.h"
#include "ui/theme.h"

// Platzhalter-Kachel für noch nicht implementierte Apps:
// farbiger App-Icon-Kreis (watchOS-Stil) + Name + Status.
class PlaceholderApp : public WatchApp {
public:
    PlaceholderApp(const char* title, const char* symbol, theme::Tint tint, bool darkGlyph = false)
        : m_title(title), m_symbol(symbol), m_tint(tint), m_darkGlyph(darkGlyph) {}

    const char* name() const override { return m_title; }
    void create(lv_obj_t* tile) override;

private:
    const char* m_title;
    const char* m_symbol;
    theme::Tint m_tint;
    bool m_darkGlyph;
};
