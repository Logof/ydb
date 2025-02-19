#pragma once
#include "defs.h"
#include "blobstorage_pdisk_defs.h"

namespace NKikimr {
namespace NPDisk {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Color limits for the Quota Tracker
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct TDiskColor {
    i64 Multiplier = 0;
    i64 Divisor = 1;
    i64 Addend = 0;

    TString ToString() const {
        return TStringBuilder() << Multiplier << " / " << Divisor << " + " << Addend;
    }

    i64 CalculateQuota(i64 total) const {
        return total * Multiplier / Divisor + Addend;
    }
};

struct TColorLimits {
    TDiskColor Black;
    TDiskColor Red;
    TDiskColor Orange;
    TDiskColor PreOrange;
    TDiskColor LightOrange;
    TDiskColor Yellow;
    TDiskColor LightYellow;
    TDiskColor Cyan;

    void Print(IOutputStream &str) {
        str << "  Black = Total * " << Black.ToString() << "\n";
        str << "  Red = Total * " << Red.ToString() << "\n";
        str << "  Orange = Total * " << Orange.ToString() << "\n";
        str << "  PreOrange = Total * " << PreOrange.ToString() << "\n";
        str << "  LightOrange = Total * " << LightOrange.ToString() << "\n";
        str << "  Yellow = Total * " << Yellow.ToString() << "\n";
        str << "  LightYellow = Total * " << LightYellow.ToString() << "\n";
        str << "  Cyan = Total * " << Cyan.ToString() << "\n";
    }

    static TColorLimits MakeChunkLimits() {
        return {
            {1,   1000, 2}, // Black: Leave bare minimum for disaster recovery
            {10,  1000, 3}, // Red
            {30,  1000, 4}, // Orange
            {50,  1000, 4}, // PreOrange
            {65,  1000, 5}, // LightOrange
            {80,  1000, 6}, // Yellow: Stop serving user writes at 8% free space
            {100, 1000, 7}, // LightYellow: Ask tablets to move to another group at 10% free space
            {130, 1000, 8}, // Cyan: 13% free space or less
        };
    }

    static TColorLimits MakeLogLimits() {
        return {
            {250, 1000}, // Black: Stop early to leave some space for disaster recovery
            {350, 1000}, // Red
            {500, 1000}, // Orange
            {600, 1000}, // PreOrange
            {700, 1000}, // LightOrange
            {900, 1000}, // Yellow
            {930, 1000}, // LightYellow
            {982, 1000}, // Cyan: Ask to cut log
        };
    }
};

} // NPDisk
} // NKikimr

