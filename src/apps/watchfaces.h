#pragma once
#include <lvgl.h>

#include "ui/app.h"

// =============================================================================
//  Spalte X=1 – Watchfaces (Start-Spalte)
//    Y=0 DigitalFace   Y=1 AnalogFace   Y=2 ModernFace
//    Y=3 ActivityTile  Y=4 InfoTile
//
//  Neues Watchface: Klasse von WatchApp ableiten, in registerColumn() eintragen
//  und ui::WATCHFACE_COUNT erhöhen (Aktivität + Info rutschen eine Reihe nach unten).
// =============================================================================
namespace watchfaces {

void registerColumn(uint8_t col);

}  // namespace watchfaces

// --- Y=0  Digital: Datum, große Uhrzeit, Status ------------------------------
class DigitalFace : public WatchApp {
public:
    const char* name() const override { return "Digital"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { onTick(); }
    void onTick() override;

private:
    lv_obj_t* m_date     = nullptr;
    lv_obj_t* m_time     = nullptr;
    lv_obj_t* m_status   = nullptr;  // Flex-Zeile
    lv_obj_t* m_ble      = nullptr;
    lv_obj_t* m_steps    = nullptr;
    lv_obj_t* m_batIcon  = nullptr;
    lv_obj_t* m_batValue = nullptr;
};

// --- Y=1  Analog: Minutenskala, Zeiger mit Steg, Datum bei 3 Uhr -------------
class AnalogFace : public WatchApp {
public:
    const char* name() const override { return "Analog"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { onTick(); }
    void onTick() override;

private:
    struct Hand {
        lv_obj_t* stem  = nullptr;  // dünner Steg ab der Mitte
        lv_obj_t* blade = nullptr;  // breites Zeigerblatt
        lv_point_t stemPts[2];
        lv_point_t bladePts[2];
    };
    static lv_obj_t* makeLine(lv_obj_t* tile, lv_style_t* style, lv_coord_t width);
    static void setHand(Hand& h, float angleDeg, int stemLen, int length);

    lv_point_t m_tickPts[60][2];
    Hand m_hour, m_min;
    lv_obj_t* m_sec = nullptr;
    lv_point_t m_secPts[2];
    lv_obj_t* m_weekday = nullptr;
    lv_obj_t* m_day     = nullptr;
    lv_obj_t* m_steps   = nullptr;
};

// --- Y=2  Modern: gestapelte Ziffern, Sekundenring ---------------------------
class ModernFace : public WatchApp {
public:
    const char* name() const override { return "Modern"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { onTick(); }
    void onTick() override;

private:
    lv_obj_t* m_ring    = nullptr;
    lv_obj_t* m_weekday = nullptr;
    lv_obj_t* m_hour    = nullptr;
    lv_obj_t* m_minute  = nullptr;
    lv_obj_t* m_date    = nullptr;
};

// --- Y=3  Aktivität: Schritte-Ring (Ziel), Strecke, Energie ----------------
class ActivityTile : public WatchApp {
public:
    const char* name() const override { return "Aktivität"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { refresh(); }

private:
    void refresh();
    void applyColors();

    lv_obj_t* m_ring  = nullptr;
    lv_obj_t* m_steps = nullptr;
    lv_obj_t* m_goal  = nullptr;
    lv_obj_t* m_dist  = nullptr;
    lv_obj_t* m_kcal  = nullptr;
};

// --- Y=4  Info: gruppierte Liste (Akku, Standort, Handy, Dunkelmodus) --------
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

    lv_obj_t* m_batteryIcon  = nullptr;  // Glyph im Zeilen-Icon
    lv_obj_t* m_batteryValue = nullptr;
    lv_obj_t* m_location     = nullptr;
    lv_obj_t* m_bleStatus    = nullptr;
    lv_obj_t* m_darkSwitch   = nullptr;

    // Akku-Übersicht (lv_layer_top)
    lv_obj_t* m_modal = nullptr;
    struct BatteryRow {
        lv_obj_t* value;
        lv_obj_t* bar;
    } m_rows[3] = {};
};
