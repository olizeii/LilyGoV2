#pragma once
#include "ui/app.h"

// Platzhalter-Kachel für noch nicht implementierte Apps (Icon + Titel).
class PlaceholderApp : public WatchApp {
public:
    PlaceholderApp(const char* title, const char* symbol) : m_title(title), m_symbol(symbol) {}

    const char* name() const override { return m_title; }
    void create(lv_obj_t* tile) override;

private:
    const char* m_title;
    const char* m_symbol;
};
