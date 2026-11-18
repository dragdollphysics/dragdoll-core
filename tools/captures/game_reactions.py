# SPDX-License-Identifier: MIT
"""How the game itself reacts to a vehicle hit, from a live capture (dragdoll-sa with F6: car hits OFF).

For every pedestrian whose animation, health or state changes like a hit (knock-down / fall / lying / get-up animations,
or a health drop), prints the timeline: each animation with how long it played, and the health / state changes.

    python tools/captures/game_reactions.py build/captures/live_<time>.jsonl [more captures]

Labels come from dragdoll-sa: "<animation> <time>/<total>[ + <second animation>] | hp <health> st <ped state>" (older
captures: "<animation> (<group>) ... | hp <health> state <ped state>"; labels hold 63 bytes)
(the most blended-in animation of all, partial ones included: the game's knock-down and death animations are partial).
"""
import json
import re
import sys

HIT_WORDS = ('KD_', 'KO_', 'FALL', 'FLOOR', 'GETUP', 'HIT_', 'CAR_HIT', 'SKID', 'ROLL')
LABEL = re.compile(r'^(?P<anim>.+?) \((?P<group>[^)]*)\)(?: [\d.]+/[\d.]+)?(?: \+ (?P<second>[^|]*?))?(?: \| hp (?P<hp>-?[\d.]+) state (?P<state>-?\d+))?$')
# SA ped states worth naming (ePedState).
STATES = {1: 'idle', 4: 'wander', 41: 'fall', 42: 'getup', 54: 'die', 55: 'dead'}


def parse(label):
    """(animation name, second animation name or '', health or None, state or None) - tolerant of cut-off labels."""
    label = label or ''
    first = re.match(r'^(\S+)', label)
    second = re.search(r' \+ (\S+)', label)
    hp = re.search(r'\| hp (-?[\d.]+)', label)
    st = re.search(r'(?:state|st) (-?\d+)', label)
    return (first.group(1) if first else '', second.group(1) if second else '', float(hp.group(1)) if hp else None,
            int(st.group(1)) if st else None)


def analyse(path):
    peds = {}  # id -> list of (seconds, anim, hp, state)
    note = ''
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            m = rec.get('m', {})
            if m.get('t') == 'note':
                note = m.get('text', '')
            if 'step' not in m:
                continue
            t = m['step'] / 120.0
            for c in m.get('characters', []):
                if not c.get('label') or c.get('name', '').startswith('CJ'):
                    continue
                anim, _, hp, st = parse(c['label'])
                seq = peds.setdefault(c['id'], [])
                if hp is None and seq:
                    hp, st = seq[-1][2], seq[-1][3]  # A cut-off label: keep the last known values
                if not seq or seq[-1][1:] != (anim, hp, st):
                    seq.append((t, anim, hp, st))
    print('== %s  (note: "%s")' % (path, note))
    shown = 0
    for cid, seq in peds.items():
        hit = any(any(w in a.upper() for w in HIT_WORDS) for _, a, _, _ in seq)
        hps = [h for _, _, h, _ in seq if h is not None]
        if not hit and not (hps and min(hps) < max(hps)):
            continue
        shown += 1
        start = next((t for t, a, _, _ in seq if any(w in a.upper() for w in HIT_WORDS)), seq[0][0])
        print('  ped #%d (times from its first hit animation):' % cid)
        # Collapse consecutive entries with the same animation, keeping health / state changes.
        rows = []
        for i, (t, a, h, s) in enumerate(seq):
            end = seq[i + 1][0] if i + 1 < len(seq) else t
            if rows and rows[-1][1] == a:
                rows[-1][3] = end
                if h != rows[-1][4] or s != rows[-1][5]:
                    rows[-1][6].append('hp %s state %s at %+.2f s' % (h, STATES.get(s, s), t - start))
                    rows[-1][4], rows[-1][5] = h, s
            else:
                rows.append([t, a, None, end, h, s, []])
        for t, a, _, end, h, s, changes in rows:
            if end - start < -2.0:
                continue  # Long before the hit
            print('    %+7.2f s  %-34s %5.2f s   hp %-5s %-7s %s' % (t - start, a[:34], end - t, '%.0f' % h if h is not None else '-',
                                                                 STATES.get(s, s) if s is not None else '-', '; '.join(changes)))
    if shown == 0:
        print('  no hit pedestrians found (captured with F6 = car hits OFF? labels need the dragdoll-sa build with "| hp")')


for p in sys.argv[1:]:
    analyse(p)
