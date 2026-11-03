#pragma once
#include "ui/app.h"

// =============================================================================
//  Spotify / Medien – "Jetzt läuft" im watchOS-Stil.
//  Daten: MEDIA|... vom Handy (WatchBridge liest die aktive Medien-Session).
//  Steuerung: MEDIA|toggle|next|prev|vol_up|vol_down ans Handy.
// =============================================================================
class MediaApp : public WatchApp {
public:
    const char* name() const override { return "Spotify"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { refresh(); }
    void onTick() override { updateProgress(); }

private:
    void refresh();
    void updateProgress();
    void command(const char* cmd);

    lv_obj_t* m_empty    = nullptr;
    lv_obj_t* m_player   = nullptr;
    lv_obj_t* m_app      = nullptr;
    lv_obj_t* m_title    = nullptr;
    lv_obj_t* m_artist   = nullptr;
    lv_obj_t* m_bar      = nullptr;
    lv_obj_t* m_posLabel = nullptr;
    lv_obj_t* m_durLabel = nullptr;
    lv_obj_t* m_playIcon = nullptr;
};
