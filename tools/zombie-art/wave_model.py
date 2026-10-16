"""Rough model of Board::PickZombieWaves for Adventure Plus: average zombie
health sent per wave, old rules vs new, against the campaign's own 20-wave
replay formula. Health in plain-zombie units (270 = 1)."""
import random

# type: (value, first allowed wave, pick weight, health)
Z = {
    'NORMAL': (1, 1, 4000, 270), 'FLAG': (1, 1, 0, 270), 'CONE': (2, 1, 4000, 640),
    'POLE': (2, 5, 2000, 500), 'PAIL': (4, 1, 3000, 1370), 'NEWS': (2, 1, 1000, 420),
    'FOOT': (7, 5, 2000, 1670), 'ZAMB': (7, 10, 2000, 1350), 'JACK': (3, 10, 1000, 500),
    'DIG': (4, 10, 1000, 370), 'POGO': (4, 10, 1000, 770), 'YETI': (4, 1, 1, 1350),
    'LADDER': (4, 10, 1000, 1000), 'GARG': (10, 15, 1500, 3000),
    'TANK': (7, 6, 1500, 1800), 'HEAVY': (5, 3, 2000, 1300),
    'SOLDIER': (3, 2, 4000, 1000), 'SPY': (4, 6, 1000, 500),
}
FLAGS = {9, 19}

OLD = [
    ('1 Dust Bowl', ['NORMAL','CONE','PAIL'], 1, [('PAIL',2,1,1,4)]),
    ('2 Yummy', ['NORMAL','CONE','PAIL','FOOT'], 1, [('FOOT',4,2,1,3),('PAIL',3,2,1,2),('HEAVY',9,9,1,1)]),
    ('3 Up and Under', ['NORMAL','CONE','PAIL','LADDER','DIG'], 2, [('LADDER',3,1,1,3),('DIG',5,3,1,3)]),
    ('4 Yeti Season', ['NORMAL','CONE','PAIL','YETI'], 1, [('YETI',1,2,1,4)]),
    ('5 Bowling', ['NORMAL','CONE','PAIL','NEWS','FOOT'], 1, [('FOOT',3,1,1,4)]),
    ('6 Heavy Weapons Guy', ['NORMAL','CONE','LADDER','DIG','GARG','HEAVY'], 2, [('GARG',9,4,1,1),('LADDER',3,2,1,2),('HEAVY',3,2,1,3)]),
    ('7 Red Spy', ['NORMAL','CONE','PAIL','POLE','NEWS','FOOT','LADDER','HEAVY'], 3, [('FOOT',3,2,1,3),('POLE',2,2,1,2)]),
    ('8 Robots', ['NORMAL','CONE','PAIL','FOOT','ZAMB','TANK'], 3, [('ZAMB',5,3,1,2),('FOOT',4,3,1,2),('TANK',6,4,1,2)]),
    ('9 Tunnel Vision', ['NORMAL','CONE','PAIL','DIG','POGO','JACK','HEAVY'], 3, [('DIG',3,1,2,4),('POGO',5,3,1,2)]),
    ('10 Everything', ['NORMAL','CONE','PAIL','FOOT','LADDER','DIG','ZAMB','YETI','GARG','HEAVY','TANK'], 4, [('GARG',9,4,1,2),('HEAVY',3,2,1,3),('TANK',6,4,1,2)]),
]
NEW_PREV2 = [
    ('1 Dust Bowl', ['NORMAL','CONE','PAIL'], 0, [('PAIL',3,2,1,2),('HEAVY',12,7,1,1)]),
    ('2 Yummy', ['NORMAL','CONE','PAIL','FOOT'], 0, [('FOOT',5,3,1,2),('PAIL',3,3,1,1),('HEAVY',6,4,1,1)]),
    ('3 Up and Under', ['NORMAL','CONE','PAIL','LADDER','DIG'], 1, [('LADDER',4,2,1,2),('DIG',6,4,1,2),('HEAVY',7,4,1,1)]),
    ('4 Yeti Season', ['NORMAL','CONE','PAIL','YETI'], 0, [('YETI',2,3,1,2),('HEAVY',8,5,1,1)]),
    ('5 Bowling', ['NORMAL','CONE','PAIL','NEWS'], 0, [('FOOT',6,4,1,1),('GARG',19,1,0,1)]),
    ('6 Heavy Weapons Guy', ['NORMAL','CONE','LADDER','DIG','GARG','HEAVY'], 1, [('GARG',9,5,1,1),('LADDER',4,3,1,1),('HEAVY',4,3,1,2)]),
    ('7 Red Spy', ['NORMAL','CONE','PAIL','POLE','NEWS','FOOT','LADDER','HEAVY'], 1, [('FOOT',5,3,1,2),('POLE',3,3,1,2),('TANK',10,6,1,1)]),
    ('8 Robots', ['NORMAL','CONE','PAIL','FOOT','ZAMB','TANK'], 1, [('ZAMB',7,4,1,1),('FOOT',6,4,1,1),('TANK',8,5,1,1)]),
    ('9 Tunnel Vision', ['NORMAL','CONE','PAIL','DIG','POGO','JACK','HEAVY'], 1, [('DIG',4,2,1,2),('POGO',6,4,1,1),('TANK',10,6,1,1)]),
    ('10 Everything', ['NORMAL','CONE','PAIL','FOOT','LADDER','DIG','ZAMB','YETI','GARG','HEAVY','TANK'], 2, [('GARG',10,5,1,1),('HEAVY',5,3,1,2),('TANK',8,5,1,1)]),
]

NEW_PREV = [
    ('1 Dust Bowl', ['NORMAL','CONE','PAIL'], 0, [('PAIL',3,2,1,2)]),
    ('2 Yummy', ['NORMAL','CONE','PAIL','FOOT'], 0, [('FOOT',5,3,1,2),('PAIL',3,3,1,1),('HEAVY',9,9,1,1)]),
    ('3 Up and Under', ['NORMAL','CONE','PAIL','LADDER','DIG'], 1, [('LADDER',4,2,1,2),('DIG',6,4,1,2)]),
    ('4 Yeti Season', ['NORMAL','CONE','PAIL','YETI'], 0, [('YETI',2,3,1,2)]),
    ('5 Bowling', ['NORMAL','CONE','PAIL','NEWS'], 0, [('FOOT',6,4,1,1)]),
    ('6 Heavy Weapons Guy', ['NORMAL','CONE','LADDER','DIG','GARG','HEAVY'], 1, [('GARG',9,5,1,1),('LADDER',4,3,1,1),('HEAVY',4,3,1,2)]),
    ('7 Red Spy', ['NORMAL','CONE','PAIL','POLE','NEWS','FOOT','LADDER','HEAVY'], 1, [('FOOT',5,3,1,2),('POLE',3,3,1,2)]),
    ('8 Robots', ['NORMAL','CONE','PAIL','FOOT','ZAMB','TANK'], 1, [('ZAMB',7,4,1,1),('FOOT',6,4,1,1),('TANK',8,5,1,1)]),
    ('9 Tunnel Vision', ['NORMAL','CONE','PAIL','DIG','POGO','JACK','HEAVY'], 1, [('DIG',4,2,1,2),('POGO',6,4,1,1)]),
    ('10 Everything', ['NORMAL','CONE','PAIL','FOOT','LADDER','DIG','ZAMB','YETI','GARG','HEAVY','TANK'], 2, [('GARG',10,5,1,1),('HEAVY',5,3,1,2),('TANK',8,5,1,1)]),
]

NEW = [
    ('1 Dust Bowl', ['NORMAL','CONE','PAIL','SOLDIER'], 0, [('PAIL',3,2,1,2),('HEAVY',12,7,1,1)]),
    ('2 Yummy', ['NORMAL','CONE','PAIL','FOOT','SOLDIER'], 0, [('FOOT',5,3,1,2),('PAIL',3,3,1,1),('HEAVY',6,4,1,1)]),
    ('3 Up and Under', ['NORMAL','CONE','PAIL','LADDER','DIG','SOLDIER'], 1, [('LADDER',4,2,1,2),('DIG',7,5,1,1),('HEAVY',7,4,1,1),('SPY',4,4,1,1)]),
    ('4 Yeti Season', ['NORMAL','CONE','PAIL','YETI','SOLDIER'], 0, [('YETI',2,3,1,2),('HEAVY',8,5,1,1),('SOLDIER',5,4,1,1)]),
    ('5 Bowling', ['NORMAL','CONE','PAIL','NEWS'], 0, [('FOOT',6,4,1,1),('GARG',19,1,0,1)]),
    ('6 Heavy Weapons Guy', ['NORMAL','CONE','LADDER','DIG','GARG','HEAVY','SOLDIER','SPY'], 1, [('GARG',9,5,1,1),('LADDER',4,3,1,1),('HEAVY',4,3,1,2),('SOLDIER',5,3,1,1)]),
    ('7 Red Spy', ['NORMAL','CONE','PAIL','POLE','NEWS','FOOT','HEAVY','SOLDIER','SPY'], 1, [('SPY',3,2,1,3),('FOOT',6,4,1,1),('TANK',10,6,1,1),('SOLDIER',4,3,1,1)]),
    ('8 Robots', ['NORMAL','CONE','PAIL','FOOT','ZAMB','TANK','SOLDIER','SPY'], 1, [('ZAMB',7,4,1,1),('FOOT',6,4,1,1),('TANK',8,5,1,1),('SOLDIER',4,3,1,1)]),
    ('9 Tunnel Vision', ['NORMAL','CONE','PAIL','DIG','POGO','JACK','HEAVY','SPY','SOLDIER'], 1, [('DIG',4,2,1,2),('POGO',6,4,1,1),('TANK',10,6,1,1),('SOLDIER',5,4,1,1)]),
    ('10 Everything', ['NORMAL','CONE','PAIL','FOOT','LADDER','DIG','ZAMB','GARG','HEAVY','TANK','SPY','SOLDIER'], 1, [('GARG',10,5,1,1),('HEAVY',5,3,1,2),('TANK',8,5,1,1),('SOLDIER',3,3,1,2)]),
]


def wave_hp(roster, bonus, feats, wave, rule, rng):
    bowling = rule.endswith('bowl')
    if rule.startswith('old'):
        pts = wave * 2 // 3 + 2 + bonus
    elif rule.startswith('new'):
        pts = (wave // 5 + 1) if bowling else (wave * 2 // 5 + 1 + bonus)
    else:  # campaign replay
        pts = wave * 2 // 5 + 1
    hp = 0
    if wave in FLAGS:
        plain = min(pts, 8)
        pts = int(pts * 2.5)
        hp += (plain + 1) * 270
    if bowling:
        pts *= 4
    while pts > 0:
        c = [(t, Z[t]) for t in roster if Z[t][0] <= pts and wave + 1 >= Z[t][1] and Z[t][2] > 0]
        if not c:
            c = [('NORMAL', Z['NORMAL'])]
        t, d = rng.choices(c, weights=[x[1][2] for x in c])[0]
        pts -= d[0]
        hp += d[3]
    for (t, frm, every, per, perflag) in feats:
        if wave < frm:
            continue
        n = perflag if wave in FLAGS else (per if (wave - frm) % max(every, 1) == 0 else 0)
        hp += n * Z[t][3]
    return hp


def run(levels, rule, pct):
    rng = random.Random(1)
    out = {}
    for name, roster, bonus, feats in levels:
        r = rule + ('bowl' if 'Bowling' in name else '')
        tot = [0.0] * 20
        for _ in range(200):
            for w in range(20):
                tot[w] += wave_hp(roster, bonus, feats, w, r, rng) * pct / 100 / 270
        out[name] = [t / 200 for t in tot]
    return out


old = run(NEW_PREV2, 'new', 100)
new = run(NEW, 'new', 100)
camp = [sum(wave_hp(['NORMAL','CONE','PAIL','FOOT'], 0, [], w, 'camp', random.Random(w)) for _ in range(1)) / 270 for w in range(20)]
print('before = last round, after = with the extra TF2 enemies')
for name in old:
    o, n = old[name], new[name]
    print('%-22s %8.0f %8.0f %8.0f %8.0f   %8.0f %8.0f   total old %5.0f new %5.0f (%.0f%%)' % (
        name, o[5], o[9], o[15], o[19], n[9], n[19], sum(o), sum(n), 100 * sum(n) / sum(o)))
print('campaign replay (no features, 100%%): w9F %.0f, w19F %.0f, total %.0f' % (camp[9], camp[19], sum(camp)))
