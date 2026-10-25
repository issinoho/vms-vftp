#!/usr/bin/env python3
"""vtscreen.py - replay a PTYDRIVE transcript through a small VT100 emulator.

    tools/vtscreen.py [--rows 24] [--cols 80] [--attrs] [--step LABEL]... transcript.txt

PTYDRIVE prints what the pseudo-terminal sent, with control characters made
visible (<ESC>, <CR>, <LF> plus a newline, <0E>) and PTD$ events as {CHAR}.
This turns that back into bytes, plays them on a screen, and prints the screen
after each step (or only the named ones). --attrs adds a second picture of the
renditions: R reverse, B bold, U underline. Line drawing (ESC ) 0, SO/SI) is
shown with box characters. Enough of VT100/VT220 for what SMG$ sends."""
import re
import sys

GFX = {'q': '─', 'x': '│', 'l': '┌', 'k': '┐', 'm': '└', 'j': '┘', 't': '├',
       'u': '┤', 'w': '┬', 'v': '┴', 'n': '┼', '`': '◆', 'a': '▒'}


def decode(text):
    text = re.sub(r'\{[A-Z]+\}', '', text)
    text = text.replace('<LF>\n', '\n')
    out = []
    i = 0
    while i < len(text):
        if text.startswith('<ESC>', i):
            out.append('\x1b'); i += 5
        elif text.startswith('<CR>', i):
            out.append('\r'); i += 4
        elif re.match(r'<[0-9A-F]{2}>', text[i:i + 4]):
            out.append(chr(int(text[i + 1:i + 3], 16))); i += 4
        else:
            out.append(text[i]); i += 1
    return ''.join(out)


class Screen:
    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.ch = [[' '] * cols for _ in range(rows)]
        self.at = [[' '] * cols for _ in range(rows)]
        self.r = self.c = 0
        self.sgr = set()
        self.fg, self.bg = 7, 0
        self.col = [[' '] * cols for _ in range(rows)]
        self.g = ['B', 'B']
        self.shift = 0
        self.top, self.bot = 0, rows - 1
        self.saved = (0, 0)

    def scroll(self, n=1):
        for _ in range(n):
            for g in (self.ch, self.at, self.col):
                del g[self.top]
                g.insert(self.bot, [' '] * self.cols)

    def put(self, x):
        if self.c >= self.cols:
            self.c = 0
            self.lf()
        gfx = self.g[self.shift] == '0'
        self.ch[self.r][self.c] = GFX.get(x, x) if gfx else x
        a = ''.join(sorted(self.sgr))
        self.at[self.r][self.c] = a[0] if a else ' '
        self.col[self.r][self.c] = self.colour_code()
        self.c += 1

    def colour_code(self):
        bold = 'B' in self.sgr
        key = (self.bg, self.fg) if 'R' in self.sgr else (self.fg, self.bg)   # reverse swaps
        if key == (7, 4): return 'w' if bold else '.'
        if key == (3, 4): return 'y'
        if key == (6, 4): return 'C' if bold else 'c'
        if key == (0, 6): return '#'
        if key == (0, 7): return '='
        if self.bg == 1: return '!'
        return '?'


    def lf(self):
        if self.r == self.bot:
            self.scroll()
        elif self.r < self.rows - 1:
            self.r += 1

    def csi(self, params, final, priv):
        nums = [int(p) if p else 0 for p in params.split(';')] if params else []
        n = nums[0] if nums else 0
        if priv:
            return
        if final in 'Hf':
            r = (nums[0] if len(nums) > 0 and nums[0] else 1) - 1
            c = (nums[1] if len(nums) > 1 and nums[1] else 1) - 1
            self.r, self.c = min(r, self.rows - 1), min(c, self.cols - 1)
        elif final == 'J':
            rng = []
            if n == 0:
                self.clear_line(self.r, self.c, self.cols)
                rng = range(self.r + 1, self.rows)
            elif n == 1:
                self.clear_line(self.r, 0, self.c + 1)
                rng = range(0, self.r)
            else:
                rng = range(self.rows)
            for rr in rng:
                self.clear_line(rr, 0, self.cols)
        elif final == 'K':
            if n == 0:
                self.clear_line(self.r, self.c, self.cols)
            elif n == 1:
                self.clear_line(self.r, 0, self.c + 1)
            else:
                self.clear_line(self.r, 0, self.cols)
        elif final == 'm':
            for p in nums or [0]:
                if 30 <= p <= 37:
                    self.fg = p - 30
                elif 40 <= p <= 47:
                    self.bg = p - 40
                if p == 0:
                    self.sgr = set()
                    self.fg, self.bg = 7, 0
                elif p == 1:
                    self.sgr.add('B')
                elif p == 4:
                    self.sgr.add('U')
                elif p == 7:
                    self.sgr.add('R')
        elif final == 'r':
            self.top = (nums[0] - 1) if nums and nums[0] else 0
            self.bot = (nums[1] - 1) if len(nums) > 1 and nums[1] else self.rows - 1
            self.r = self.c = 0
        elif final == 'A':
            self.r = max(0, self.r - (n or 1))
        elif final == 'B':
            self.r = min(self.rows - 1, self.r + (n or 1))
        elif final == 'C':
            self.c = min(self.cols - 1, self.c + (n or 1))
        elif final == 'D':
            self.c = max(0, self.c - (n or 1))
        elif final == 'X':
            self.clear_line(self.r, self.c, min(self.cols, self.c + (n or 1)))
        elif final == 'L':
            for _ in range(n or 1):
                for g in (self.ch, self.at, self.col):
                    del g[self.bot]
                    g.insert(self.r, [' '] * self.cols)
        elif final == 'M':
            for _ in range(n or 1):
                for g in (self.ch, self.at, self.col):
                    del g[self.r]
                    g.insert(self.bot, [' '] * self.cols)

    def clear_line(self, r, c1, c2):
        for c in range(c1, c2):
            self.ch[r][c] = ' '
            self.at[r][c] = ' '
            self.col[r][c] = ' '

    def feed(self, data):
        i = 0
        while i < len(data):
            x = data[i]
            if x == '\x1b':
                if i + 1 < len(data) and data[i + 1] == '[':
                    m = re.match(r'\x1b\[(\??)([0-9;]*)([@-~])', data[i:])
                    if m:
                        self.csi(m.group(2), m.group(3), m.group(1))
                        i += m.end(); continue
                elif i + 2 < len(data) and data[i + 1] in '()':
                    self.g[0 if data[i + 1] == '(' else 1] = data[i + 2]
                    i += 3; continue
                elif i + 1 < len(data):
                    y = data[i + 1]
                    if y == '7':
                        self.saved = (self.r, self.c)
                    elif y == '8':
                        self.r, self.c = self.saved
                    elif y == 'D':
                        self.lf()
                    elif y == 'M':
                        self.r = max(0, self.r - 1)
                    elif y == 'E':
                        self.c = 0; self.lf()
                    i += 2; continue
                i += 1; continue
            if x == '\r':
                self.c = 0
            elif x == '\n':
                self.lf()
            elif x == '\b':
                self.c = max(0, self.c - 1)
            elif x == '\t':
                self.c = min(self.cols - 1, (self.c // 8 + 1) * 8)
            elif x == '\x0e':
                self.shift = 1
            elif x == '\x0f':
                self.shift = 0
            elif ord(x) >= 32:
                self.put(x)
            i += 1

    def text(self, attrs=False, colours=False):
        lines = [''.join(row).rstrip() for row in self.ch]
        if attrs:
            lines += ['', 'renditions:'] + [''.join(row).rstrip() for row in self.at]
        if colours:
            lines += ['', 'colours (. text  w bright  y yellow  c/C cyan  # bar  = bar off  ! error):'] + \
                     [''.join(row).rstrip() for row in self.col]
        return '\n'.join(lines)


def main():
    args = sys.argv[1:]
    rows, cols, attrs, colours, steps = 24, 80, False, False, []
    while args and args[0].startswith('--'):
        a = args.pop(0)
        if a == '--rows':
            rows = int(args.pop(0))
        elif a == '--cols':
            cols = int(args.pop(0))
        elif a == '--attrs':
            attrs = True
        elif a == '--colours':
            colours = True
        elif a == '--step':
            steps.append(args.pop(0))
    text = open(args[0], encoding='latin-1').read()
    parts = re.split(r'^---- (.*)\n', text, flags=re.M)
    s = Screen(rows, cols)
    for k in range(1, len(parts), 2):
        label, body = parts[k], parts[k + 1]
        s.feed(decode(body.rstrip('\n')))
        if not steps or label.strip() in steps:
            print('=' * 30, label)
            print(s.text(attrs, colours))


if __name__ == '__main__':
    main()
