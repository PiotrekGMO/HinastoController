// Texts of the firmware. They are chosen at compile time: one build (one .bin) per language, selected with
// -DHN_LANG_PL / _EN / _DE / _CS / _SK or by a generated lang_select.h that holds one such #define
// (tools/publish-firmware.ps1 writes it into a temporary copy of the sketch, so every language shares the compiled
// Arduino core). Without either, the English build is made.
// Every text is a macro of the same name in each lang_xx.h; keep their order and the printf formats identical,
// and keep ' and " out of the P_* texts (they are placed inside the HTML and JavaScript of the setup page).
#pragma once

#if __has_include("lang_select.h")
#include "lang_select.h"
#endif

#if defined(HN_LANG_PL)
#include "lang_pl.h"
#elif defined(HN_LANG_DE)
#include "lang_de.h"
#elif defined(HN_LANG_CS)
#include "lang_cs.h"
#elif defined(HN_LANG_SK)
#include "lang_sk.h"
#else
#include "lang_en.h"
#endif
