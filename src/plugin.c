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
//
// Without a map the whole map (cUIOWholeMap, its update at 0x61BAA0) shows only the part around the hunter: unless
// the pouch holds a Map (item 1715) or the hunter has the skill effect 0xCE, it puts a texture object (instance +0x9C,
// object +0xA4) at the hunter's map position - 64 and cuts it to the map's 128 x 128 square around him (the cut is
// offset by 128, so the hunter is at cut x0 - 64, y0 - 64). The plugin then shows only the spots in that square.
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

#define WHOLE_SHOWS_AREA 0x61BDA8 // cUIOWholeMap's vtable +0x64: tells the whole map from the area minimap
#define WHOLE_FOG_INST 0x9C
#define WHOLE_FOG_OBJ 0xA4
#define TEXOBJ_RECT 0x10C
#define FOG_HALF 64.0f   // half the cut: the hunter is at cut x0 - 64
#define FADE_START 0.55f // points inside this part of the radius are fully shown
#define GOT_POUCH 0x183A828   // the hunter's pouch
#define POUCH_COUNT 0x19C21C  // (pouch, item) -> how many
#define ITEM_MAP 1715
#define GOT_SPLAYER 0x1838D80
#define GET_MY_PLAYER 0x276278 // (sPlayer) -> the local hunter
#define PLAYER_HAS 0x2864C4    // (hunter, skill effect) -> nonzero if active
#define EFFECT_MAP 0xCE

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
typedef int (*PouchCount)(u32 pouch, u32 item);
typedef u32 (*GetMyPlayer)(u32 splayer);
typedef u32 (*PlayerHas)(u32 player, u32 effect);

static ForgeHook g_init_hook, g_update_hook;
static bool g_hooked, g_enabled = true, g_show_other = false, g_follow_map = true;
static u32 g_main;
static int g_map = -1, g_shown;
static int g_size = 35; // icon scale in percent
static int g_radius = 40; // without a map: radius (map units) of the circle points show in

// Settings: gather_map.ini in the game's save storage. Defaults until the first change; read once the storage
// mounts (it can't before the game sets up its save), written on every change.
#define SETTINGS_FILE "gather_map.ini"
static bool g_settings_read;
static u32 g_settings_wait; // frames until the next read attempt

static u32 make_path(char *out, u32 max) {
    const char *m = forge_fs_getSaveDataMountPoint();
    u32 n = 0;
    for (const char *s = m ? m : "savedata"; *s && n < max - 1; s++) out[n++] = *s;
    for (const char *s = ":/" SETTINGS_FILE; *s && n < max - 1; s++) out[n++] = *s;
    out[n] = 0;
    return n;
}

int memcmp(const void *a, const void *b, u32 n) {
    const u8 *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
    return 0;
}

static int parse_int(const char *s, const char *end) {
    int v = 0;
    while (s < end && *s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

static void settings_parse(const char *s, const char *end) {
    while (s < end) {
        const char *eol = s, *eq = 0;
        while (eol < end && *eol != '\n') { if (*eol == '=' && !eq) eq = eol; eol++; }
        if (eq) {
            u32 k = (u32)(eq - s);
            int v = parse_int(eq + 1, eol);
            #define KEY(name) (k == sizeof name - 1 && !memcmp(s, name, k))
            if (KEY("enabled")) g_enabled = v != 0;
            else if (KEY("show_other")) g_show_other = v != 0;
            else if (KEY("follow_map")) g_follow_map = v != 0;
            else if (KEY("radius") && v >= 10 && v <= 90) g_radius = v;
            else if (KEY("icon_size") && v >= 20 && v <= 150) g_size = v;
            #undef KEY
        }
        s = eol + 1;
    }
}

// Reads the settings once; true when done (read, or there is no file yet).
static bool settings_read(void) {
    if (g_settings_read) return true;
    if (g_settings_wait) { g_settings_wait--; return false; }
    if (!forge_fs_mountSaveData()) { g_settings_wait = 120; return false; }
    char path[64], buf[256];
    make_path(path, sizeof path);
    if (forge_fs_fileExists(path)) {
        FsFile f;
        s64 size = 0;
        if (!nnfs_OpenFile(&f, path, FS_READ)) {
            if (!nnfs_GetFileSize(&size, f) && size > 0) {
                u32 n = size < (s64)sizeof buf ? (u32)size : sizeof buf;
                if (!nnfs_ReadFile(f, 0, buf, n)) settings_parse(buf, buf + n);
            }
            nnfs_CloseFile(f);
        }
        forge_log(LOG_INFO, "gather-map: settings read from %s", path);
    }
    forge_fs_unmountSaveData();
    g_settings_read = true;
    return true;
}

static char *put_line(char *q, const char *key, int v) {
    while (*key) *q++ = *key++;
    *q++ = '=';
    char digits[8];
    int n = 0;
    do digits[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 8);
    while (n) *q++ = digits[--n];
    *q++ = '\n';
    return q;
}

static void settings_write(void) {
    if (!forge_fs_mountSaveData()) { forge_log(LOG_WARN, "gather-map: save storage unavailable, settings not saved"); return; }
    char path[64], buf[128], *q = buf;
    make_path(path, sizeof path);
    q = put_line(q, "enabled", g_enabled);
    q = put_line(q, "show_other", g_show_other);
    q = put_line(q, "follow_map", g_follow_map);
    q = put_line(q, "radius", g_radius);
    q = put_line(q, "icon_size", g_size);
    u32 n = (u32)(q - buf);
    bool ok = false;
    FsFile f;
    if (forge_fs_fileExists(path) || !nnfs_CreateFile(path, 0)) {
        if (!nnfs_OpenFile(&f, path, FS_WRITE | FS_APPEND)) {
            FsWriteOption opt = {1};
            ok = !nnfs_SetFileSize(f, n) && !nnfs_WriteFile(f, 0, buf, n, &opt);
            nnfs_CloseFile(f);
        }
    }
    if (ok) ok = !nnfs_CommitSaveData(forge_fs_getSaveDataMountPoint() ? forge_fs_getSaveDataMountPoint() : "savedata");
    forge_fs_unmountSaveData();
    g_settings_read = true; // what was chosen now wins over an older file
    forge_log(ok ? LOG_INFO : LOG_WARN, "gather-map: settings %s", ok ? "saved" : "could not be saved");
}

#define MAX_VIEWS 4
static struct { u32 obj, count, inst[MAX_ICONS]; } g_views[MAX_VIEWS];
static u32 g_next_view;

#define FN(type, off) ((type)(g_main + (off)))

// Object n of a GUI instance (as the game's 0x546F48 finds it).
static u32 inst_object(u32 gui, u32 id, u32 n) {
    u32 split = *(u32 *)(gui + GUI_INST_SPLIT);
    u32 inst = id < split ? ((u32 *)*(u32 *)(gui + 0xF8))[id] : ((u32 *)*(u32 *)(gui + 0x294))[id - split];
    u32 objs = inst ? *(u32 *)(inst + INST_OBJECTS) : 0;
    return objs ? ((u32 *)objs)[n] : 0;
}

static u32 icon_object(u32 gui, u32 id) { return inst_object(gui, id, OBJ_ICON); }

// Whether the hunter sees the whole map: a Map in the pouch or the map skill effect (as cUIOWholeMap decides).
static bool has_map(void) {
    u32 *pouch = *(u32 **)(g_main + GOT_POUCH);
    if (pouch && *pouch && FN(PouchCount, POUCH_COUNT)(*pouch, ITEM_MAP) > 0) return true;
    u32 *splayer = *(u32 **)(g_main + GOT_SPLAYER);
    u32 player = splayer && *splayer ? FN(GetMyPlayer, GET_MY_PLAYER)(*splayer) : 0;
    return !player || FN(PlayerHas, PLAYER_HAS)(player, EFFECT_MAP) != 0;
}

// For the whole map without a map: the map position of the hunter, the middle of the part it shows.
static bool fog_center(u32 view, float *x, float *y) {
    u32 *vt = *(u32 **)view;
    if (vt[VIEW_SHOWS_AREA] != g_main + WHOLE_SHOWS_AREA || has_map()) return false;
    u32 obj = inst_object(*(u32 *)(view + ICON_GUI), *(u32 *)(view + WHOLE_FOG_INST), *(u32 *)(view + WHOLE_FOG_OBJ));
    if (!obj) return false;
    const float *cut = (const float *)(obj + TEXOBJ_RECT);
    *x = cut[0] - FOG_HALF;
    *y = cut[1] - FOG_HALF;
    return true;
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
    g_settings_wait = 0;
    settings_read(); // a quest is loading: the save is set up by now
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
        float fx = 0, fy = 0;
        bool fog = g_follow_map && fog_center(view, &fx, &fy);
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
            ((WorldToMap)vt[VIEW_WORLD_TO_MAP])(view, pos, on_map, area);
            // Fades out towards the edge of the circle that shows the map: fully seen inside FADE_START of the radius.
            u32 alpha = 255;
            if (fog) {
                float dx = on_map[0] - fx, dy = on_map[1] - fy, d2 = dx * dx + dy * dy;
                float r = (float)g_radius, r0 = r * FADE_START;
                if (d2 >= r * r) continue;
                if (d2 > r0 * r0) {
                    float t = (r * r - d2) / (r * r - r0 * r0);
                    alpha = (u32)(t * t * 255.0f);
                    if (alpha < 8) continue;
                }
            }
            u32 id = inst[used++];
            FN(SetLayer, UIO_SET_LAYER)(obj, id, 1, 2, 5);
            u32 tex = icon_object(gui, id);
            if (tex) {
                const IconCut *k = &g_kinds[kind];
                float cut[4] __attribute__((aligned(16))) = {k->cut[0], k->cut[1], k->cut[2], k->cut[3]};
                FN(SetTexture, TEXOBJ_SET_TEXTURE)(tex, k->texture);
                FN(SetRect, TEXOBJ_SET_RECT)(tex, cut);
                u32 rgba = (g_colors[color] & 0x00FFFFFF) | alpha << 24;
                FN(SetColor, TEXOBJ_SET_COLOR)(tex, &rgba);
            }
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
    settings_read();
    bool changed = igCheckbox("Show gathering points on the map", &g_enabled);
    changed |= igCheckbox("Also show other pickups (crates, barrels, ammo: the ! marker)", &g_show_other);
    changed |= igCheckbox("Without a Map item, only show points in the part of the map you can see", &g_follow_map);
    igSliderInt("Visible radius without a map", &g_radius, 10, 90, "%d", 0);
    changed |= igIsItemDeactivatedAfterEdit();
    igSliderInt("Icon size (%)", &g_size, 20, 150, "%d", 0);
    changed |= igIsItemDeactivatedAfterEdit();
    if (changed) settings_write();
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
