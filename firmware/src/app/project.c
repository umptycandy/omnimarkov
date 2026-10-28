/* SPDX-License-Identifier: GPL-3.0-only */
/* OMNI project: one flash object (OBJ_PROJ). A project of another format, or none, gives the
 * defaults; formats 1 and 2 (0.1, 0.2) are brought forward. Part of the unity build, after app.h. */
project_t proj;

/* the factory chord buttons, left to right, in C: the primary chords on the first group of three,
 * their relative minors on the next two, then the dominants that lead into them */
static const uint8_t PAD_ROOT[NPADS] = {5, 0, 7, 2, 9, 4, 7, 4, 2, 10, 9};
static const uint8_t PAD_TYPE[NPADS] = {CH_MAJ, CH_MAJ, CH_MAJ, CH_MIN, CH_MIN, CH_MIN, CH_7, CH_7, CH_7, CH_MAJ,
                                        CH_7};

/* formats 1 and 2: sixteen parameters, the buttons, three flags */
#define P_NPARAMS_V2 16
typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS_V2];
    uint8_t pad_root[NPADS], pad_type[NPADS];
    uint8_t hold, sync, leds;
    uint8_t rsv[13];
} project_v2_t;

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = om_param_info(i)->def;
    for (i = 0; i < NPADS; i++) {
        proj.pad_root[i] = PAD_ROOT[i];
        proj.pad_type[i] = PAD_TYPE[i];
    }
    proj.leds = 1;
}

/* an older project: its values kept, the new ones at their defaults (the voices are the new ones) */
static void project_upgrade(const project_v2_t *o)
{
    int i;
    project_defaults();
    for (i = 0; i < P_NPARAMS_V2; i++)
        proj.par[i] = o->par[i];
    if (o->format == 1u)                         /* 0.1: that slot was Lo-fi (0 / 1) */
        proj.par[P_CREV] = om_param_info(P_CREV)->def;
    proj.par[P_SWING] = om_param_info(P_SWING)->def;   /* that slot was Auto bass (0 / 1) */
    memcpy(proj.pad_root, o->pad_root, NPADS);
    memcpy(proj.pad_type, o->pad_type, NPADS);
    proj.hold = o->hold;
    proj.sync = o->sync;
    proj.leds = o->leds;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT)
        return 0;
    for (i = 0; i < P_NPARAMS; i++) {
        const om_param_t *p = om_param_info(i);
        if (proj.par[i] < p->lo || proj.par[i] > p->hi)
            return 0;
    }
    for (i = 0; i < NPADS; i++)
        if (proj.pad_root[i] > 11u || proj.pad_type[i] >= CH_NTYPES)
            return 0;
    if (proj.loop_len > OM_LOOP_MAX)
        return 0;
    for (i = 0; i < OM_LOOP_MAX; i++) {          /* a chord code, a let-go, or nothing */
        uint8_t c = proj.loop[i];
        if ((c && c < 0x80u) || (c >= 0x80u + 12u * CH_NTYPES && c != 0xFFu))
            return 0;
    }
    return 1;
}

int project_load(void)
{
    static union {
        project_t p;
        project_v2_t v2;
    } raw;
    int n;
    memset(&raw, 0, sizeof raw);
    n = plat_store_load(OBJ_PROJ, &raw, sizeof raw);
    if (n >= 8 && raw.p.magic == PROJ_MAGIC && (raw.p.format == 1u || raw.p.format == 2u)) {
        project_upgrade(&raw.v2);
        if (project_valid())
            return 0;
    } else {
        proj = raw.p;
        if (n >= 8 && project_valid())
            return 0;
    }
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    int i;
    om_link(proj.loop, &proj.loop_len);
    for (i = 0; i < P_NPARAMS; i++)
        om_set(i, proj.par[i]);
}
