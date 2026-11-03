#pragma once
#include <Arduino.h>

#include "ui/app.h"

// =============================================================================
//  Mitteilungen vom Handy (NOTIF|key|app|titel|text).
//  Antippen öffnet die Detailansicht, "Löschen" entfernt sie auch am Handy.
// =============================================================================
class MessagesApp : public WatchApp {
public:
    const char* name() const override { return "Mitteilungen"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { rebuild(); }

    // Gemeinsam mit dem Banner genutzt: App-Farbe + Buchstabe
    static lv_obj_t* appBadge(lv_obj_t* parent, const String& app, lv_coord_t size);
    static void formatAge(char* buf, size_t len, uint32_t receivedAt);
    static void openDetail(const String& key);

private:
    void rebuild();

    lv_obj_t* m_count = nullptr;
    lv_obj_t* m_list  = nullptr;
    lv_obj_t* m_empty = nullptr;
};
