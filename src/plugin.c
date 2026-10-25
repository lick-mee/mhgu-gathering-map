// Gathering Map: shows the gathering points (mining, bone, bug, herb, mushroom...) on the quest maps with the icon of
// the item they mostly give.
//
// The quest map draws its icons through cUIOQuestMapIcon, one per map view (the minimap and the full map). Each
// keeps a GUI instance of the map_icon layout per icon it may show; every frame it gives each one a sequence of the
// layout's "icn" animation (which picks the texture cut and color), places it with its map view's worldToMap and
// shows or hides it. The plugin does the same with instances of its own: when an icon object initializes it gets
// MAX_MAP_POINTS more instances, and after each of its updates those show the gathering points of the current map
// in the areas the view shows. The sequences are the mod's own (FIRST_SEQUENCE...), added to map_icon by
// tools/make_data.py (the release's setup.bat), along with the icons themselves in free space of the map icon
// texture. Sequences give the color; the texture cut doesn't reach instances made this way, so the plugin sets it on
// the instance's texture object itself. The points (src/points.h) come from the maps' gathering-point sets.
//
// cUIOQuestMapIcon: +0x24 its GUI, +0xCC the map view (cUIOMapBase: +0x94 map number; vtable +0x64 shows an area,
// +0x6C world position -> map position), +0xD0/+0xD4 what new icon instances are made from and attached to.
#include "forge.h"

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "dev"
#endif

typedef struct { u8 map, area, kind, color; float pos[3]; } GatherPoint;
#include "points.h"

#define ICON_INIT 0x61912C   // cUIOQuestMapIcon: makes its icon instances
#define ICON_UPDATE 0x61947C // cUIOQuestMapIcon: places them, every frame
#define GOT_ICON_INST_DTI 0x183EF78
#define GUI_NEW_INSTANCE 0x54501C
#define GUI_INSTANCE_FLAG 0x546F48
#define GUI_SET_VISIBLE 0x54533C
#define GUI_SET_X 0x545A40
#define GUI_SET_Y 0x545AC0
#define GUI_SET_PRIORITY 0x546E84
#define TEXOBJ_SET_RECT 0xA4FB58 // cGUIObjTexture: mTextureRect (+0x10C)
#define GUI_SET_SCALE 0x545FAC // both axes; the game draws big monsters at 1.5
#define UIO_SET_SEQUENCE 0x56E538
#define UIO_SET_LAYER 0x56E6A8

#define GUI_INST_SPLIT 0x298 // instances below it in the array at +0xF8, the rest at +0x294
#define INST_OBJECTS 0xD8
#define OBJ_ICON 1 // the instance's objects: 0 icn root, 1 Tex_icn
#define ICON_GUI 0x24
#define ICON_VIEW 0xCC
#define ICON_INST_SRC 0xD0
#define ICON_INST_PARENT 0xD4
#define VIEW_MAP 0x94
#define VIEW_SHOWS_AREA (0x64 / 4)
#define VIEW_WORLD_TO_MAP (0x6C / 4)

#define ANIM_ICN 4
#define KIND_OTHER 0 // crates, barrels, ammo...: the game's dropped-item marker
#define PRIORITY 50 // below the game's icons (traps 100, monsters 700+, hunters 900)

void *memset(void *d, int c, u32 n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }

typedef void (*Method)(u32 self);
typedef u32 (*NewInstance)(u32 gui, u32 src, u32 anim, u32 parent, u32 dti);
typedef void (*InstanceFlag)(u32 gui, u32 inst, u32 a, u32 b);
typedef void (*SetBool)(u32 gui, u32 inst, u32 on);
typedef void (*SetFloat)(u32 gui, u32 inst, float v);
typedef void (*SetInt)(u32 gui, u32 inst, u32 v);
typedef void (*SetSequence)(u32 uio, u32 inst, u32 seq);
typedef void (*SetLayer)(u32 uio, u32 inst, u32 a, u32 b, u32 layer);
typedef bool (*ShowsArea)(u32 view, u32 area);
typedef void (*WorldToMap)(u32 view, const float *pos, float *out, u32 area);
typedef void (*SetRect)(u32 obj, const float *rect);

static ForgeHook g_init_hook, g_update_hook;
static bool g_hooked, g_enabled = true, g_show_other = false;
static u32 g_main;
static int g_map = -1, g_shown;
static int g_size = 35; // icon scale in percent

#define MAX_VIEWS 4
static struct { u32 obj, count, inst[MAX_MAP_POINTS]; } g_views[MAX_VIEWS];
static u32 g_next_view;

#define FN(type, off) ((type)(g_main + (off)))

// The icon texture object of a GUI instance (as the game's 0x546F48 finds it).
static u32 icon_object(u32 gui, u32 id) {
    u32 split = *(u32 *)(gui + GUI_INST_SPLIT);
    u32 inst = id < split ? ((u32 *)*(u32 *)(gui + 0xF8))[id] : ((u32 *)*(u32 *)(gui + 0x294))[id - split];
    u32 objs = inst ? *(u32 *)(inst + INST_OBJECTS) : 0;
    return objs ? ((u32 *)objs)[OBJ_ICON] : 0;
}

static u32 view_of(u32 obj) {
    u32 i = 0;
    while (i < MAX_VIEWS && g_views[i].obj != obj) i++;
    return i;
}

static void icon_init(u32 obj) {
    ((Method)g_init_hook.original)(obj);
    u32 i = view_of(obj);
    if (i == MAX_VIEWS) i = g_next_view++ % MAX_VIEWS; // a freed object's slot; its instances went with it
    g_views[i].obj = obj;
    g_views[i].count = 0;
    u32 gui = *(u32 *)(obj + ICON_GUI), dti = *(u32 *)(g_main + GOT_ICON_INST_DTI);
    for (u32 k = 0; k < MAX_MAP_POINTS; k++) {
        u32 inst = FN(NewInstance, GUI_NEW_INSTANCE)(gui, *(u32 *)(obj + ICON_INST_SRC), ANIM_ICN,
                                                      *(u32 *)(obj + ICON_INST_PARENT), dti);
        FN(InstanceFlag, GUI_INSTANCE_FLAG)(gui, inst, 1, 3);
        FN(SetBool, GUI_SET_VISIBLE)(gui, inst, 0);
        g_views[i].inst[g_views[i].count++] = inst;
    }
    forge_log(LOG_INFO, "gather-map: map icons %08X ready (%u instances)", obj, g_views[i].count);
}

static void icon_update(u32 obj) {
    ((Method)g_update_hook.original)(obj);
    u32 v = view_of(obj);
    if (v == MAX_VIEWS) return;
    u32 count = g_views[v].count, *inst = g_views[v].inst;
    u32 gui = *(u32 *)(obj + ICON_GUI), view = *(u32 *)(obj + ICON_VIEW);
    u32 used = 0;
    if (g_enabled && view) {
        u32 map = *(u32 *)(view + VIEW_MAP);
        if ((int)map != g_map) {
            forge_log(LOG_INFO, "gather-map: map %u, %u gathering points", map, map < MAP_COUNT ? g_map_points[map][1] : 0);
            g_map = (int)map;
        }
        if (map < MAP_COUNT) {
            u32 *vt = *(u32 **)view;
            const GatherPoint *p = &g_points[g_map_points[map][0]];
            for (u32 n = g_map_points[map][1]; n-- && used < count; p++) {
                if (p->kind == KIND_OTHER && !g_show_other) continue;
                if (!((ShowsArea)vt[VIEW_SHOWS_AREA])(view, p->area)) continue;
                float pos[4] __attribute__((aligned(16))) = {p->pos[0], p->pos[1], p->pos[2], 0};
                float at[4] __attribute__((aligned(16))) = {0};
                u32 id = inst[used++];
                FN(SetSequence, UIO_SET_SEQUENCE)(obj, id, FIRST_SEQUENCE + p->kind * COLOR_COUNT + p->color);
                FN(SetLayer, UIO_SET_LAYER)(obj, id, 1, 2, 5);
                u32 tex = icon_object(gui, id);
                if (tex) {
                    const float *r = g_kind_rects[p->kind];
                    float rect[4] __attribute__((aligned(16))) = {r[0], r[1], r[2], r[3]};
                    FN(SetRect, TEXOBJ_SET_RECT)(tex, rect);
                }
                ((WorldToMap)vt[VIEW_WORLD_TO_MAP])(view, pos, at, p->area);
                FN(SetFloat, GUI_SET_X)(gui, id, at[0]);
                FN(SetFloat, GUI_SET_Y)(gui, id, at[1]);
                FN(SetInt, GUI_SET_PRIORITY)(gui, id, PRIORITY);
                FN(SetFloat, GUI_SET_SCALE)(gui, id, g_size / 100.0f);
                FN(SetBool, GUI_SET_VISIBLE)(gui, id, 1);
            }
        }
    }
    for (u32 k = used; k < count; k++) FN(SetBool, GUI_SET_VISIBLE)(gui, inst[k], 0);
    g_shown = (int)used;
}

EXPORT void forge_onLoad(ForgePluginInfo *info) {
    static const char name[] = "Gathering Map " PLUGIN_VERSION;
    if (!info) return;
    info->major = 1; info->minor = 5; info->patch = 1;
    memset(info->name, 0, sizeof info->name);
    for (u32 i = 0; i < sizeof name; i++) info->name[i] = name[i];
}

EXPORT void forge_onInit(void) {
    u32 main = g_main = forge_mem_getMainTextAddr();
    u32 original = 0; // non-null asks Forge for a trampoline to the original
    forge_hook_create(&g_init_hook, main + ICON_INIT, (u32)icon_init, &original);
    forge_hook_create(&g_update_hook, main + ICON_UPDATE, (u32)icon_update, &original);
    g_hooked = g_init_hook.original && g_update_hook.original;
    forge_log(g_hooked ? LOG_INFO : LOG_WARN, "gather-map: %s", g_hooked ? "gathering points on the map" : "hook FAILED, mod inactive");
}

EXPORT void forge_onImGuiInit(void *ctx, void *alloc, void *freefn) {
    if (!ctx || !alloc || !freefn) return;
    igSetCurrentContext(ctx);
    igSetAllocatorFunctions(alloc, freefn, NULL);
}

EXPORT void forge_onImGuiRender(void) {
    if (!igTreeNode_Str("Gathering Map###gather-map")) return;
    if (!g_hooked) igTextUnformatted("Hook failed: the mod is inactive (see log.txt).", NULL);
    igCheckbox("Show gathering points on the map", &g_enabled);
    igCheckbox("Also show other pickups (crates, barrels, ammo: the ! marker)", &g_show_other);
    igSliderInt("Icon size (%)", &g_size, 20, 150, "%d", 0);
    static char line[48];
    const char *s = "Map: not on a quest";
    if (g_map >= 0) {
        char *q = line;
        for (const char *t = "Map "; *t; t++) *q++ = *t;
        *q++ = '0' + g_map / 10 % 10; *q++ = '0' + g_map % 10;
        for (const char *t = ", icons shown: "; *t; t++) *q++ = *t;
        if (g_shown >= 10) *q++ = '0' + g_shown / 10 % 10;
        *q++ = '0' + g_shown % 10;
        *q = 0;
        s = line;
    }
    igTextUnformatted(s, NULL);
    igTreePop();
}
