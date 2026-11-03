#pragma once
#include "ui/app.h"

// =============================================================================
//  Stundenplan (WebUntis über WatchBridge).
//  Daten: UNTIS|#Titel\n08:00;08:45;Mathe;R204;MUE;N;Info ...
//  Status: N normal, C Entfall, S Vertretung/geändert, E Prüfung.
//  Beim Öffnen fragt die Uhr per UNTIS_REQ eine Aktualisierung an.
// =============================================================================
class UntisApp : public WatchApp {
public:
    const char* name() const override { return "Stundenplan"; }
    void create(lv_obj_t* tile) override;
    void onShow() override;
    void onTick() override;

private:
    void rebuild();
    void scrollToNow(bool anim);
    int currentIndex() const;  // laufende oder nächste Stunde, -1 = keine mehr

    lv_obj_t* m_title = nullptr;
    lv_obj_t* m_list  = nullptr;
    lv_obj_t* m_empty = nullptr;
    int m_highlighted = -2;
};
