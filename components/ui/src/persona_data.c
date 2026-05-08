/*
 * persona_data.c — ASCII art animation frames for 18 species × 7 states
 *
 * All frames use lv_font_unscii_8 (8×8 px monospace).  Each string has
 * lines separated by \n and is stored in flash (.rodata).
 *
 * State order (sm_state_t): SLEEP IDLE BUSY ATTENTION CELEBRATE DIZZY HEART
 */

#include "ui/persona.h"

/* ---- 1. CAT -------------------------------------------------------- */
static const char * const c_cat_sleep[] = {
    "  /\\_/\\  \n ( -_-) z\n  |~~~|  ",
    "  /\\_/\\  \n ( -_-)zZ\n  |~~~|  ",
    NULL
};
static const char * const c_cat_idle[] = {
    "  /\\_/\\  \n ( ^.^ ) \n  |~~~|  ",
    "  /\\_/\\  \n ( -.- ) \n  |~~~|  ",
    NULL
};
static const char * const c_cat_busy[] = {
    "  /\\_/\\  \n ( o.o )/\n  |\\~/|  ",
    "  /\\_/\\  \n ( o.o )-\n  |~~/|  ",
    "  /\\_/\\  \n ( o.o )\\\n  |~/~|  ",
    NULL
};
static const char * const c_cat_attention[] = {
    "  /\\_/\\  \n (!!! !) \n  |!!!|  ",
    "  /\\_/\\  \n ( !! !) \n  |!~!|  ",
    NULL
};
static const char * const c_cat_celebrate[] = {
    "  /\\_/\\  \n (*^_^*) \n  \\o~o/ *",
    " */\\_/\\* \n ( ^o^ ) \n  \\~o~/ *",
    "  /\\_/\\  \n (*^_^*)*\n  \\~~~/ *",
    NULL
};
static const char * const c_cat_dizzy[] = {
    "  /\\_/\\  \n (@_@) ~ \n  |~~~|  ",
    "  /\\_/\\  \n (x_x) ~ \n  |~~~|  ",
    "  /\\_/\\  \n (@.@)  ~\n  |~~~|  ",
    NULL
};
static const char * const c_cat_heart[] = {
    "  /\\_/\\  \n (>.<) v \n  |vvv|  ",
    "  /\\_/\\  \n (>.<)   \n  |<3>|  ",
    NULL
};

/* ---- 2. DOG -------------------------------------------------------- */
static const char * const c_dog_sleep[] = {
    " (^.   .^)\n ( -_-) z\n   U   U  ",
    " (^.   .^)\n ( -_-)Zz\n   U   U  ",
    NULL
};
static const char * const c_dog_idle[] = {
    " (^.   .^)\n ( o.o ) \n   U   U  ",
    " (^.   .^)\n ( -.-)  \n   U   U  ",
    NULL
};
static const char * const c_dog_busy[] = {
    " (^.   .^)\n ( o.o )/\n  /U   U\\ ",
    " (^.   .^)\n ( o.o )-\n  |U   U| ",
    " (^.   .^)\n ( o.o )\\\n  \\U   U/ ",
    NULL
};
static const char * const c_dog_attention[] = {
    " (^.   .^)\n (!!! !)  \n   U ! U  ",
    " (^.   .^)\n ( !!!!)  \n   U!!!U  ",
    NULL
};
static const char * const c_dog_celebrate[] = {
    " (^.   .^)\n (*^v^*) *\n  \\U~U/ * ",
    "*(^.   .^)\n ( ^o^ ) *\n   \\o/   ",
    " (^.   .^)\n (*^v^*)  \n  \\~~~/ *",
    NULL
};
static const char * const c_dog_dizzy[] = {
    " (^.   .^)\n (@_@) ~  \n   U   U  ",
    " (^.   .^)\n (x_x)~   \n   U   U  ",
    " (^.   .^)\n (@.@)  ~ \n   U   U  ",
    NULL
};
static const char * const c_dog_heart[] = {
    " (^.   .^)\n (>w<) v  \n   U v U  ",
    " (^.   .^)\n (>w<)    \n   U<3>U  ",
    NULL
};

/* ---- 3. BEAR ------------------------------------------------------- */
static const char * const c_bear_sleep[] = {
    " (U)   (U)\n ( -_-) z\n  (     ) ",
    " (U)   (U)\n ( -_-)Zz\n  (     ) ",
    NULL
};
static const char * const c_bear_idle[] = {
    " (U)   (U)\n ( ^.^ ) \n  (     ) ",
    " (U)   (U)\n ( -.- ) \n  (     ) ",
    NULL
};
static const char * const c_bear_busy[] = {
    " (U)   (U)\n ( o.o )/\n  (\\   /) ",
    " (U)   (U)\n ( o.o )|\n  (|   |) ",
    " (U)   (U)\n ( o.o )\\\n  (/   \\) ",
    NULL
};
static const char * const c_bear_attention[] = {
    " (U)   (U)\n (!!! !)  \n  (! ! !) ",
    " (U)   (U)\n ( !!!! ) \n  (!!!!!) ",
    NULL
};
static const char * const c_bear_celebrate[] = {
    " (U)   (U)\n (*^_^*) *\n  \\~o~/ * ",
    "*(U)   (U)\n ( ^o^ ) *\n   \\o/   ",
    " (U)   (U)\n (*^_^*)  \n  \\*~*/ *",
    NULL
};
static const char * const c_bear_dizzy[] = {
    " (U)   (U)\n (@_@) ~  \n  (~   ~) ",
    " (U)   (U)\n (x_x) ~  \n  (~   ~) ",
    " (U)   (U)\n (@.@)  ~ \n  (~   ~) ",
    NULL
};
static const char * const c_bear_heart[] = {
    " (U)   (U)\n (>.<) v  \n  (v   v) ",
    " (U)   (U)\n (>.<)    \n  (<3> <3)",
    NULL
};

/* ---- 4. FOX -------------------------------------------------------- */
static const char * const c_fox_sleep[] = {
    " /\\   /\\ \n( -_-) z \n (     )  ",
    " /\\   /\\ \n( -_-)Zz \n (     )  ",
    NULL
};
static const char * const c_fox_idle[] = {
    " /\\   /\\ \n( ^.^ )  \n (  ~  )  ",
    " /\\   /\\ \n( -.- )  \n (  ~  )  ",
    NULL
};
static const char * const c_fox_busy[] = {
    " /\\   /\\ \n( o.o ) / \n (/\\~/\\) ",
    " /\\   /\\ \n( o.o ) - \n (|~|~|)  ",
    " /\\   /\\ \n( o.o ) \\ \n (\\~\\/\\) ",
    NULL
};
static const char * const c_fox_attention[] = {
    " /\\   /\\ \n(!!!.!!!) \n (!!!!!)  ",
    " /\\   /\\ \n( !! !! ) \n (!~!!!)  ",
    NULL
};
static const char * const c_fox_celebrate[] = {
    " /\\   /\\ \n(*^_^*) * \n \\o~o/ *  ",
    "*/\\   /\\*\n( ^o^ )   \n  \\~o~/   ",
    " /\\   /\\ \n(*^_^*) * \n \\~*~/ *  ",
    NULL
};
static const char * const c_fox_dizzy[] = {
    " /\\   /\\ \n(@_@) ~   \n (~   ~)  ",
    " /\\   /\\ \n(x_x) ~   \n (~   ~)  ",
    " /\\   /\\ \n(@.@)  ~  \n (~   ~)  ",
    NULL
};
static const char * const c_fox_heart[] = {
    " /\\   /\\ \n(>.<) v   \n (v   v)  ",
    " /\\   /\\ \n(>.<)     \n (<3> <3) ",
    NULL
};

/* ---- 5. BUNNY ------------------------------------------------------ */
static const char * const c_bunny_sleep[] = {
    " (| |)    \n ( -_-) z \n ( U   U) ",
    " (| |)    \n ( -_-)Zz \n ( U   U) ",
    NULL
};
static const char * const c_bunny_idle[] = {
    " (| |)    \n ( ^.^ )  \n ( U   U) ",
    " (| |)    \n ( -.- )  \n ( U   U) ",
    NULL
};
static const char * const c_bunny_busy[] = {
    " (| |)    \n ( o.o ) /\n (/U   U\\)",
    " (| |)    \n ( o.o ) |\n (|U   U|)",
    " (| |)    \n ( o.o ) \\\n (\\U   U/)",
    NULL
};
static const char * const c_bunny_attention[] = {
    " (|!|)    \n (!!!.!!!) \n ( U ! U) ",
    " (|!|)    \n ( !! !! ) \n ( U!!!U) ",
    NULL
};
static const char * const c_bunny_celebrate[] = {
    " (| |)*   \n (*^_^*) * \n (\\U~U/*) ",
    "*(| |)*   \n ( ^o^ ) * \n  (\\o/)   ",
    " (| |) *  \n (*^_^*)   \n (\\~*~/*)  ",
    NULL
};
static const char * const c_bunny_dizzy[] = {
    " (|~|)    \n (@_@) ~   \n (~U   U~) ",
    " (|~|)    \n (x_x)  ~  \n (~U   U~) ",
    " (|~|)    \n (@.@)   ~ \n (~U   U~) ",
    NULL
};
static const char * const c_bunny_heart[] = {
    " (|v|)    \n (>.<) v   \n (vU   Uv) ",
    " (|v|)    \n (>.<)     \n (vU<3>Uv) ",
    NULL
};

/* ---- 6. FROG ------------------------------------------------------- */
static const char * const c_frog_sleep[] = {
    "o (   ) o \n (-_-)  z \n \\-----/  ",
    "o (   ) o \n (-_-)  Z \n \\-----/  ",
    NULL
};
static const char * const c_frog_idle[] = {
    "o (   ) o \n (^. .^)  \n \\-----/  ",
    "o (   ) o \n (-. .-)  \n \\-----/  ",
    NULL
};
static const char * const c_frog_busy[] = {
    "o (   ) o \n (o. .o)/  \n \\--/--/  ",
    "o (   ) o \n (o. .o)-  \n \\-/-/-/  ",
    "o (   ) o \n (o. .o)\\  \n \\//--/   ",
    NULL
};
static const char * const c_frog_attention[] = {
    "O (   ) O \n (!! !!!)  \n \\!!!!!/ !",
    "O (   ) O \n (! !!!!)  \n \\!!!!!/ !",
    NULL
};
static const char * const c_frog_celebrate[] = {
    "o (   ) o \n (*^. .^*)*\n  \\o~o/  *",
    "*(   )  * \n ( ^. .^ ) \n  \\~o~/  *",
    "o (   ) o \n (*^ .^*) *\n  \\*~*/  *",
    NULL
};
static const char * const c_frog_dizzy[] = {
    "o (   ) o \n (@_ @) ~  \n \\~~~~~/ ~",
    "o (   ) o \n (x_ x) ~  \n \\~~~~~/ ~",
    "o (   ) o \n (@. @)  ~ \n \\~~~~~/ ~",
    NULL
};
static const char * const c_frog_heart[] = {
    "o (   ) o \n (>. .<) v \n \\v~~~v/  ",
    "o (   ) o \n (>. .<)   \n \\<3>~~/ v",
    NULL
};

/* ---- 7. OWL -------------------------------------------------------- */
static const char * const c_owl_sleep[] = {
    " /\\   /\\  \n((-.-)(-.-)\n M\\___/M  ",
    " /\\   /\\  \n((-.-)(-.-)z\n M\\___/M  ",
    NULL
};
static const char * const c_owl_idle[] = {
    " /\\   /\\  \n((O.O)(O.O)\n M\\___/M  ",
    " /\\   /\\  \n((-.-)(-.-))\n M\\___/M  ",
    NULL
};
static const char * const c_owl_busy[] = {
    " /\\   /\\  \n((o.o)(o.o)/\n M|---|M  ",
    " /\\   /\\  \n((o.o)(o.o)-\n M|---|M  ",
    " /\\   /\\  \n((o.o)(o.o)\\\n M|---|M  ",
    NULL
};
static const char * const c_owl_attention[] = {
    " /!\\  /!\\ \n((!.!)(! .!)\n M\\!_!/M  ",
    " /!\\  /!\\ \n((! .)(. !))\n M\\!!!/ !  ",
    NULL
};
static const char * const c_owl_celebrate[] = {
    " /\\   /\\  \n((*.*)(*.*))*\n M\\~~/M  *",
    "*/\\  /\\*  \n(( ^. .^ )) *\n M\\o~/M  *",
    " /\\   /\\  \n((*.*)(*.*))*\n M\\*/M  *",
    NULL
};
static const char * const c_owl_dizzy[] = {
    " /\\   /\\  \n((@.@)(@.@))~\n M\\~~~/ M  ",
    " /\\   /\\  \n((x.x)(x.x))~\n M\\~~~/ M  ",
    " /\\   /\\  \n((@.@)(@.@)) ~\n M\\~~~/ M  ",
    NULL
};
static const char * const c_owl_heart[] = {
    " /v\\  /v\\ \n((*.*)(*.*))\n M\\vv/M v  ",
    " /v\\  /v\\ \n((>.<)(>.<))\n M\\<3/M v  ",
    NULL
};

/* ---- 8. PENGUIN ---------------------------------------------------- */
static const char * const c_penguin_sleep[] = {
    "  (     )  \n O(-_-)O z\n  (_____)  ",
    "  (     )  \n O(-_-)OZz\n  (_____)  ",
    NULL
};
static const char * const c_penguin_idle[] = {
    "  (     )  \n O(^.^)O   \n  (_____)  ",
    "  (     )  \n O(-.-) O  \n  (_____)  ",
    NULL
};
static const char * const c_penguin_busy[] = {
    "  (     )  \n O(o.o)O / \n  (|___|) ",
    "  (     )  \n O(o.o)O - \n  (|___|) ",
    "  (     )  \n O(o.o)O \\ \n  (|___|) ",
    NULL
};
static const char * const c_penguin_attention[] = {
    "  (     )  \n O(!!!!)O ! \n  (!!!!!)  ",
    "  (     )  \n O( !!! )O! \n  (!_!_!)  ",
    NULL
};
static const char * const c_penguin_celebrate[] = {
    "  (     )  \n O(*^*) O * \n  \\~o~/ *  ",
    "* (     )  \n O(^o^)O  * \n   \\o/  * ",
    "  (     )* \n O(*^*)O  * \n  \\*~*/ *  ",
    NULL
};
static const char * const c_penguin_dizzy[] = {
    "  (     )  \n O(@_@)O ~  \n  (~___~)  ",
    "  (     )  \n O(x_x)O ~  \n  (~___~)  ",
    "  (     )  \n O(@.@)O  ~ \n  (~___~)  ",
    NULL
};
static const char * const c_penguin_heart[] = {
    "  (     )  \n O(>.<)O v  \n  (v___v)  ",
    "  (     )  \n O(>.<)O    \n  (<3__<3)  ",
    NULL
};

/* ---- 9. GHOST ------------------------------------------------------ */
static const char * const c_ghost_sleep[] = {
    "   .---.   \n  ( -_-)z  \n  /|~~~|\\ ",
    "   .---.   \n  ( -_-)Zz \n  /|~~~|\\ ",
    NULL
};
static const char * const c_ghost_idle[] = {
    "   .---.   \n  ( o o )  \n  /|~~~|\\ ",
    "   .---.   \n  ( - - )  \n  /|~~~|\\ ",
    NULL
};
static const char * const c_ghost_busy[] = {
    "   .---.   \n  ( o o ) /\n  /|~~/|\\ ",
    "   .---.   \n  ( o o ) -\n  /|~~-|\\ ",
    "   .---.   \n  ( o o ) \\\n  /|~/~|\\ ",
    NULL
};
static const char * const c_ghost_attention[] = {
    "  .!-!-.   \n  (!.!)  ! \n  /|!!!|\\ ",
    "  .!-!-.   \n  ( !! )  !\n  /|!!!|\\ ",
    NULL
};
static const char * const c_ghost_celebrate[] = {
    "   .---.   \n *(* o *)* \n  /|~o~|\\*",
    "  *.--.*   \n  ( ^o^)*  \n  /|~*~|\\ ",
    "   .---.  *\n *(* o *)*  \n  /|~*~|\\ ",
    NULL
};
static const char * const c_ghost_dizzy[] = {
    "   .---.   \n  (@_@) ~  \n  /|~~~|\\ ",
    "   .---.   \n  (x_x) ~  \n  /|~~~|\\ ",
    "   .---.   \n  (@.@)  ~ \n  /|~~~|\\ ",
    NULL
};
static const char * const c_ghost_heart[] = {
    "   .---.   \n  (>.<) v  \n  /|v~v|\\ ",
    "   .---.   \n  (>.<)    \n  /|<3>|\\ ",
    NULL
};

/* ---- 10. CACTUS ---------------------------------------------------- */
static const char * const c_cactus_sleep[] = {
    "  * | *    \n --(-.-)-- z\n  |   |    ",
    "  * | *    \n --(-.-)--Zz\n  |   |    ",
    NULL
};
static const char * const c_cactus_idle[] = {
    "  * | *    \n --( . )-- \n  |   |    ",
    "  * | *    \n --(. .)-- \n  |   |    ",
    NULL
};
static const char * const c_cactus_busy[] = {
    "  * | *    \n --( o )--/ \n  |   |    ",
    "  * | *    \n --( o )--- \n  |   |    ",
    "  * | *    \n --( o )--\\ \n  |   |    ",
    NULL
};
static const char * const c_cactus_attention[] = {
    "  *!| !*   \n --(!!!)-!-\n  |!!!|    ",
    "  *!|!*    \n --( !!! )-!\n  |!|!|    ",
    NULL
};
static const char * const c_cactus_celebrate[] = {
    " *(* | *)*  \n --(*o*)-- *\n  |~*~| *  ",
    "  * | * *   \n --( ^o^)--*\n  |~~~| *  ",
    " *(* | *) * \n --(*o*)-- *\n  |~~~|    ",
    NULL
};
static const char * const c_cactus_dizzy[] = {
    "  *~| ~*   \n --(@_@)--~ \n  |~~~|    ",
    "  *~|~*    \n --(x_x)-- ~\n  |~~~|    ",
    "  *~| *    \n --(@.@)--  ~\n  |~~~|    ",
    NULL
};
static const char * const c_cactus_heart[] = {
    "  v | v    \n --(>.<)-- v\n  |v~v|    ",
    "  v | v    \n --(>.<)-- \n  |<3>|    ",
    NULL
};

/* ---- 11. ROBOT ----------------------------------------------------- */
static const char * const c_robot_sleep[] = {
    " [=======] \n |.(-.-).|z\n |=======| ",
    " [=======] \n |.(-.-).|Zz\n|=======| ",
    NULL
};
static const char * const c_robot_idle[] = {
    " [=======] \n |.(^.^).| \n |=======| ",
    " [=======] \n |.(-.-).| \n |=======| ",
    NULL
};
static const char * const c_robot_busy[] = {
    " [=======] \n |.(o.o)./| \n |======/| ",
    " [=======] \n |.(o.o).-| \n |======-| ",
    " [=======] \n |.(o.o).\\ | \n|======\\| ",
    NULL
};
static const char * const c_robot_attention[] = {
    " [!=====!] \n |.(!!!).| !\n |!!!!!!! ",
    " [!=====!] \n |.( !!).| !\n |!!=!!! ",
    NULL
};
static const char * const c_robot_celebrate[] = {
    " [=======] \n |.(*^*).*| \n |=\\~~/=|*",
    "*[=======] \n |.( ^o^).| *\n  |==o==| *",
    " [=======]*\n |.(*^*). *|\n |=~*~=| *",
    NULL
};
static const char * const c_robot_dizzy[] = {
    " [=======] \n |.(@_@).~| \n |=======| ",
    " [=======] \n |.(x_x).~| \n |=======| ",
    " [=======] \n |.(@.@). ~| \n|=======| ",
    NULL
};
static const char * const c_robot_heart[] = {
    " [=======] \n |.(>.<).v| \n |==v~v==| ",
    " [=======] \n |.(>.<).  | \n|==<3>==| ",
    NULL
};

/* ---- 12. ALIEN ----------------------------------------------------- */
static const char * const c_alien_sleep[] = {
    "   .----.   \n  (-_-) z  \n /|    |\\ ",
    "   .----.   \n  (-_-)Zz  \n /|    |\\ ",
    NULL
};
static const char * const c_alien_idle[] = {
    "   .----.   \n  (* o *)   \n /|    |\\ ",
    "   .----.   \n  (*-.-*)   \n /|    |\\ ",
    NULL
};
static const char * const c_alien_busy[] = {
    "   .----.   \n  (* o *) /  \n /|---/|\\ ",
    "   .----.   \n  (* o *) -  \n /|---|\\ ",
    "   .----.   \n  (* o *) \\  \n /|---\\|\\ ",
    NULL
};
static const char * const c_alien_attention[] = {
    "  .!--!-.   \n  (*!*!*)  !\n /|!!!!|\\ ",
    "  .!--!-.   \n  (!! !!!)  \n /|!!!!|\\ ",
    NULL
};
static const char * const c_alien_celebrate[] = {
    "   .----.  *\n *(* ^ *)*  \n /|~*~|\\*  ",
    "  *.----.*  \n  (* o *)*  \n  /|~o~|\\  ",
    "   .----.* \n  (* ^ *)  *\n /|~*~|\\ * ",
    NULL
};
static const char * const c_alien_dizzy[] = {
    "   .----.   \n  (@_@) ~   \n /|~~~|\\ ",
    "   .----.   \n  (x_x)  ~  \n /|~~~|\\ ",
    "   .----.   \n  (@.@)   ~ \n /|~~~|\\ ",
    NULL
};
static const char * const c_alien_heart[] = {
    "   .----.   \n  (*>.<*)  v \n /|v~~v|\\ ",
    "   .----.   \n  (*>.<*)    \n /|<3><3|\\ ",
    NULL
};

/* ---- 13. WIZARD ---------------------------------------------------- */
static const char * const c_wizard_sleep[] = {
    "    /|\\    \n   / | \\   \n  ( -_-)z  \n   ~---~   ",
    "    /|\\    \n   / | \\   \n  ( -_-)Zz \n   ~---~   ",
    NULL
};
static const char * const c_wizard_idle[] = {
    "    /|\\    \n   / | \\   \n  ( ^.^ )  \n   ~---~   ",
    "    /|\\    \n   / | \\   \n  ( -.- )  \n   ~---~   ",
    NULL
};
static const char * const c_wizard_busy[] = {
    "    /|\\   *\n * / | \\   \n  ( o.o ) * \n * ~---~   ",
    "   */|\\    \n  */ | \\  *\n  ( o.o )  *\n   ~---~ * ",
    "    /|\\  * \n  * / | \\ *\n  ( o.o )*  \n * ~---~  *",
    NULL
};
static const char * const c_wizard_attention[] = {
    "   /!|!\\   \n  /  |  \\  \n  (!!!.!!!) \n  ~!!!!!~   ",
    "   /!|!\\   \n  /  |  \\  \n  ( !!! !) !\n  ~!!!!!~ ! ",
    NULL
};
static const char * const c_wizard_celebrate[] = {
    "   */|\\*   \n  * / | \\ * \n  (*^_^*) * \n * \\~o~/  *",
    "  * /|\\ *  \n   / | \\   \n  ( ^o^ ) * \n   \\*o*/  *",
    "   */|\\*  *\n  * / | \\  \n  (*^_^*)  *\n * \\*~*/   ",
    NULL
};
static const char * const c_wizard_dizzy[] = {
    "    /|\\    \n   / | \\   \n  (@_@) ~  \n  (~---~) ~ ",
    "    /|\\    \n   / | \\   \n  (x_x)  ~  \n  (~---~)  ~ ",
    "    /|\\    \n   / | \\   \n  (@.@)   ~ \n  (~---~)   ~",
    NULL
};
static const char * const c_wizard_heart[] = {
    "    /|\\    \n   / | \\   \n  (>.<) v  \n   v---v   ",
    "    /|\\    \n   / | \\   \n  (>.<)    \n   <3--<3  ",
    NULL
};

/* ---- 14. NINJA ----------------------------------------------------- */
static const char * const c_ninja_sleep[] = {
    " [=======] \n [-(-.-)--]z\n  | | | |  ",
    " [=======] \n [-(-.-)--]Zz\n  | | | |  ",
    NULL
};
static const char * const c_ninja_idle[] = {
    " [=======] \n [-(^.^)--] \n  | | | |  ",
    " [=======] \n [-(-.-)--] \n  | | | |  ",
    NULL
};
static const char * const c_ninja_busy[] = {
    " [=======] \n [>(o.o)< ]/\n  |-| |-|  ",
    " [=======] \n [>(o.o)< ]-\n  |-|-|-|  ",
    " [=======] \n [>(o.o)< ]\\\n  |-|-| |  ",
    NULL
};
static const char * const c_ninja_attention[] = {
    " [=!!=!!=] \n [>(!!!)< ]!\n  |!|!|!|  ",
    " [=!!=!!=] \n [>(!.!)< ]!\n  |!|!|!| !",
    NULL
};
static const char * const c_ninja_celebrate[] = {
    " [=======] \n [>(*^*)<*] \n  \\| | |/  ",
    "*[=======] \n [>( ^ )<]*  \n   \\|o|/  *",
    " [=======]*\n [>(*^*)<] * \n  \\|~|/  * ",
    NULL
};
static const char * const c_ninja_dizzy[] = {
    " [=======] \n [>(@_@)<]~  \n  |~| |~|  ",
    " [=======] \n [>(x_x)<] ~ \n  |~|~|~|  ",
    " [=======] \n [>(@.@)<]  ~ \n  |~| |~|  ",
    NULL
};
static const char * const c_ninja_heart[] = {
    " [=======] \n [>(>.<)<] v \n  |v| |v|  ",
    " [=======] \n [>(>.<)<]   \n  |<3>|<3>|  ",
    NULL
};

/* ---- 15. DRAGON ---------------------------------------------------- */
static const char * const c_dragon_sleep[] = {
    " >*<   >*< \n ( -_-)  z \n  )===(   ",
    " >*<   >*< \n ( -_-) Zz \n  )===(   ",
    NULL
};
static const char * const c_dragon_idle[] = {
    " >*<   >*< \n ( ^.^ )   \n  )===(   ",
    " >*<   >*< \n ( -.- )   \n  )===(   ",
    NULL
};
static const char * const c_dragon_busy[] = {
    " >*<   >*</\n ( o.o )   \n  )===(/  ",
    " >*<   >*<-\n ( o.o )   \n  )===-|  ",
    " >*<   >*<\\\n ( o.o )   \n  )===(\\  ",
    NULL
};
static const char * const c_dragon_attention[] = {
    " >!<   >!< \n (!!! !)   !\n  )!!!(    ",
    " >!<   >!< \n ( !! ! )  !\n  )!!!(  ! ",
    NULL
};
static const char * const c_dragon_celebrate[] = {
    " >*<  *>*< \n (*^_^*)  * \n  )~*~( *  ",
    "*>*< *>*<  \n ( ^o^ ) *  \n  )~o~(*   ",
    " >*<* *>*< \n (*^_^*)    \n  )*~*(  * ",
    NULL
};
static const char * const c_dragon_dizzy[] = {
    " >*<   >*< \n (@_@)  ~   \n  )~~~(    ",
    " >*<   >*< \n (x_x)  ~   \n  )~~~(    ",
    " >*<   >*< \n (@.@)   ~  \n  )~~~(    ",
    NULL
};
static const char * const c_dragon_heart[] = {
    " >v<   >v< \n (>.<) v    \n  )v~v(    ",
    " >v<   >v< \n (>.<)      \n  )<3>(    ",
    NULL
};

/* ---- 16. SLIME ----------------------------------------------------- */
static const char * const c_slime_sleep[] = {
    "   .---.   \n  ( -_-)z  \n (       ) \n  -------  ",
    "   .---.   \n  ( -_-)Zz \n (       ) \n  -------  ",
    NULL
};
static const char * const c_slime_idle[] = {
    "   .---.   \n  ( ^.^ )  \n (       ) \n  -------  ",
    "   .---.   \n  ( -.- )  \n (       ) \n  -------  ",
    NULL
};
static const char * const c_slime_busy[] = {
    "  ..---..  \n  ( o.o )/  \n (  ~~~  )  \n  -------  ",
    "  .-----.  \n  ( o.o )-  \n (  ~~~  )  \n  -------  ",
    "  ..---..  \n  ( o.o )\\  \n (  ~~~  )  \n  -------  ",
    NULL
};
static const char * const c_slime_attention[] = {
    "  .!-!-.   \n  (!!!!)  ! \n (!!!!!!!!)  \n  -------  ",
    "  .!-!-.   \n  ( !!! )  !\n (!!!!!!!!)  \n  -------  ",
    NULL
};
static const char * const c_slime_celebrate[] = {
    "  *.--.* *  \n  (*^_^*)  * \n (*  ~~~  *) \n  --*--*--  ",
    "  .*--.*.   \n  ( *o* ) * \n (*  ~~~  *) \n  -* *-* -  ",
    "  *.--.*  * \n  (*^_^*)    \n (* ~~~  *)  \n  ---*---   ",
    NULL
};
static const char * const c_slime_dizzy[] = {
    "   .---.   \n  (@_@) ~  \n (  ~~~  ) ~\n  -------  ",
    "   .---.   \n  (x_x)  ~  \n (~  ~~  ~)  \n  -------  ",
    "   .---.   \n  (@.@)   ~ \n (~  ~~~  ~) \n  -------  ",
    NULL
};
static const char * const c_slime_heart[] = {
    "   .---.   \n  (>.<) v  \n (v  ~~~  v)\n  -------  ",
    "   .---.   \n  (>.<)    \n (<3>  ~<3>) \n  -------  ",
    NULL
};

/* ---- 17. SLOTH ----------------------------------------------------- */
static const char * const c_sloth_sleep[] = {
    " o--Y--o   \n  (-.-) z  \n   |   |   \n  _|___|_  ",
    " o--Y--o   \n  (-.-)Zz  \n   |   |   \n  _|___|_  ",
    NULL
};
static const char * const c_sloth_idle[] = {
    " o--Y--o   \n  (^. .^)  \n   |   |   \n  _|___|_  ",
    " o--Y--o   \n  (-. .-)  \n   |   |   \n  _|___|_  ",
    NULL
};
static const char * const c_sloth_busy[] = {
    " o--Y--o   \n  (o. .o)/ \n   |   |   \n  _|_/|_  ",
    " o--Y--o   \n  (o. .o)- \n   |   |   \n  _|-__|_  ",
    " o--Y--o   \n  (o. .o)\\ \n   |   |   \n  _|_\\|_  ",
    NULL
};
static const char * const c_sloth_attention[] = {
    " o--Y--o   \n  (!! !!)  !\n  !|   |!  \n  _|!_!|_  ",
    " o--Y--o   \n  ( !!!! )  \n   |!!!|   \n  _|!_!|_  ",
    NULL
};
static const char * const c_sloth_celebrate[] = {
    " o--Y--o  *\n  (*^ ^*) * \n  *|   |*  \n  _|~*~|_  ",
    "* o--Y--o  \n  ( ^ ^ ) * \n  *|   |*  \n  _|~o~|_  ",
    " o--Y--o * \n  (*^ ^*)   \n  *|   |*  \n  _|*~*|_  ",
    NULL
};
static const char * const c_sloth_dizzy[] = {
    " o--Y--o   \n  (@. .@) ~ \n   |   |   \n  _|~_~|_  ",
    " o--Y--o   \n  (x. .x)  ~\n   |   |   \n  _|~_~|_  ",
    " o--Y--o   \n  (@. .@)  ~ \n   |   |   \n  _|~_~|_  ",
    NULL
};
static const char * const c_sloth_heart[] = {
    " o--Y--o   \n  (>. .<) v \n   |   |   \n  _|v_v|_  ",
    " o--Y--o   \n  (>. .<)   \n   |   |   \n  _|<3 3>|_  ",
    NULL
};

/* ---- 18. DINO ------------------------------------------------------ */
static const char * const c_dino_sleep[] = {
    "    (  )   \n ( o_-) z  \n  |____)   \n   | |     ",
    "    (  )   \n ( o_-)Zz  \n  |____)   \n   | |     ",
    NULL
};
static const char * const c_dino_idle[] = {
    "    (  )   \n ( o.^)>   \n  |____)   \n   | |     ",
    "    (  )   \n ( -.-) >  \n  |____)   \n   | |     ",
    NULL
};
static const char * const c_dino_busy[] = {
    "    (  )   \n ( o.^)> / \n  |___/) \n   |/|     ",
    "    (  )   \n ( o.^)> - \n  |____) \n   |-|     ",
    "    (  )   \n ( o.^)> \\ \n  |___\\) \n   |\\|     ",
    NULL
};
static const char * const c_dino_attention[] = {
    "   (!  !)  \n (!.^!> !  \n  |!__!)   \n  !| |!    ",
    "   (!  !)  \n (!.^!)  > !\n  |!__!)   \n  !| |!    ",
    NULL
};
static const char * const c_dino_celebrate[] = {
    "   *(  )*  \n (*o.^)>*  \n  |~*~/) * \n  *| |*    ",
    " * (  )    \n ( *o^)> * \n  |~~*/) * \n  *| |*    ",
    "   *(  )*  \n (*o.^*)>  \n  |~*~/)   \n * | |  *  ",
    NULL
};
static const char * const c_dino_dizzy[] = {
    "    (  )   \n (@_^) ~   \n  |~~~~)   \n   | |     ",
    "    (  )   \n (x_^)  ~  \n  |~~~~)   \n   | |     ",
    "    (  )   \n (@.^)   ~ \n  |~~~~)   \n   | |     ",
    NULL
};
static const char * const c_dino_heart[] = {
    "    (  )   \n (>.<)> v  \n  |v__v)   \n   | |     ",
    "    (  )   \n (>.<)>    \n  |<3_<3)  \n   | |     ",
    NULL
};

/* ================================================================
 * Persona table
 * ================================================================ */

#define ANIM(frames_arr, ms) { (frames_arr), (ms) }

static const persona_t s_cat = {
    .name  = "Cat",
    .anims = {
        ANIM(c_cat_sleep,     700),
        ANIM(c_cat_idle,      500),
        ANIM(c_cat_busy,      200),
        ANIM(c_cat_attention, 250),
        ANIM(c_cat_celebrate, 200),
        ANIM(c_cat_dizzy,     150),
        ANIM(c_cat_heart,     600),
    },
};
static const persona_t s_dog = {
    .name  = "Dog",
    .anims = {
        ANIM(c_dog_sleep,     700),
        ANIM(c_dog_idle,      500),
        ANIM(c_dog_busy,      200),
        ANIM(c_dog_attention, 250),
        ANIM(c_dog_celebrate, 200),
        ANIM(c_dog_dizzy,     150),
        ANIM(c_dog_heart,     600),
    },
};
static const persona_t s_bear = {
    .name  = "Bear",
    .anims = {
        ANIM(c_bear_sleep,     700),
        ANIM(c_bear_idle,      500),
        ANIM(c_bear_busy,      200),
        ANIM(c_bear_attention, 250),
        ANIM(c_bear_celebrate, 200),
        ANIM(c_bear_dizzy,     150),
        ANIM(c_bear_heart,     600),
    },
};
static const persona_t s_fox = {
    .name  = "Fox",
    .anims = {
        ANIM(c_fox_sleep,     700),
        ANIM(c_fox_idle,      500),
        ANIM(c_fox_busy,      200),
        ANIM(c_fox_attention, 250),
        ANIM(c_fox_celebrate, 200),
        ANIM(c_fox_dizzy,     150),
        ANIM(c_fox_heart,     600),
    },
};
static const persona_t s_bunny = {
    .name  = "Bunny",
    .anims = {
        ANIM(c_bunny_sleep,     700),
        ANIM(c_bunny_idle,      500),
        ANIM(c_bunny_busy,      200),
        ANIM(c_bunny_attention, 250),
        ANIM(c_bunny_celebrate, 200),
        ANIM(c_bunny_dizzy,     150),
        ANIM(c_bunny_heart,     600),
    },
};
static const persona_t s_frog = {
    .name  = "Frog",
    .anims = {
        ANIM(c_frog_sleep,     700),
        ANIM(c_frog_idle,      500),
        ANIM(c_frog_busy,      200),
        ANIM(c_frog_attention, 250),
        ANIM(c_frog_celebrate, 200),
        ANIM(c_frog_dizzy,     150),
        ANIM(c_frog_heart,     600),
    },
};
static const persona_t s_owl = {
    .name  = "Owl",
    .anims = {
        ANIM(c_owl_sleep,     700),
        ANIM(c_owl_idle,      500),
        ANIM(c_owl_busy,      200),
        ANIM(c_owl_attention, 250),
        ANIM(c_owl_celebrate, 200),
        ANIM(c_owl_dizzy,     150),
        ANIM(c_owl_heart,     600),
    },
};
static const persona_t s_penguin = {
    .name  = "Penguin",
    .anims = {
        ANIM(c_penguin_sleep,     700),
        ANIM(c_penguin_idle,      500),
        ANIM(c_penguin_busy,      200),
        ANIM(c_penguin_attention, 250),
        ANIM(c_penguin_celebrate, 200),
        ANIM(c_penguin_dizzy,     150),
        ANIM(c_penguin_heart,     600),
    },
};
static const persona_t s_ghost = {
    .name  = "Ghost",
    .anims = {
        ANIM(c_ghost_sleep,     700),
        ANIM(c_ghost_idle,      500),
        ANIM(c_ghost_busy,      200),
        ANIM(c_ghost_attention, 250),
        ANIM(c_ghost_celebrate, 200),
        ANIM(c_ghost_dizzy,     150),
        ANIM(c_ghost_heart,     600),
    },
};
static const persona_t s_cactus = {
    .name  = "Cactus",
    .anims = {
        ANIM(c_cactus_sleep,     700),
        ANIM(c_cactus_idle,      500),
        ANIM(c_cactus_busy,      200),
        ANIM(c_cactus_attention, 250),
        ANIM(c_cactus_celebrate, 200),
        ANIM(c_cactus_dizzy,     150),
        ANIM(c_cactus_heart,     600),
    },
};
static const persona_t s_robot = {
    .name  = "Robot",
    .anims = {
        ANIM(c_robot_sleep,     700),
        ANIM(c_robot_idle,      500),
        ANIM(c_robot_busy,      200),
        ANIM(c_robot_attention, 250),
        ANIM(c_robot_celebrate, 200),
        ANIM(c_robot_dizzy,     150),
        ANIM(c_robot_heart,     600),
    },
};
static const persona_t s_alien = {
    .name  = "Alien",
    .anims = {
        ANIM(c_alien_sleep,     700),
        ANIM(c_alien_idle,      500),
        ANIM(c_alien_busy,      200),
        ANIM(c_alien_attention, 250),
        ANIM(c_alien_celebrate, 200),
        ANIM(c_alien_dizzy,     150),
        ANIM(c_alien_heart,     600),
    },
};
static const persona_t s_wizard = {
    .name  = "Wizard",
    .anims = {
        ANIM(c_wizard_sleep,     700),
        ANIM(c_wizard_idle,      500),
        ANIM(c_wizard_busy,      200),
        ANIM(c_wizard_attention, 250),
        ANIM(c_wizard_celebrate, 200),
        ANIM(c_wizard_dizzy,     150),
        ANIM(c_wizard_heart,     600),
    },
};
static const persona_t s_ninja = {
    .name  = "Ninja",
    .anims = {
        ANIM(c_ninja_sleep,     700),
        ANIM(c_ninja_idle,      500),
        ANIM(c_ninja_busy,      200),
        ANIM(c_ninja_attention, 250),
        ANIM(c_ninja_celebrate, 200),
        ANIM(c_ninja_dizzy,     150),
        ANIM(c_ninja_heart,     600),
    },
};
static const persona_t s_dragon = {
    .name  = "Dragon",
    .anims = {
        ANIM(c_dragon_sleep,     700),
        ANIM(c_dragon_idle,      500),
        ANIM(c_dragon_busy,      200),
        ANIM(c_dragon_attention, 250),
        ANIM(c_dragon_celebrate, 200),
        ANIM(c_dragon_dizzy,     150),
        ANIM(c_dragon_heart,     600),
    },
};
static const persona_t s_slime = {
    .name  = "Slime",
    .anims = {
        ANIM(c_slime_sleep,     700),
        ANIM(c_slime_idle,      500),
        ANIM(c_slime_busy,      200),
        ANIM(c_slime_attention, 250),
        ANIM(c_slime_celebrate, 200),
        ANIM(c_slime_dizzy,     150),
        ANIM(c_slime_heart,     600),
    },
};
static const persona_t s_sloth = {
    .name  = "Sloth",
    .anims = {
        ANIM(c_sloth_sleep,     700),
        ANIM(c_sloth_idle,      500),
        ANIM(c_sloth_busy,      200),
        ANIM(c_sloth_attention, 250),
        ANIM(c_sloth_celebrate, 200),
        ANIM(c_sloth_dizzy,     150),
        ANIM(c_sloth_heart,     600),
    },
};
static const persona_t s_dino = {
    .name  = "Dino",
    .anims = {
        ANIM(c_dino_sleep,     700),
        ANIM(c_dino_idle,      500),
        ANIM(c_dino_busy,      200),
        ANIM(c_dino_attention, 250),
        ANIM(c_dino_celebrate, 200),
        ANIM(c_dino_dizzy,     150),
        ANIM(c_dino_heart,     600),
    },
};

const persona_t * const g_personas[] = {
    &s_cat,     /* 0  */
    &s_dog,     /* 1  */
    &s_bear,    /* 2  */
    &s_fox,     /* 3  */
    &s_bunny,   /* 4  */
    &s_frog,    /* 5  */
    &s_owl,     /* 6  */
    &s_penguin, /* 7  */
    &s_ghost,   /* 8  */
    &s_cactus,  /* 9  */
    &s_robot,   /* 10 */
    &s_alien,   /* 11 */
    &s_wizard,  /* 12 */
    &s_ninja,   /* 13 */
    &s_dragon,  /* 14 */
    &s_slime,   /* 15 */
    &s_sloth,   /* 16 */
    &s_dino,    /* 17 */
};

const int g_persona_count = 18;
