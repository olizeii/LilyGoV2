#pragma once
#include <lvgl.h>

#include "ui/app.h"

// =============================================================================
//  Spalte X=1 – Watchfaces (Start-Spalte)
//    Y=0 DigitalFace   Y=1 AnalogFace   Y=2 ModernFace   Y=3 InfoTile
//
//  Neues Watchface: Klasse von WatchApp ableiten, in registerColumn() eintragen
//  und ui::WATCHFACE_COUNT erhöhen (Info-Kachel rutscht eine Reihe nach unten).
// =============================================================================
namespace watchfaces {

void registerColumn(uint8_t col);

}  // namespace watchfaces

// --- Y=0 ---------------------------------------------------------------------
class DigitalFace : public WatchApp {
public:
    const char* name() const override { return "Digital"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { onTick(); }
    void onTick() override;

private:
    lv_obj_t* m_date   = nullptr;
    lv_obj_t* m_time   = nullptr;
    lv_obj_t* m_status = nullptr;
};

// --- Y=1 ---------------------------------------------------------------------
class AnalogFace : public WatchApp {
public:
    const char* name() const override { return "Analog"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { onTick(); }
    void onTick() override;

private:
    static void setHand(lv_obj_t* line, lv_point_t* pts, float angleDeg, int length, int tail);

    lv_point_t m_tickPts[12][2];
    lv_point_t m_hourPts[2], m_minPts[2], m_secPts[2];
    lv_obj_t* m_hour = nullptr;
    lv_obj_t* m_min  = nullptr;
    lv_obj_t* m_sec  = nullptr;
    lv_obj_t* m_date = nullptr;
};

// --- Y=2 ---------------------------------------------------------------------
class ModernFace : public WatchApp {
public:
    const char* name() const override { return "Modern"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { onTick(); }
    void onTick() override;

private:
    lv_obj_t* m_ring    = nullptr;  // Sekunden-Ring
    lv_obj_t* m_weekday = nullptr;
    lv_obj_t* m_hour    = nullptr;
    lv_obj_t* m_minute  = nullptr;
    lv_obj_t* m_date    = nullptr;
};

// --- Y=3 ---------------------------------------------------------------------
class InfoTile : public WatchApp {
public:
    const char* name() const override { return "Info"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { refresh(); }
    void onHide() override { closeBatteryModal(); }
    void onTick() override { refresh(); }

private:
    void refresh();
    void openBatteryModal();
    void closeBatteryModal();
    void refreshModal();

    lv_obj_t* m_batteryIcon  = nullptr;
    lv_obj_t* m_batteryValue = nullptr;
    lv_obj_t* m_location     = nullptr;
    lv_obj_t* m_bleStatus    = nullptr;
    lv_obj_t* m_darkSwitch   = nullptr;

    // Akku-Overlay (lv_layer_top)
    lv_obj_t* m_modal = nullptr;
    struct BatteryRow {
        lv_obj_t* value;
        lv_obj_t* bar;
    } m_rows[3] = {};
};
