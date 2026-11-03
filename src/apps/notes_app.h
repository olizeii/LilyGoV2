#pragma once
#include "ui/app.h"

// =============================================================================
//  Notizen – synchronisiert aus der WatchBridge-App.
//  Welche Notizen auf der Uhr erscheinen, wählst du in WatchBridge per Schalter.
//  Gespeichert im Flash (LittleFS) -> auch ohne Handy verfügbar.
// =============================================================================
class NotesApp : public WatchApp {
public:
    const char* name() const override { return "Notizen"; }
    void create(lv_obj_t* tile) override;

private:
    void rebuild();
    static void openNote(size_t index);

    lv_obj_t* m_count = nullptr;
    lv_obj_t* m_list  = nullptr;
    lv_obj_t* m_empty = nullptr;
};
