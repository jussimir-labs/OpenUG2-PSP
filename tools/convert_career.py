#!/usr/bin/env python3
"""Career data from the Xbox NFSU2 GLOBALB.BUN -> career.n2cr.

Bin section 0x34A11 holds the career races (struct CareerRace, 0x88 bytes,
layout from the nfsu2-re project, structs.html): race type at +0x0F
(RACE_TYPE: 0 circuit, 1 sprint, 2 Street X, 3 URL, 4 drag, 5 drift), up
to four (u16 event id, u8 ?, u8 laps) entries at +0x18 (count at +0x7E),
the money won at +0x30 ("bankToWinFromRace"), the reward kind at +0x34 and
the career stage at +0x37. Tournaments list several events.

Each event the PSP build can race (races.n2rc) has a row per stage whose
career races list it, from the first such race; the career order is the
stage, then the record's place in the table. Events in no career race get
the last stage and its median money, flagged as approximated.

Events come from the city (races.n2rc) and the closed tracks (drag, drift,
Street X and URL: races-L4R?.n2rc). A career race listing several events
is a tournament (the URL ones): each, in career order, groups those of its
events no earlier one took; they are raced one after another from the
first, the others hidden from the map (flag 2).

The stages (0x34A18, struct CareerStageSettings): the five sponsors on
offer from stage 2 (0x34A19, CareerSponsor: name in the career string
pool 0x34A1D, purse per sponsor race, signing bonus, the average
reputation per race the previous stage needs, the three race types of
the sponsor's races at +4), the outrun stakes. The contract terms are the
game's own texts (CONTRACT TERMS: "Driver must win 5 URL races, 19 races
- driver's choice, 3 races - sponsor's choice ... featured on 2 DVD
covers"): per stage 3/5/7/9 URL wins, 9/19/29/34 wins of choice and 1-4
DVD covers. The original races its tracks many times over a stage (35 to
69 career races); here each event comes once per stage it is raced in
(an event of stages 3 and 5 has a row in each), the wins of choice are
the original's (repeat wins count), and the URL wins are capped by the
stage's URL events.
Magazine covers and DVDs (0x34A16, Bin34A16: per stage SHOW_n_PICTURE and
DVD_n, the DVDs with the visual rating they need at +0x3C). SMS (0x34A17,
SmsData: name in the string pool, type, mailbox, stage in the high half
of the data word for the unlock messages, sender label; subject and body
the language table's <name>_SUBJECT and <name>). Shops (0x34A12,
CareerShop) retain their legacy animated-door instance positions. Entry
uses the original PathsFreeRoam TrackPathZone polygons in shopzones.n2sz
(N2SZ v1, generated alongside career.n2cr by convert_shopzones.py). The
0x37090 record is an animation instance, not a trigger box.

career.n2cr: 'N2CA', u32 version 4, u32 count, count x 12 bytes:
u16 event id, u8 stage, u8 race type, i32 money, u8 reward kind,
u8 laps (0 = the course's own), u8 flags (1 = money approximated, 2 = a
tournament's later course, 4 = on a closed track, 8 = a sponsor race),
u8 tournament group (0 = none). Entries are in career order. Then:
  u32 6, 6 x 16 bytes stages: u8 stage, u8 sponsors, u16 outrun stakes,
    u8 sponsor index x5, u8 URL wins, wins of choice, DVD covers, magazine
    covers, events to finish (stages 0-1, no contract), u8 pad x2;
  u32 n, n x 26 bytes sponsors: char name[16], u16 purse per race, u16
    signing bonus, u16 average reputation needed, u8 race type x3
    (RMODE_ of psp-viewer/race.h), u8 pad;
  u32 n, n x 4 bytes covers: u8 stage, u8 kind (0 magazine, 1 DVD), u8
    visual rating x10 needed, u8 Rachel's line for it (1 + the bit of the
    UG2_SPEECH Career_Magazine / Career_Dvd parameter, 0 none);
  u32 n, n x 20 bytes SMS: u8 type, u8 stage, u16 condition (races won
    in the stage), i32 money, u32 sender, subject, body (offsets in the
    text block);
  u32 n, n x 40 bytes shops: char name[24], u8 kind (0 car lot, 1
    performance, 2 body, 3 graphics, 4 car specialties, 5 crib), u8 stage,
    u8 hidden, u8 pad, f32 GU x y z;
  u32 n, n x 208 bytes showcases (version 7): u8 stage, u8 kind (0 a
    magazine's photo shoot, 2 a unique part's, 3 a DVD cover's: after the
    others), u8 the stage's wins it
    opens at, u8 the kind of unique part it opens (tools/convert_rating.py's
    kinds; 0 none), i16 its magazine cover (the covers' table, -1 none),
    u16 the performance categories it opens (bit n: 0x34A15's category n;
    a STAGE_n_PERF_k's), f32 GU x z (the
    city's marker of type 0xd), char name[24] (STAGE_2_SHOW_1), char
    magazine[8] (its masthead's prefix, 7 letters at most: BB, SCNORWA;
    empty none), its 3 cameras x 7 f32 (show_cameras: degrees around the
    car from its front, degrees up, metres away, the point looked at in
    metres ahead, to the side and up of the car, the lens in degrees; 0s
    none), a studio cover's pose x 10 f32 (studio_covers: the car's turn,
    the wheels' steering, the lens, the eye, the point looked at, the roll;
    0s none), f32 GU x z of the place the countdown leads to (show_target),
    u16 the countdown's seconds (+16), u8 Rachel's texts for it, u8 pad,
    u32 the texts x 5 (ENGAGE_SHOWCASE_GENERIC_n_BODY... with a %s for the
    time; offsets in the text block), u32 the dialog's subject;
  (version 9) u32 count, the rows' records; u32 6, the stages' rules;
  (version 10) u32 count, count x 12 bytes per row: u8 x5 how each rival's
    performance is set (0 a fixed rating, 1 matched to the player's car,
    2 the preset's, 3 the stage's ramp), i8 x5 the fixed rating (0..10),
    u8 pad x2; u32 6, 6 x 12 bytes the stages' ramps: i16 races, u8 pad
    x2, f32 the rating at the start, at the end;
  (version 11) u32 n, n x 56 bytes the career's preset cars (presets/rivals:
    char pack[24] opp-NAME, char model[16] its car, char neon[16] its
    underglow by the garage's name, "" none); u32 count, count x 8 bytes per row: u8 x5 each rival's
    preset (0xff: a RANDOM car), u8 pad x3;
  (version 12) u32 count, count x 16 bytes per row: f32 GU x y z of its
    record's trigger zone (race_zones: where the street engages it), u32
    the zone's name hash (0: none, the course's own start stands in);
  (version 13) u32 n, n x 12 bytes per sponsor: u32 its contract's terms,
    its races (the Sponsorship screen's SPONSOR_DETAILS_<NAME> and
    SPONSOR_INFO_<NAME>), its logo's texture name (DECAL_ICON_<NAME>);
    offsets in the text block, "" none;
  (version 14) the event dialog's texts: u32 n, n x u32 the bodies (offsets
    in the text block), u32 rows, rows x 16 bytes: u32 its subject, its type
    line, its sender (offsets), u16 its first body and how many to draw from;
  (version 15) u32 n, n x 4 bytes per SMS: u8 its mailbox (1 Inbox, 2 Game
    Tips, 3 Special Events, 4 Rachel, 5 Unlocks), u8 1 if it opens by
    itself in the free roam (the original's autopopup), u8 its envelope's
    colour (SMSGetEnvelopeImageHashAndColour's category), u8 pad;
  (version 16) u32 n, n x 8 bytes per showcase (the showcases' order): f32
    GU y of its star and of the place its countdown leads to (the trigger
    zones' heights, race_zones; a shop's, its marker's; NaN unknown): the
    GPS's target is 3-D (UnlockedTriggerZone::GetGPSPosition,
    PathfindingNode::CloseEnoughToGoal within 4 m of height);
  u32 size, the text block (zero-terminated strings).
"""
import argparse
import collections
import json
from pathlib import Path
import statistics
import struct
import sys

from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
TYPES = ['circuito', 'sprint', 'street x', 'url', 'drag', 'drift']


def career_races(globalb):
    blob = [globalb[p:p + n] for t, p, n in chunks(globalb) if t == 0x34A11]
    if len(blob) != 1 or len(blob[0]) % 0x88:
        raise ValueError('GLOBALB career race table (0x34A11) mismatch')
    pool = sections(globalb)[0x34a1d]
    out = []
    for k in range(len(blob[0]) // 0x88):
        r = blob[0][k * 0x88:(k + 1) * 0x88]
        n = min(r[0x7E], 4)
        events = [struct.unpack_from('<HBB', r, 0x18 + 4 * i) for i in range(n)]
        # the rest of the record (GameCube Speed.elf: CareerEventData,
        # EventPile, RuntimeCareerStageInfo::CanDoURL, RaceStarter::
        # AddCareerEventAIOpponents): its name's hash at +0x08, the
        # requirement's kind at +0x0C (3: the stage's world races won, the
        # count in the high half of +0x10; 4: the stage's final, +0x10
        # packs the world, sponsor and URL races won it needs in its bytes
        # 1, 2 and 3; 0: none, or the hash at +0x10 of the race won first),
        # an SUV race at +0x0D, its trigger zone's name hash at +0x28
        # (CareerEventData: the TrackPathZone the street engages it by,
        # PlayerCareerState::RecalcUnlockedZones), the rivals at +0x40 (12 bytes each: their
        # name in the career's string pool, how their performance is set
        # at +2 and its rating at +3, its car at +4: the hash of RANDOM or
        # of a preset's name), how many at +0x7C, hidden
        # until found at +0x7F
        rivals = min(r[0x7C], 5)
        name = struct.unpack_from('<H', r, 0)[0]
        out.append(dict(index=k, type=r[0x0F], money=struct.unpack_from('<i', r, 0x30)[0], reward=r[0x34],
                        stage=r[0x37], events=[(e, laps) for e, _, laps in events],
                        name=pool[name:pool.index(b'\0', name)].decode('latin-1'),
                        hash=struct.unpack_from('<I', r, 0x08)[0], req=r[0x0C], need=struct.unpack_from('<I', r, 0x10)[0],
                        suv=int(r[0x0D] == 1), hidden=int(r[0x7F] != 0), rivals=rivals, zone=struct.unpack_from('<I', r, 0x28)[0],
                        sender=struct.unpack_from('<I', r, 0x84)[0],   # the event dialog's From (0: its first rival's name)
                        rival_names=[struct.unpack_from('<H', r, 0x40 + 12 * i)[0] for i in range(rivals)],
                        rival_perf=[(r[0x42 + 12 * i], struct.unpack_from('<b', r, 0x43 + 12 * i)[0]) for i in range(rivals)],
                        rival_cars=[struct.unpack_from('<I', r, 0x44 + 12 * i)[0] for i in range(rivals)]))
    return out


def read_event_ids(path):
    d = Path(path).read_bytes()
    count = struct.unpack_from('<I', d, 8)[0]
    at, ids = 12, []
    for _ in range(count):
        eid, circ, hint, npts, ngates, length = struct.unpack_from('<HBBHHf', d, at)
        ids.append(eid)
        at += 12 + 12 * npts + 2 * ngates
        at += (-at) % 4
    return ids


def build(races, ids, track_ids=()):
    """One row per career race of the original (0x34A11) whose courses the
    PSP build has, in career order (the stage, then the record's place in
    the table): the game's own list, each race once (the same course can
    come back in several races of a stage, a sponsor's and the world's).
    A tournament (a record listing several courses, the URL ones) is raced
    from its row, its later courses following it (flag 2, not on the map).
    Left out: DDAY_EVENT_A/B (the prologue's shop visits, no course)."""
    stage_money = collections.defaultdict(list)
    for r in races:
        if r['money'] > 0:
            stage_money[r['stage']].append(r['money'])
    rows, group = [], 0
    for r in sorted(races, key=lambda r: (r['stage'], r['index'])):
        if not r['events'] or any(e not in ids for e, _ in r['events']):
            continue
        g = 0
        if len(r['events']) > 1 and group < 255:
            group += 1
            g = group
        money, approx = r['money'], 0
        if money <= 0:   # -1: "no money" in the record; the stage's median
            money, approx = int(statistics.median(stage_money[r['stage']] or [1000])), 1
        for k, (eid, laps) in enumerate(r['events']):
            rows.append(dict(id=eid, stage=r['stage'], type=r['type'], money=money, reward=r['reward'], laps=laps,
                             flags=approx | (2 if k else 0) | (4 if eid in track_ids else 0), group=g, race=r['index'],
                             record=r))
    return rows


# The event dialog (RaceEngageDialog::Setup, CareerEventData::
# GetEngageDialogBody): the record's race type (+0x0F) to its body texts'
# kind, how many ENGAGE_<kind>_RANDOM_n each has (the game's bRandom
# ranges), the subject's label ((world '', sponsor, URL), type) and the type
# line's
ENGAGE_KIND = {0: 'CIRCUIT', 1: 'SPRINT', 2: 'STREET', 4: 'DRAG', 5: 'DRIFT'}
ENGAGE_RANDOM = {'SUV': 9, 'SPON_STREET': 10, 'SPON_CIRCUIT': 11, 'SPON_SPRINT': 10, 'SPON_DRAG': 8, 'SPON_DRIFT': 11, 'URL': 12,
                 'STREET': 14, 'CIRCUIT': 18, 'SPRINT': 15, 'DRAG': 12, 'DRIFT': 13}
ENGAGE_SUBJECT = {('', 0): 0x9b39c122, ('', 1): 0x9ec5f82f, ('', 2): 0x57f833ad, ('', 4): 0xe45a7387, ('', 5): 0x3a10a7c2,
                  ('SPON', 0): 0xf037a7b, ('SPON', 1): 0x7216a68, ('SPON', 2): 0xc5f7902c, ('SPON', 4): 0x3d6a70e6, ('SPON', 5): 0xb5205101,
                  **{('URL', t): 0x80b8267c for t in range(6)}}
ENGAGE_TYPE = {0: 0x268d063c, 1: 0xba613729, 2: 0xfda6758, 4: 0x40c91ba7, 5: 0x59ecb2c2}
SPONSOR_TYPE = {1: 0, 2: 5, 3: 4, 4: 1, 5: 2}      # contract race type -> RMODE_ (circuit, drift, drag, sprint, Street X)
SHOP_KIND = {'CARLOT01': 0, 'PERFSHOP01': 1, 'PARTSSHOP01': 2, 'PAINTSHOP01': 3, 'AUDIOSHOP01': 4, 'CRIB01': 5}


def sections(globalb):
    out = {}
    for t, p, n in chunks(globalb):
        if t & 0x80000000 and (t & 0xffffff00) == 0x80034a00:
            for tt, pp, nn in chunks(globalb, p, p + n):
                out[tt] = globalb[pp:pp + nn]
    return out


# Rachel's texts as a showcase is engaged (Xbox default.xbe 0x1219b0, by
# the showcase's +20: 1 a visual unique part's, 2 a performance one's, 4 a
# magazine's; one drawn at random) and the dialog's subject (0x121ac0)
ENGAGE = {1: ('ENGAGE_VISUAL_GENERIC', 3, 0x48157d57), 2: ('ENGAGE_PERF_GENERIC', 3, 0xa517fd7a),
          4: ('ENGAGE_SHOWCASE_GENERIC', 5, 0xe5b2c887)}   # (rand() % 3 or 5: the visual ones' 4 and 5 unused)


def show_target(sec, h, where, shop_at, heights):
    """Where a showcase's countdown leads (default.xbe 0x13f620, 0x121820):
    its +0xc is a shop's name hash (0x34A12's +0x38: a unique part's, the
    shop's place) or its magazine cover's (0x34A16's +0x20, the name's), and
    the cover's +0x2c the city marker of type 0xd where the photographer
    waits; GU x y z (heights: the zones' by name hash, race_zones; NaN
    unknown), None if not found."""
    if h in shop_at:
        return shop_at[h]
    for k in range(len(sec[0x34a16]) // 64):
        name, _, _, at = struct.unpack_from('<4I', sec[0x34a16], 64 * k + 0x20)
        if name == h and at in where:
            return zone_at(where, heights, at)
    return None


def zone_at(where, heights, h):
    """A type 0xd marker's GU x y z: its square's centre (markers) and its
    zone's height (race_zones' +0x14); NaN when the zone is not found."""
    x, z = where[h]
    return (x, heights[h][1] if h in heights else float('nan'), z)


PERF_SHOWCASE = {1: 3, 2: 7, 3: 5, 4: 2, 5: 1, 6: 0, 7: 8, 8: 6, 9: 4, 10: 9}   # a showcase's byte: 0x34A15's category (0x112f30)


def sponsor_icon(key):
    """A sponsor's logo (uiSponsorsMain::Setup): DECAL_ICON_<NAME>, its
    texture's name cut at 23 characters (DECAL_ICON_NITROUS_EXPR)."""
    return ('DECAL_ICON_' + key)[:23]


def sponsor_keys(globalb):
    """The sponsors' names in the career string pool (STREETGLOW...,
    CareerSponsor 0x34A19), in table order."""
    sec = sections(globalb)
    pool = sec[0x34a1d]
    return [pool[o:pool.index(b'\0', o)].decode('latin-1')
            for o in (struct.unpack_from('<h', sec[0x34a19], 16 * k)[0] for k in range(len(sec[0x34a19]) // 16))]


def career_world(game, globalb, rows, races, language=None, written=None):
    """Stages, sponsors, covers, SMS and shops (see the module doc). With a
    language: its texts where the English has one (the same records and
    choices, career_translations); written: every text written, in order."""
    from convert_hud import strings
    from n2formats import game_hash
    sec = sections(globalb)
    pool = sec[0x34a1d]
    pooled = lambda o: pool[o:pool.index(b'\0', o)].decode('latin-1')
    lang = strings(game)
    if language:
        other = strings(game, language)
        lang = {h: other.get(h) or t if t else t for h, t in lang.items()}
    text, offsets = bytearray(), {}
    def put(t):
        t = (t or '').replace('\u2019', "'")
        if written is not None:
            written.append(t)
        if t not in offsets:
            offsets[t] = len(text)
            text.extend(t.encode('latin-1', 'replace') + b'\0')
        return offsets[t]
    sponsors, index = [], {}
    for k in range(len(sec[0x34a19]) // 16):
        off, purse, t0, t1, t2, _, h, bonus, rep = struct.unpack_from('<hhBBBBIhh', sec[0x34a19], 16 * k)
        name = lang.get(game_hash('SPONSOR_' + pooled(off)), pooled(off))
        index[h] = k
        sponsors.append(dict(name=name, key=pooled(off), purse=purse, bonus=bonus, rep=rep, types=[SPONSOR_TYPE.get(v, 0) for v in (t0, t1, t2)]))
    covers = []
    # Rachel's line for each cover: the game's tables of the cover's name
    # hash (+0x28 of the record) and the line's bit (Xbox default.xbe,
    # 0x3a3940 the magazines, 0x3a39e0 the DVDs; 0x11da10, 0x11da30)
    said = {0x9e101f88: 0x10, 0x601410dc: 0x80, 0x601417c4: 0x4, 0x9e102056: 0x40000, 0x9e10211b: 0x40,
            0x9e102177: 0x1000000, 0x9e1021d0: 0x8000, 0x72eca090: 0x8000, 0x601462b5: 0x1, 0x601417c3: 0x20,
            0x6014211a: 0x8, 0x9e102097: 0x1000, 0x9e1021dd: 0x200000, 0x60145e23: 0x2000, 0x60145e47: 0x400000,
            0x9e1021e0: 0x800, 0x60142b3b: 0x100000, 0xb5cbefb2: 0x800000, 0x9e1021e1: 0x2000000, 0x60145eb8: 0x4000,
            0x9e101f9e: 0x1, 0x9e101fb3: 0x8, 0x60145c24: 0x100000, 0x629fedba: 0x20, 0x601466bc: 0x4,
            0x62968b8e: 0x40, 0x6298f309: 0x2, 0x629cdefc: 0x200, 0x60145be1: 0x10, 0x601459f4: 0x80}
    for k in range(len(sec[0x34a16]) // 64):
        r = sec[0x34a16][64 * k:64 * k + 64]
        name = r[:32].split(b'\0')[0].decode()
        stage = int(name.split('_')[1])
        dvd = '_DVD_' in name
        rating = struct.unpack_from('<f', r, 0x3c)[0] if dvd else 0.0
        line = said.get(struct.unpack_from('<I', r, 0x28)[0], 0x1 if dvd else 0x10)   # the game's defaults
        covers.append(dict(name=name, stage=stage, dvd=dvd, rating=round(rating * 10), speech=line.bit_length()))
    # the stages' rules (0x34A18, CareerStageSettings, 0x50 bytes; the
    # GameCube's EventPile and RuntimeCareerStageInfo): +0x04 the cash per
    # star of a showcase's photo, +0x28 the hash of the stage's final race
    # (won, with the stage's DVD covers shot, the stage is done; none: every
    # race of the stage won), +0x30..+0x34 how many races of each kind the
    # map offers at once (circuit, drag, Street X, drift, sprint), +0x37 the
    # SUV races' (with an SUV), +0x41 the hidden races' (-1 no limit); the
    # contract's counts are the final's own (its +0x10: the world, sponsor
    # and URL races won it needs)
    row_of = {}
    for k, x in enumerate(rows):
        if not x['flags'] & 2:
            row_of.setdefault(x['record']['hash'], k)
    ours = collections.Counter(x['stage'] for x in rows if not x['flags'] & 2)
    stages, rules = [], []
    for k in range(len(sec[0x34a18]) // 0x50):
        r = sec[0x34a18][0x50 * k:0x50 * k + 0x50]
        st, nsp, stakes = struct.unpack_from('<BBh', r, 0)
        sp = [index.get(h, 255) for h in struct.unpack_from('<5I', r, 8)][:nsp] + [255] * (5 - nsp)
        final = row_of.get(struct.unpack_from('<I', r, 0x28)[0], 0xffff)
        # the final race's needs: the epic's (stage 5) are the race it follows'
        need = rows[final]['record'] if final != 0xffff else None
        if need and need['req'] == 0 and need['need'] in row_of:
            need = rows[row_of[need['need']]]['record']
        counts = need['need'] if need and need['req'] == 4 else 0
        mags = sum(1 for v in covers if v['stage'] == st and not v['dvd'])
        dvds = sum(1 for v in covers if v['stage'] == st and v['dvd'])
        finish = ours[st] if st < 2 else 0
        outruns = struct.unpack_from('<b', r, 0x40)[0]   # the stage's outrun wins at most (-1: no limit; the game's +0x40)
        stages.append(dict(stage=st, sponsors=sp, stakes=stakes, url=counts >> 24, choice=counts >> 8 & 0xff, dvd=dvds, mags=mags,
                           finish=finish, outruns=outruns))
        caps = [r[0x30], r[0x34], r[0x32], 0, r[0x31], r[0x33]]   # by the record's race type (+0x0F): circuit, sprint, Street X, -, drag, drift
        rules.append(dict(caps=caps, suv=r[0x37], hidden=struct.unpack_from('<b', r, 0x41)[0], final=final,
                          star=struct.unpack_from('<h', r, 4)[0], sponsor_races=counts >> 16 & 0xff))
    sms, tip_sms, mag_sms, tip_popup = [], {}, [], {}
    # a photo shoot's news as it opens (Xbox default.xbe 0x140390): its SMS
    # (SMS_MAG_S2_SHOW_1, type 13, the showcase's +8) has no text of its
    # own: the game writes SMS_MAG_GENERIC_SUBJ/BODY_1 or _2 by turns, the
    # magazine's title in them (the cover's +0x28 hash continued with
    # "_TITLE", 0x4cdf0); cond: the shoot's index in the showcases' table
    from n2feng import bin_hash
    where_d = {h for h, gx, gy, r in markers(game, 0xd)}
    show_order = [pooled(struct.unpack_from('<H', sec[0x34a1b], 24 * k)[0]) for k in range(len(sec[0x34a1b]) // 24)
                  if bin_hash(pooled(struct.unpack_from('<H', sec[0x34a1b], 24 * k + 2)[0])) in where_d]
    show_open = sorted((struct.unpack_from('<h', sec[0x34a1b], 24 * k + 4)[0], sec[0x34a1b][24 * k + 7], pooled(struct.unpack_from('<H', sec[0x34a1b], 24 * k)[0]))
                       for k in range(len(sec[0x34a1b]) // 24) if sec[0x34a1b][24 * k + 6] == 0)
    turn = {name: 1 + n % 2 for n, (_, _, name) in enumerate(show_open)}   # the generic text by turns, as they open
    def mag_title(show):
        r = [sec[0x34a16][64 * k:64 * k + 64] for k in range(len(sec[0x34a16]) // 64)
             if sec[0x34a16][64 * k:64 * k + 32].split(b'\0')[0].decode() == show + '_PICTURE']
        if not r:
            return 'a magazine'
        h = struct.unpack_from('<I', r[0], 0x28)[0]
        for c in b'_TITLE':
            h = (h * 33 + c) & 0xffffffff
        return lang.get(h, 'a magazine')
    # a unique part's news as its showcase opens (GameCube SMSMessageQueue::
    # AddWorldChallengeMessage): SMS_UNIQUE_* have no text either, the game
    # writes SMS_VISUAL_GENERIC_1..5 (the kind of part in its subject) or
    # SMS_PERF_GENERIC_1..3 by turns (one count for both, as they open)
    part_name = {11: 0x28f7092, 12: 0xbb034ea6, 13: 0x294d2a3, 22: 0x945e9bc5, 62: 0x78deea1c}   # Hoods, Spoilers, Rims, Wide Body Kits, Vinyls
    uniques = sorted((struct.unpack_from('<h', sec[0x34a1b], 24 * k + 4)[0], sec[0x34a1b][24 * k + 7], pooled(struct.unpack_from('<H', sec[0x34a1b], 24 * k)[0]),
                      sec[0x34a1b][24 * k + 20], sec[0x34a1b][24 * k + 21])
                     for k in range(len(sec[0x34a1b]) // 24) if sec[0x34a1b][24 * k + 6] == 2)
    unique_text = {}
    for n, (_, _, name, sub, part) in enumerate(uniques):
        if sub == 2:
            v = n % 3 + 1
            unique_text[name] = (lang.get(game_hash(f'SMS_PERF_GENERIC_{v}_SUBJECT')), lang.get(game_hash(f'SMS_PERF_GENERIC_{v}_BODY')))
        else:
            v = n % 5 + 1
            what = lang.get(part_name.get(part, 0), '')
            unique_text[name] = ((lang.get(game_hash(f'SMS_VISUAL_GENERIC_{v}_SUBJECT')) or '%s').replace('%s', what),
                                 (lang.get(game_hash(f'SMS_VISUAL_GENERIC_{v}_BODY')) or '').replace('%s', what))
    shop_kind_of = {struct.unpack_from('<I', sec[0x34a12], 0xa0 * k + 0x38)[0]:
                    SHOP_KIND.get(sec[0x34a12][0xa0 * k + 0x40:0xa0 * k + 0x50].split(b'\0')[0].decode().split('.')[0])
                    for k in range(len(sec[0x34a12]) // 0xa0)}
    late = []
    for k in range(len(sec[0x34a17]) // 20):
        off, typ, box, popup, envelope, data, money, sender = struct.unpack_from('<hBBBB2xIiI', sec[0x34a17], 20 * k)
        name = pooled(off)
        subject, body = lang.get(game_hash(name + '_SUBJECT')), lang.get(game_hash(name + '_BODY')) or lang.get(game_hash(name))
        show = 'STAGE_' + name[9:] if name.startswith('SMS_MAG_S') and not name.startswith('SMS_MAG_STAGE') else name[8:]
        if typ == 13 and name.startswith('SMS_MAG_') and show in show_order:
            v, title = turn.get(show, 1), mag_title(show)
            subject = (lang.get(game_hash(f'SMS_MAG_GENERIC_SUBJ_{v}')) or '%s').replace('%s', title)
            body = (lang.get(game_hash(f'SMS_MAG_GENERIC_BODY_{v}')) or '').replace('%s', title)
        new = False
        if typ == 13 and name.startswith('SMS_UNIQUE_'):
            show = 'STAGE_' + name[12:] if name[11] == 'S' and name[12].isdigit() else name[11:]
            subject, body = unique_text.get(show, (None, None))
            new = True
        if name.startswith('SMS_CAR_UNLOCK_') and (not subject or not body):   # a car in the lot: SMS_CAR_UNLOCK_1's text (SMSMessage::GetSubject/GetBody)
            subject, body = lang.get(game_hash('SMS_CAR_UNLOCK_1_SUBJECT')), lang.get(game_hash('SMS_CAR_UNLOCK_1_BODY'))
            new = name != 'SMS_CAR_UNLOCK_1'
        if not subject or not body:
            continue
        stage = (data >> 16) & 0xff if typ in (9, 10, 12, 13, 14) else 0
        # cond: the stage's count (9, 10, 12); the showcase (13); the race
        # won (1: its row); the shop entered (2: its kind; the car lot's
        # hash is in no shop's record, SMS_GPS_INTRO and SMS_LOC_GO_TO_GARAGE
        # come there, SMS_LOC_EVENTS_OUT_THERE at the crib); the seconds of
        # free roam (3); the DVD cover's stars (14)
        if typ in (9, 10, 12):
            cond = data & 0xffff
        elif typ == 13:
            cond = show_order.index(show) if show in show_order else 0xffff
        elif typ == 1:
            cond = row_of.get(data, 0xffff)
        elif typ == 2:
            cond = shop_kind_of.get(data, 0)
        elif typ in (3, 14):
            cond = data & 0xffff
        else:
            cond = 0
        if typ == 0xF:
            tip_sms[data] = len(sms)    # an engage tip's: the marker's hash
            tip_popup[data] = popup     # +4: the SMS opens itself (SMSMessageQueue::AnyUnseenAutoPopups); the rest only reach the mailbox
        (late if new else mag_sms if typ == 13 and name.startswith('SMS_MAG_') else sms).append(
            dict(name=name, type=typ, stage=stage, cond=cond, money=money, sender=lang.get(sender, 'Rachel'), subject=subject, body=body,
                 box=box, envelope=envelope, popup=popup))
    sms += mag_sms + late   # (after the others: a save's sent and read bits are by the message's place; the ones new in version 9 last)
    # Legacy shop/map positions from animated instances; entry geometry is
    # the distinct original TrackPathZone in shopzones.n2sz, never this point.
    l4ra = archived(game, 'TRACKS/L4RA.BUN')
    where = {}
    for t, p, n in chunks(l4ra):
        if t == 0x37090 and n >= 132:
            where[struct.unpack_from('<I', l4ra, p + 40)[0]] = struct.unpack_from('<3f', l4ra, p + 120)
    shops, shop_at = [], {}
    for k in range(len(sec[0x34a12]) // 0xa0):
        r = sec[0x34a12][0xa0 * k:0xa0 * k + 0xa0]
        name = r[:32].split(b'\0')[0].decode()
        marker = struct.unpack_from('<I', r, 0x3c)[0]
        if marker in where:
            shop_at[struct.unpack_from('<I', r, 0x38)[0]] = (where[marker][1], where[marker][2], where[marker][0])   # by its name's hash: GU x y z
        kind = SHOP_KIND.get(r[0x40:0x50].split(b'\0')[0].decode().split('.')[0])
        if marker not in where or kind is None:
            continue
        gx, gy, gz = where[marker]
        shops.append(dict(name=name, kind=kind, stage=r[0x9d], hidden=r[0x51], pos=(gy, gz, gx)))
    out = struct.pack('<I', len(stages))
    for v in stages:
        out += struct.pack('<BBH5B5Bbx', v['stage'], sum(1 for s in v['sponsors'] if s != 255), v['stakes'], *v['sponsors'],
                           v['url'], v['choice'], v['dvd'], v['mags'], v['finish'], v['outruns'])
    out += struct.pack('<I', len(sponsors))
    for v in sponsors:
        out += struct.pack('<16sHHH3Bx', v['name'].encode('latin-1')[:15], v['purse'], v['bonus'], v['rep'], *v['types'])
    out += struct.pack('<I', len(covers))
    for v in covers:
        out += struct.pack('<4B', v['stage'], int(v['dvd']), v['rating'], v['speech'])
    out += struct.pack('<I', len(sms))
    for v in sms:
        out += struct.pack('<BBHi3I', v['type'], v['stage'], v['cond'], v['money'], put(v['sender']), put(v['subject']), put(v['body']))
    out += struct.pack('<I', len(shops))
    for v in shops:
        out += struct.pack('<24sBBBx3f', v['name'].encode()[:23], v['kind'], v['stage'], v['hidden'], *v['pos'])
    # a tip: f32 GU x z, u16 its SMS, u8 radius, u8 flags (1: its SMS only
    # reaches the mailbox, the HUD's envelope and its chime; 0: it opens
    # itself, as ENGAGE_TIP_CAREER_PROG, the only tip that does; beta
    # issue 120. Older files: 0, every tip opening)
    tips = [dict(sms=tip_sms[h], pos=(gy, gx), radius=r, queue=0 if tip_popup.get(h) else 1) for h, gx, gy, r in markers(game, 0x12) if h in tip_sms]
    out += struct.pack('<I', len(tips))
    for v in tips:
        out += struct.pack('<2fHBB', *v['pos'], v['sms'], v['radius'], v['queue'])
    # the city's cash (markers of type 0x13, MoneyMarkerConfiguration 0x34A1E:
    # i16 amount, u8 condition (1: from the career stage in the value), u8
    # id for the save, u32 value, u32 the marker's hash)
    config = {}
    for k in range(len(sec[0x34a1e]) // 12):
        amount, cond, mid, value, h = struct.unpack_from('<hBBII', sec[0x34a1e], 12 * k)
        config[h] = (amount, value if cond == 1 else 0, mid)
    cash = [dict(pos=(gy, gx), amount=config[h][0], stage=config[h][1], id=config[h][2])
            for h, gx, gy, r in markers(game, 0x13) if h in config and config[h][2] < 64]
    out += struct.pack('<I', len(cash))
    for v in cash:
        out += struct.pack('<2fHBB', *v['pos'], v['amount'], v['stage'], v['id'])
    # the showcases (CareerShowcase 0x34A1B, 24 bytes: u16 the name and u16
    # the trigger's in the string pool (a city marker of type 0xd: where),
    # i16 stage, u8 kind (0 a magazine's photo shoot, 2 a unique part's), u8
    # the stage's wins it opens at, u32 its SMS's hash (SMS_MAG_S2_SHOW_1,
    # not in the SMS table), u32 its picture's (STAGE_2_SHOW_1_PICTURE: the
    # cover) or a unique part's shop's (CC_BODYSHOP_1); Xbox default.xbe
    # 0x140580/0x140630 open them, 0x140390 puts their marker in the city)
    from n2feng import bin_hash
    where = {h: (gy, gx) for h, gx, gy, r in markers(game, 0xd)}
    heights = race_zones(game)   # the same zones' heights, by the same name hash
    cover_of = {v['name']: k for k, v in enumerate(covers)}   # its magazine cover: <name>_PICTURE
    magazine_of = {sec[0x34a16][64 * k:64 * k + 32].split(b'\0')[0].decode(): struct.unpack_from('<I', sec[0x34a16], 64 * k + 0x28)[0]
                   for k in range(len(sec[0x34a16]) // 64)}   # the cover's magazine (+0x28: its setup's, show_cameras)
    setups = show_cameras(game)
    studio = studio_covers(game)
    shows = []
    for k in range(len(sec[0x34a1b]) // 24):
        a, b, stage, kind, wins, _, _ = struct.unpack_from('<HHhBBII', sec[0x34a1b], 24 * k)
        trigger = bin_hash(pooled(b))   # the marker's name (TRIGGER_START_STAGE_2_SHOW_1), by its bin hash
        # a unique part's (STAGE_n_VISUAL_k): +21 the kind of part it opens
        # (tools/convert_rating.py's: 11 hoods, 12 spoilers, 13 rims, 22 the
        # wide body kits, 62 the unique vinyls); the performance ones'
        # (STAGE_n_PERF_k) not in yet
        opens = sec[0x34a1b][24 * k + 21] if kind == 2 and '_VISUAL_' in pooled(a) else 0
        # a performance one's (STAGE_n_PERF_k): +21..23 three of the shop's
        # categories (default.xbe 0x112f30 maps them to 0x34A15's; 0x1408a0
        # lists those the car has not had yet), as a mask of the categories
        perf = sum(1 << PERF_SHOWCASE[v] for v in sec[0x34a1b][24 * k + 21:24 * k + 24] if v in PERF_SHOWCASE) \
            if kind == 2 and '_PERF_' in pooled(a) else 0
        if trigger in where:
            key = magazine_of.get(pooled(a) + '_PICTURE')
            mag, cams = setups.get(key, ('', []))
            mag, pose = (mag, []) if mag else studio.get(key, ('', []))
            time, sub = struct.unpack_from('<IB', sec[0x34a1b], 24 * k + 16)
            says, n_says, subject = ENGAGE.get(sub, ENGAGE[4])
            shows.append(dict(name=pooled(a), stage=stage, kind=kind, wins=wins, opens=opens, perf=perf,
                              cover=cover_of.get(pooled(a) + '_PICTURE', -1), pos=zone_at(where, heights, trigger), magazine=mag, cameras=cams, pose=pose,
                              to=show_target(sec, struct.unpack_from('<I', sec[0x34a1b], 24 * k + 12)[0], where, shop_at, heights) or zone_at(where, heights, trigger),
                              time=time, says=[put(lang.get(game_hash(f'{says}_{n}_BODY'))) for n in range(1, n_says + 1) if lang.get(game_hash(f'{says}_{n}_BODY'))],
                              subject=put(lang.get(subject, ''))))
    # the DVD covers' sessions (the game's showcase subtype 3, "DVD Cover
    # Opportunity"): won, the cover's SMS sends the player to the star on
    # the minimap, its place the cover's +0x2c (a city marker of type 0xd,
    # as a magazine's photographer); there, the session opens (0x13f620:
    # 0x110c00 finds the cover by the marker, 0x111e50 sends 0x11 for a
    # cover of the career's not yet shot); no countdown. After the
    # showcases: their places in the table, the save's bits, stay.
    for k in range(len(sec[0x34a16]) // 64):
        r = sec[0x34a16][64 * k:64 * k + 64]
        name = r[:32].split(b'\0')[0].decode()
        key, at = struct.unpack_from('<I', r, 0x28)[0], struct.unpack_from('<I', r, 0x2c)[0]
        if '_DVD_' not in name or at not in where:
            continue
        mag, cams = setups.get(key, ('', []))
        mag, pose = (mag, []) if mag else studio.get(key, ('', []))
        shows.append(dict(name=name, stage=int(name.split('_')[1]), kind=3, wins=0, opens=0, perf=0, cover=cover_of[name],
                          pos=zone_at(where, heights, at), magazine=mag, cameras=cams, pose=pose, to=zone_at(where, heights, at), time=0, says=[],
                          subject=put(lang.get(0xbf178f39, ''))))   # ("Subjec: DVD Cover Opportunity", the game's)
    out += struct.pack('<I', len(shows))
    for v in shows:
        cams = [x for c in v['cameras'] for x in c] + [0.0] * (21 - 7 * len(v['cameras']))
        says = (v['says'] + [0] * 5)[:5]
        out += struct.pack('<4BhH2f24s8s21f10f', v['stage'], v['kind'], v['wins'], v['opens'], v['cover'], v['perf'], v['pos'][0], v['pos'][2], v['name'].encode()[:23],
                           v['magazine'].encode()[:7], *cams, *(v['pose'] or [0.0] * 10))
        out += struct.pack('<2fHBx6I', v['to'][0], v['to'][2], v['time'], min(5, len(v['says'])), *says, v['subject'])
    # version 9: per row, its record (the rows' parallel table), then the
    # stages' rules
    records = []
    for x in rows:
        r = x['record']
        after = row_of.get(r['need'], 0xffff) if r['req'] == 0 else 0xffff
        names = [put(pooled(o)) for o in r['rival_names']] + [0] * (5 - r['rivals'])
        records.append(dict(record=r['index'], hidden=r['hidden'], suv=r['suv'], rivals=r['rivals'], req=r['req'], after=after,
                            need=r['need'] if r['req'] in (3, 4) else 0, names=names))
    out += struct.pack('<I', len(records))
    for v in records:
        out += struct.pack('<HBBBBHI5I', v['record'], v['hidden'], v['suv'], v['rivals'], v['req'], v['after'], v['need'], *v['names'])
    out += struct.pack('<I', len(rules))
    for v in rules:
        out += struct.pack('<6BBbHhB3x', *v['caps'], v['suv'], v['hidden'], v['final'], v['star'], v['sponsor_races'])
    # version 10: per row, how the race starter sets its rivals'
    # performance (RaceStarter::AddCareerEventAIOpponents: 0 a fixed
    # rating, 1 matched to the player's car, 2 the preset's own, 3 the
    # stage's ramp) and the fixed rating; then per stage the ramp
    # (RuntimeCareerStageInfo::GetAIPerformanceMatchingParameter: the
    # stage's races, 0x34A18 +0x42, and the rating from +0x44 to +0x48
    # as they are won)
    out += struct.pack('<I', len(rows))
    for x in rows:
        p = (x['record']['rival_perf'] + [(1, 0)] * 5)[:5]
        out += struct.pack('<5B5b2x', *[m for m, _ in p], *[q for _, q in p])
    ramps = [struct.unpack_from('<h', sec[0x34a18], 0x50 * k + 0x42) + struct.unpack_from('<2f', sec[0x34a18], 0x50 * k + 0x44)
             for k in range(len(sec[0x34a18]) // 0x50)]
    out += struct.pack('<I', len(ramps))
    for v in ramps:
        out += struct.pack('<h2xff', *v)
    # version 11: the rivals in a preset car of the original (Caleb and his
    # crew, the last stage's finals: AddCareerEventAIOpponents fills them
    # with RideInfo::FillWithPreset), their packs and neons
    from n2feng import bin_hash
    presets = [json.loads(f.read_text()) for f in sorted((ROOT / 'presets/rivals').glob('*.json'))]
    by_hash, random = {bin_hash(p['preset']): i for i, p in enumerate(presets)}, bin_hash('RANDOM')
    out += struct.pack('<I', len(presets))
    for p in presets:
        out += struct.pack('<24s16s16s', f"opp-{p['preset']}".encode(), p['car'].encode(), p.get('neon', '').encode())
    out += struct.pack('<I', len(rows))
    for x in rows:
        cars = x['record']['rival_cars']
        for c in cars:
            if c != random and c not in by_hash:
                raise ValueError(f"{x['record']['name']}: rival in preset 0x{c:08x} has no file in presets/rivals")
        out += struct.pack('<5B3x', *([0xff if c == random else by_hash[c] for c in cars] + [0xff] * 5)[:5])
    # version 12: per row, its record's trigger zone (the street's place
    # of the race, often far from the course's start)
    zones = race_zones(game)
    out += struct.pack('<I', len(rows))
    for x in rows:
        h = x['record']['zone']
        out += struct.pack('<3fI', *zones[h], h) if h in zones else struct.pack('<3fI', 0, 0, 0, 0)
    # version 13: per sponsor, the Sponsorship screen's texts
    # (uiSponsorsMain::RefreshHeader, ContractTerms::GetBody): its contract
    # (SPONSOR_DETAILS_<NAME>), its races (SPONSOR_INFO_<NAME>) and its
    # logo's texture (DECAL_ICON_<NAME>, cut at 23 as the textures' names)
    out += struct.pack('<I', len(sponsors))
    for v in sponsors:
        out += struct.pack('<3I', put(lang.get(game_hash('SPONSOR_DETAILS_' + v['key']), '')),
                           put(lang.get(game_hash('SPONSOR_INFO_' + v['key']), '')), put(sponsor_icon(v['key'])))
    # version 14: the event dialog's texts per row (RaceEngageDialog::Setup,
    # CareerEventData::GetEngageDialogBody/From): the body's choices (u32
    # offsets, all the rows' sets one after the other: a stage 0 race's own
    # ENGAGE_<NAME>, else one drawn from the kind's ENGAGE_..._RANDOM_1..n),
    # then per row u32 its subject (the kind, "Sponsored Sprint"), the type
    # line, the sender (+0x84's text, or its first rival's name), u16 its
    # first choice and how many
    bodies, sets, engage = [], {}, []
    for x in rows:
        r = x['record']
        if r['stage'] == 0:
            key, n = 'ENGAGE_' + r['name'], 0
        else:
            kind = ENGAGE_KIND.get(r['type'], '')
            key = ('ENGAGE_SUV' if r['suv'] else f'ENGAGE_SPON_{kind}' if r['reward'] == 1 else 'ENGAGE_URL' if r['reward'] == 2
                   else f'ENGAGE_{kind}') + '_RANDOM_%d'
            n = ENGAGE_RANDOM.get(key[7:-10], 0)
        if key not in sets:
            said = [lang.get(game_hash(key)) if not n else lang.get(game_hash(key % (i + 1))) for i in range(max(n, 1))]
            said = [t for t in said if t]
            sets[key] = (len(bodies), len(said))
            bodies += [put(t) for t in said]
        normal = r['suv'] or r['reward'] == 3
        subject = ENGAGE_SUBJECT.get(('' if normal else 'URL' if r['reward'] == 2 else 'SPON' if r['reward'] == 1 else '', r['type']), 0x9b39c122)
        kind_line = 0xb46377bc if r['reward'] == 2 and not normal else ENGAGE_TYPE.get(r['type'], 0x268d063c)
        sender = lang.get(r['sender'], '') if r['sender'] else pooled(r['rival_names'][0]) if r['rival_names'] else ''
        first, count = sets[key]
        engage.append(struct.pack('<3I2H', put(lang.get(subject, '')), put(lang.get(kind_line, '')), put(sender), first, count))
    out += struct.pack('<I', len(bodies)) + b''.join(struct.pack('<I', o) for o in bodies)
    out += struct.pack('<I', len(engage)) + b''.join(engage)
    # version 15: per SMS (the SMS table's order) its SmsData bytes 3-5:
    # the mailbox it is filed in once read (SMSMailboxViewer::
    # CanDisplayMessageInCurrentList: 1 Inbox, 2 Game Tips, 3 Special
    # Events, 4 Rachel, 5 Unlocks), whether it opens by itself in the free
    # roam (SMSMessageQueue::AnyUnseenAutoPopups) and its envelope's colour
    # (SMSGetEnvelopeImageHashAndColour's category)
    out += struct.pack('<I', len(sms)) + b''.join(struct.pack('<4B', v['box'], v['popup'], v['envelope'], 0) for v in sms)
    # version 16: per showcase, the heights of its star and of its goal
    # (the GPS's 3-D target: UnlockedTriggerZone::GetGPSPosition)
    out += struct.pack('<I', len(shows)) + b''.join(struct.pack('<2f', v['pos'][1], v['to'][1]) for v in shows)
    text.extend(bytes((-len(text)) % 4))
    out += struct.pack('<I', len(text)) + bytes(text)
    report = dict(stages=stages, rules=rules, sponsors=sponsors, covers=covers, shops=shops, tips=[dict(sms=sms[v['sms']]['name'], pos=v['pos']) for v in tips], cash=cash, shows=shows,
                  sms=[dict(name=v['name'], type=v['type'], stage=v['stage'], cond=v['cond'], subject=v['subject']) for v in sms])
    return out, report


def show_cameras(game):
    """{magazine hash: (its masthead's prefix, [3 cameras])}: the photo
    shoots' setups (FRONTEND/MAGAZINES_SHOWCASE.BIN, chunk 0x30231: u32 42
    cameras, u32 1, u32 14 setups; the cameras at +12, 44 bytes: u32 the
    name's hash, u32 0, f32 degrees around the car (0 its front, 90 its
    left), degrees up (at least 3: the game's), metres away, the point
    looked at (metres ahead, left, up of the car), the lens (degrees), 2 x
    0; the setups at +0xc8c, 56 bytes: u32 the magazine's hash (a cover's
    +0x28), u32 0, u32 counts of 4 lists (2 mastheads, 0, 3 cameras, 1),
    the mastheads' (BB_MASTHEAD_01/02), one, the cameras', one). Xbox
    default.xbe: 0xcdb80 opens a setup (the cover's, 0x11d160), 0x11cf90
    draws its masthead and one of its cameras at random, 0x54dcc places it
    (the elevation below 3 degrees raised to 3)."""
    from convert_magazines import names
    d = archived(game, 'FRONTEND/MAGAZINES_SHOWCASE.BIN')
    d = [d[p:p + n] for t, p, n in chunks(d) if t == 0x30231][0]
    ncams, _, nsets = struct.unpack_from('<3I', d, 0)
    cams = {struct.unpack_from('<I', d, 12 + 44 * i)[0]: struct.unpack_from('<7f', d, 12 + 44 * i + 8) for i in range(ncams)}
    tex = names(game)
    out = {}
    for i in range(nsets):
        r = struct.unpack_from('<14I', d, 0xc8c + 56 * i)
        mast = tex.get(r[6], '')
        out[r[0]] = (mast.split('_MAS')[0], [cams[h] for h in r[9:9 + r[4]] if h in cams][:3])
    return out


def studio_covers(game):
    """{magazine hash: (its prefix, its first pose)}: the covers made in the
    studio (FRONTEND/MAGAZINES_FRONTEND.BIN, chunk 0x30230: u32 37 poses of
    56 bytes at +12, u32 3, u32 16 setups of 104 bytes at +0x110c: u32 the
    magazine's hash (a cover's +0x28), u32 0, u32 counts of 6 lists, then
    the lists: backgrounds (3 at most), mastheads (2), girls (5), texts (3),
    the poses (3), one more (2); Xbox default.xbe 0x130c80, 0x11d0a0,
    0x11ced0 draws one of each at random). A pose: u32 its name's hash, u32
    0, f32 the car's turn on the studio's turntable (degrees, 0x278 of the
    showroom's car), the front wheels' steering (degrees, 0x39dc1c), then
    the camera's (0xd9930, 0x5eb50): the lens (degrees), the eye and the
    point looked at (the studio's metres: the car at 0, z up), 2 x 0, its
    roll (degrees)."""
    from convert_magazines import names
    d = archived(game, 'FRONTEND/MAGAZINES_FRONTEND.BIN')
    d = [d[p:p + n] for t, p, n in chunks(d) if t == 0x30230][0]
    tex, out = names(game), {}
    poses = {struct.unpack_from('<I', d, 12 + 56 * i)[0]: struct.unpack_from('<13f', d, 12 + 56 * i + 4) for i in range(struct.unpack_from('<I', d, 0)[0])}
    for i in range(struct.unpack_from('<I', d, 8)[0]):
        r = struct.unpack_from('<26I', d, 0x110c + 0x68 * i)
        mast = tex.get(r[11], '') if r[3] else ''   # the first masthead
        p = poses.get(r[8 + 13]) if r[6] else None   # the first pose
        out[r[0]] = (mast.split('_MAS')[0], list(p[1:10]) + [p[12]] if p else [])
    return out


def racer_names(xbe):
    """QuickRaceCharacterNames: the 53 first names the game gives its AI
    racers, from the Xbox default.xbe (a pointer array to the strings, found
    by one of them, "Lola"; the PS2's first quick race after the boot names
    its three Lola, Clarence and Audrey, the 50th, 29th and 10th)."""
    import n2exe
    exe = n2exe.Exe(xbe)
    b, va, raw = exe.b, exe.va, exe.raw
    def text(v):
        o = raw(v)
        if o is None or not 0x41 <= b[o] <= 0x5a:   # a capital first
            return None
        e = b.index(b'\0', o)
        return b[o:e].decode('latin-1') if 1 < e - o < 16 and b[o:e].isalpha() else None
    ref = b.index(struct.pack('<I', va(b.index(b'\0Lola\0') + 1)))
    while text(struct.unpack_from('<I', b, ref - 4)[0]):
        ref -= 4
    names = []
    while (n := text(struct.unpack_from('<I', b, ref + 4 * len(names))[0])):
        names.append(n)
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    globalb = archived(args.game, 'GLOBAL/GLOBALB.BUN')
    races = career_races(globalb)
    ids = read_event_ids(args.out / 'races.n2rc')
    track_ids = set()
    for f in sorted(args.out.glob('races-L4R?.n2rc')):
        track_ids.update(read_event_ids(f))
    rows = build(races, ids + sorted(track_ids - set(ids)), track_ids)
    for x in rows:
        if x['reward'] == 1:
            x['flags'] |= 8      # a sponsor race: its purse goes to the signed sponsor's contract
    out = struct.pack('<4s2I', b'N2CA', 16, len(rows))
    for x in rows:
        out += struct.pack('<HBBiBBBB', x['id'], x['stage'], x['type'], x['money'], x['reward'], x['laps'], x['flags'],
                           x['group'])
    written = []
    world, world_report = career_world(args.game, globalb, rows, races, written=written)
    (args.out / 'career.n2cr').write_bytes(out + world)
    (args.out / 'career-lang.json').write_text(json.dumps(career_translations(args.game, globalb, rows, races, written)) + '\n')
    from convert_shopzones import write as write_shopzones
    write_shopzones(args.game, [v['name'] for v in world_report['shops']], args.out)
    blob, found = hoods(args.game)
    (args.out / 'hoods.n2nb').write_bytes(blob)
    world_report['hoods'] = found
    (args.out / 'career-world.json').write_text(json.dumps(world_report, indent=1) + '\n')
    report = [dict(id=x['id'], stage=x['stage'], type=TYPES[x['type']] if x['type'] < len(TYPES) else x['type'],
                   money=x['money'], laps=x['laps'], approximated=bool(x['flags'] & 1), group=x['group'],
                   hidden=bool(x['flags'] & 2), track=bool(x['flags'] & 4), record=x['race'],
                   name=x['record']['name'],
                   kind=x['reward'], secret=bool(x['record']['hidden']), suv=bool(x['record']['suv']), rivals=x['record']['rivals'])
              for x in rows]
    (args.out / 'career.json').write_text(json.dumps(report, indent=1) + '\n')
    names = racer_names(n2source.executable(args.game))
    (args.out / 'names.txt').write_text('\n'.join(names) + '\n', encoding='utf-8', newline='\n')   # the AI racers' names, a line each
    print(len(rows), 'rows;', sum(1 for x in rows if x['flags'] & 1), 'with approximated money;',
          sum(1 for x in rows if x['flags'] & 4), 'on tracks;', max((x['group'] for x in rows), default=0), 'tournaments; first:',
          [(x['id'], x['stage'], x['money']) for x in rows[:6]])


def career_translations(game, globalb, rows, races, english):
    """career-lang.json, for convert_hud's language tables: per language,
    [English, its text] for the career's texts (english: career_world's,
    in order) that differ, the SMS and the sponsors' among them, put
    together the same way in the language (a magazine's title in its
    "%s cover opportunity"). The port shows career.n2cr's English through
    lang_en."""
    from convert_hud import LANGUAGES
    out = {}
    for code, language in LANGUAGES.items():
        if code == 'en':
            continue
        written = []
        career_world(game, globalb, rows, races, language, written)
        if len(written) != len(english):   # not the same choices: none rather than wrong pairs
            print(f'career-lang: {language} wrote {len(written)} texts, English {len(english)}; left out', file=sys.stderr)
            continue
        out[code] = sorted(dict((e, t) for e, t in zip(english, written) if e and t and e != t).items())
    return out


# The city's neighbourhoods (the world's triggers of type 0x11: chunk
# 0x3414A of the track's trigger pack, records of u32 type, the bounds at
# +0x20, the name's bin hash at +0x30, u16 corners at +0x40, u16 record
# size at +0x42, the corners (game x, y) from +0x44), with Rachel's line for
# a race there: the game's switch on NEIGHBOURHOOD_<name> (0x132380 in the
# Xbox default.xbe) to the bit of UG2_SPEECH Intro_Events' second parameter.
HOODS = {'COAL_HARBOUR_WEST': 28, 'COAL_HARBOUR_EAST': 27, 'AIRPORT': 6, 'STADIUM': 7, 'SOUTH_MARKET': 18,
         'EL_NORTE': 4, 'FORT_UNION': 15, 'FINANCE': 23, 'HOTELS': 24, 'BEACON_HILL_WEST': 0, 'BEACON_HILL_EAST': 0,
         'PIGEON_PARK': 20, 'JACKSON_HEIGHTS': 21}
HOOD_PACK = 0x69007260   # ZDIR hash of the city's trigger pack


def trigger_pack(game):
    """The city's trigger records (leaf 0x3414A of HOOD_PACK, see below)."""
    z = (game / 'ZDIR.BIN').read_bytes()
    _, arc, sector, _, size, _ = [e for e in struct.iter_unpack('<6I', z) if e[0] == HOOD_PACK][0]
    with (game / f'ZZDATA{arc}.BIN').open('rb') as f:
        f.seek(sector * 2048)
        pack = f.read(size)
    return [pack[p:p + n] for t, p, n in chunks(pack, 8, len(pack)) if t == 0x3414A][0]


def markers(game, kind):
    """Point markers of a type (0x12 engage tips, 0x13 money pickups): (name
    hash at +0x30, the centre of the square at +0x20..+0x28 in game x y,
    radius at +0x40)."""
    leaf, at, out = trigger_pack(game), 0, []
    while at + 0x4c <= len(leaf):
        typ, = struct.unpack_from('<I', leaf, at)
        r, step = struct.unpack_from('<hH', leaf, at + 0x40)
        if step < 0x44:
            break
        if typ == kind:
            x0, y0, x1, y1 = struct.unpack_from('<4f', leaf, at + 0x20)
            out.append((struct.unpack_from('<I', leaf, at + 0x30)[0], (x0 + x1) / 2, (y0 + y1) / 2, max(1, min(255, r))))
        at += step
    return out


def race_zones(game):
    """{name hash: GU (x, y, z)}: the career races' trigger zones (type
    0xd records of the trigger pack, a square 8 m across; the game's
    TrackPathZone: its centre at +0x04, +0x08 in game x y, its height at
    +0x14; a race's record names its zone at +0x28). The game engages a
    race there and puts its marker there (UpdateTriggerEffects), not at
    the course's start: the race's own load takes the car to the grid."""
    leaf, at, out = trigger_pack(game), 0, {}
    while at + 0x44 <= len(leaf):
        typ, = struct.unpack_from('<I', leaf, at)
        step, = struct.unpack_from('<H', leaf, at + 0x42)
        if step < 0x44:
            break
        if typ == 0xd:
            gx, gy = struct.unpack_from('<2f', leaf, at + 4)
            out[struct.unpack_from('<I', leaf, at + 0x30)[0]] = (gy, struct.unpack_from('<f', leaf, at + 0x14)[0], gx)
        at += step
    return out


def hoods(game):
    """hoods.n2nb: 'N2NB', u32 version 1, u32 polygons; a polygon: u8
    neighbourhood (HOODS order), u8 line bit, u16 corners, f32 bounds (x
    min, z min, x max, z max), corners f32 x z (GU)."""
    from n2feng import bin_hash
    z = (game / 'ZDIR.BIN').read_bytes()
    _, arc, sector, _, size, _ = [e for e in struct.iter_unpack('<6I', z) if e[0] == HOOD_PACK][0]
    with (game / f'ZZDATA{arc}.BIN').open('rb') as f:
        f.seek(sector * 2048)
        pack = f.read(size)
    names = list(HOODS)
    by_hash = {bin_hash(n): k for k, n in enumerate(names)}
    leaf = [pack[p:p + n] for t, p, n in chunks(pack, 8, len(pack)) if t == 0x3414A][0]
    out, at = [], 0
    while at + 0x44 <= len(leaf):
        typ, = struct.unpack_from('<I', leaf, at)
        n, step = struct.unpack_from('<HH', leaf, at + 0x40)
        if step < 0x44:
            break
        if typ == 0x11 and struct.unpack_from('<I', leaf, at + 0x30)[0] in by_hash:
            k = by_hash[struct.unpack_from('<I', leaf, at + 0x30)[0]]
            pts = [struct.unpack_from('<2f', leaf, at + 0x44 + 8 * i) for i in range(n)]
            out.append((k, [(gy, gx) for gx, gy in pts]))
        at += step
    blob = struct.pack('<4s2I', b'N2NB', 1, len(out))
    for k, pts in out:
        xs, zs = [p[0] for p in pts], [p[1] for p in pts]
        blob += struct.pack('<2BH4f', k, HOODS[names[k]], len(pts), min(xs), min(zs), max(xs), max(zs))
        blob += b''.join(struct.pack('<2f', *p) for p in pts)
    return blob, sorted({names[k] for k, _ in out})


if __name__ == '__main__':
    main()
