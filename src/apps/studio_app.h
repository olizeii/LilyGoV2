#pragma once
#include <Arduino.h>

#include "core/studio.h"
#include "ui/app.h"

// =============================================================================
//  Studio – Mini-DAW: oben der Beat (aus WatchBridge), darunter bis zu 3
//  Gesangsspuren. Spur antippen = auswählen, ziehen = verschieben, 🗑 = löschen.
//  Freie Stelle auf der Zeitleiste antippen = Startpunkt setzen.
//  ▶ spielt alles über den Lautsprecher, ● nimmt auf (Beat läuft mit).
//  Seitentaste = Play/Stopp.
// =============================================================================
class StudioApp : public WatchApp {
public:
    const char* name() const override { return "Studio"; }
    void create(lv_obj_t* tile) override;
    void onShow() override;
    void onHide() override;
    bool onButton() override;

private:
    void tick();
    void layout();                 // Spuren neu anordnen (nach Änderungen)
    void togglePlay();
    void toggleRecord();
    uint32_t viewLength() const;   // sichtbare Länge der Zeitleiste (Samples)
    lv_coord_t xFor(uint32_t samples) const;
    uint32_t samplesFor(lv_coord_t dx) const;

    static void onClipEvent(lv_event_t* e);
    static void onLaneClick(lv_event_t* e);

    lv_obj_t* m_header   = nullptr;
    lv_obj_t* m_timeLbl  = nullptr;
    lv_obj_t* m_area     = nullptr;   // Zeitleiste
    lv_obj_t* m_beatBar  = nullptr;
    lv_obj_t* m_beatLbl  = nullptr;
    lv_obj_t* m_lane[studio::MAX_CLIPS] = {};
    lv_obj_t* m_clip[studio::MAX_CLIPS] = {};
    lv_obj_t* m_head     = nullptr;   // Abspielkopf
    lv_obj_t* m_btnStart = nullptr;
    lv_obj_t* m_btnPlay  = nullptr;
    lv_obj_t* m_btnRec   = nullptr;
    lv_obj_t* m_recDot   = nullptr;
    lv_obj_t* m_btnDel   = nullptr;
    lv_timer_t* m_timer  = nullptr;

    uint32_t m_rev     = 0;
    uint32_t m_cursor  = 0;     // Startpunkt (Samples)
    int m_selected     = -1;
    uint32_t m_view    = 0;     // fixiert während des Ziehens
    // Ziehen
    int m_dragIdx      = -1;
    lv_coord_t m_dragX0 = 0;
    uint32_t m_dragOff0 = 0;
    bool m_dragMoved   = false;
    String m_msg;
    uint32_t m_msgUntil = 0;
};
