// Gathering Map: shows the quest's gathering spots (mining, bone, bug, herb, mushroom...) on the quest maps with the
// icon of the item they mostly give.
//
// The spots: sPop builds them when the quest loads, into a pool of POP_COUNT cPop. Some come from the map's point
// set (those of the quest's rank), the rest it rolls: a random spot of an area's candidates with a random lot. A pop
// in use has bit 0 of +0x70 set, its world position at +0x04, its area at +0x55, its class (2 mining, 3 bug) at
// +0x64, its model at +0x68, how it was made at +0x60 (4: not a gathering spot) and its item lot at +0x38 (entries of
// weight, quantity, item, ended by weight 0xFF). Its icon: ore for mining and bug for bug spots, else what its model
// looks like, else the icon of the item it most likely gives; colored like that item.
//
// The quest map draws its icons through cUIOQuestMapIcon, one per map view (the minimap and the full map). Each
// keeps a GUI instance of the map_icon layout per icon it may show; every frame it places each one with its map
// view's worldToMap and shows or hides it. The plugin does the same with instances of its own: when an icon object
// initializes it gets MAX_ICONS more instances, and after each of its updates those show the spots in the areas the
// view shows. Each plays the game's dropped-item sequence; the plugin then gives the instance's texture object the
// item icon itself (the common icon texture, the cut of the item icon, the item color), so no game file changes.
//
// cUIOQuestMapIcon: +0x24 its GUI, +0xCC the map view (cUIOMapBase: +0x94 map number; vtable +0x64 shows an area,
// +0x6C world position -> map position), +0xD0/+0xD4 what new icon instances are made from and attached to.
#include "forge.h"

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "dev"
#endif

typedef struct { u32 texture; float cut[4]; u32 icon; } IconCut;
#include "items.h"

#define ICON_INIT 0x61912C   // cUIOQuestMapIcon: makes its icon instances
#define ICON_UPDATE 0x61947C // cUIOQuestMapIcon: places them, every frame
#define GOT_ICON_INST_DTI 0x183EF78
#define GUI_NEW_INSTANCE 0x54501C
#define GUI_INSTANCE_FLAG 0x546F48
#define GUI_SET_VISIBLE 0x54533C
#define GUI_SET_X 0x545A40
#define GUI_SET_Y 0x545AC0
#define GUI_SET_PRIORITY 0x546E84
#define GUI_SET_SCALE 0x545FAC      // both axes; the game draws big monsters at 1.5
#define TEXOBJ_SET_TEXTURE 0xA4FAF0 // cGUIObjTexture: Texture, by the layout's texture id (+0x100)
#define TEXOBJ_SET_RECT 0xA4FB58    // cGUIObjTexture: mTextureRect (+0x10C)
#define TEXOBJ_SET_COLOR 0xA4E074   // cGUIObj2D: VertexColor, RGBA (+0xA4)
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

#define POP_POOL 0x24 // sPop: its cPop pool
#define POP_COUNT 128
#define POP_SIZE 0x80
#define POP_POS 0x04
#define POP_LOT 0x38
#define POP_AREA 0x55
#define POP_ORIGIN 0x60 // 1 point set, 2 rolled, 4 not a gathering spot
#define POP_CLASS 0x64
#define POP_MODEL 0x68
#define POP_FLAGS 0x70
#define ORIGIN_OTHER 4
#define CLASS_MINING 2
#define CLASS_BUG 3

#define ANIM_ICN 4
#define SEQ_DROPPED_ITEM 1000023 // em_otosimono
#define PRIORITY 50              // below the game's icons (traps 100, monsters 700+, hunters 900)
#define MAX_ICONS 96

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
typedef void (*SetTexture)(u32 obj, u32 texture);
typedef void (*SetRect)(u32 obj, const float *rect);
typedef void (*SetColor)(u32 obj, const u32 *rgba);

static ForgeHook g_init_hook, g_update_hook;
static bool g_hooked, g_enabled = true, g_show_other = false;
static u32 g_main;
static int g_map = -1, g_shown;
static int g_size = 35; // icon scale in percent

#define MAX_VIEWS 4
static struct { u32 obj, count, inst[MAX_ICONS]; } g_views[MAX_VIEWS];
static u32 g_next_view;

#define FN(type, off) ((type)(g_main + (off)))

// The icon texture object of a GUI instance (as the game's 0x546F48 finds it).
static u32 icon_object(u32 gui, u32 id) {
    u32 split = *(u32 *)(gui + GUI_INST_SPLIT);
    u32 inst = id < split ? ((u32 *)*(u32 *)(gui + 0xF8))[id] : ((u32 *)*(u32 *)(gui + 0x294))[id - split];
    u32 objs = inst ? *(u32 *)(inst + INST_OBJECTS) : 0;
    return objs ? ((u32 *)objs)[OBJ_ICON] : 0;
}

// The quest's gathering pops, or 0 if there are none.
static u32 pop_pool(void) {
    u32 pop = forge_singleton_getInstanceByName("sPop");
    return pop ? *(u32 *)(pop + POP_POOL) : 0;
}

// The kind of icon and item color a pop shows.
static void pop_icon(u32 pop, u32 *kind, u32 *color) {
    // The lot's items, by icon and color, with their summed weights.
    u8 icons[16], colors[16];
    u32 weights[16], n = 0;
    for (const u8 *e = *(const u8 **)(pop + POP_LOT); e && e[0] != 0xFF && n < 16; e += 4) {
        u32 item = e[2] | e[3] << 8, i;
        if (item >= ITEM_COUNT) continue;
        for (i = 0; i < n && (icons[i] != g_items[item][0] || colors[i] != g_items[item][1]); i++) {}
        if (i == n) { icons[n] = g_items[item][0]; colors[n] = g_items[item][1]; weights[n++] = 0; }
        weights[i] += e[0];
    }
    u32 top = 0;
    for (u32 i = 1; i < n; i++) if (weights[i] > weights[top]) top = i;
    u32 cls = *(u32 *)(pop + POP_CLASS), model = *(u32 *)(pop + POP_MODEL);
    u32 k = cls == CLASS_MINING ? KIND_ORE : cls == CLASS_BUG ? KIND_BUG : model < MODEL_COUNT ? g_model_kind[model] : KIND_OTHER;
    if (k == KIND_OTHER && n) k = g_icon_kind[icons[top]];
    // Colored like the likeliest item of that icon, else the likeliest item; gray for ore, bugs and bones without.
    int best = -1;
    for (u32 i = 0; i < n; i++)
        if (icons[i] == g_kinds[k].icon && (best < 0 || weights[i] > weights[best])) best = (int)i;
    *kind = k;
    *color = best >= 0 ? colors[best] : (k == KIND_ORE || k == KIND_BUG || k == KIND_BONE || !n) ? COLOR_GRAY : colors[top];
    if (*color >= COLOR_COUNT) *color = COLOR_GRAY;
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
    for (u32 k = 0; k < MAX_ICONS; k++) {
        u32 inst = FN(NewInstance, GUI_NEW_INSTANCE)(gui, *(u32 *)(obj + ICON_INST_SRC), ANIM_ICN,
                                                      *(u32 *)(obj + ICON_INST_PARENT), dti);
        FN(InstanceFlag, GUI_INSTANCE_FLAG)(gui, inst, 1, 3);
        FN(SetSequence, UIO_SET_SEQUENCE)(obj, inst, SEQ_DROPPED_ITEM);
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
    u32 used = 0, pool = pop_pool();
    if (g_enabled && view && pool) {
        u32 *vt = *(u32 **)view;
        g_map = (int)*(u32 *)(view + VIEW_MAP);
        for (u32 i = 0; i < POP_COUNT && used < count; i++) {
            u32 pop = pool + i * POP_SIZE;
            if (!(*(u8 *)(pop + POP_FLAGS) & 1) || *(u32 *)(pop + POP_ORIGIN) == ORIGIN_OTHER) continue;
            u32 area = *(u8 *)(pop + POP_AREA), kind, color;
            if (!((ShowsArea)vt[VIEW_SHOWS_AREA])(view, area)) continue;
            pop_icon(pop, &kind, &color);
            if (kind == KIND_OTHER && !g_show_other) continue;
            const float *at = (const float *)(pop + POP_POS);
            float pos[4] __attribute__((aligned(16))) = {at[0], at[1], at[2], 0};
            float on_map[4] __attribute__((aligned(16))) = {0};
            u32 id = inst[used++];
            FN(SetLayer, UIO_SET_LAYER)(obj, id, 1, 2, 5);
            u32 tex = icon_object(gui, id);
            if (tex) {
                const IconCut *k = &g_kinds[kind];
                float cut[4] __attribute__((aligned(16))) = {k->cut[0], k->cut[1], k->cut[2], k->cut[3]};
                FN(SetTexture, TEXOBJ_SET_TEXTURE)(tex, k->texture);
                FN(SetRect, TEXOBJ_SET_RECT)(tex, cut);
                FN(SetColor, TEXOBJ_SET_COLOR)(tex, &g_colors[color]);
            }
            ((WorldToMap)vt[VIEW_WORLD_TO_MAP])(view, pos, on_map, area);
            FN(SetFloat, GUI_SET_X)(gui, id, on_map[0]);
            FN(SetFloat, GUI_SET_Y)(gui, id, on_map[1]);
            FN(SetInt, GUI_SET_PRIORITY)(gui, id, PRIORITY);
            FN(SetFloat, GUI_SET_SCALE)(gui, id, g_size / 100.0f);
            FN(SetBool, GUI_SET_VISIBLE)(gui, id, 1);
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
