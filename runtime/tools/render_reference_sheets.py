#!/usr/bin/env python3
"""Author the offline control/map sheets as editable SVG, with optional PNG rendering."""
from pathlib import Path
import argparse
import html
import textwrap
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BG, FG, MID, LINE = '#0c0d0d', '#eeeeee', '#aaaaaa', '#494b4b'

class Sheet:
    def __init__(self, height, number):
        self.height = height
        self.parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="860" height="{height}" viewBox="0 0 860 {height}">',
                      '<style>text{font-family:Arial,Helvetica,sans-serif} .mono{font-family:monospace;letter-spacing:1px}</style>']
        self.box(0,0,860,height,BG,LINE)
        self.text('RHYTHMER OS / FOR MPC SAMPLE',32,42,10,MID,mono=True)
        self.text(number,638,42,10,MID,mono=True)
        self.line(32,62,828,62)
    def box(self,x,y,w,h,fill='none',stroke=LINE):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}"/>')
    def line(self,x,y,a,b):
        self.parts.append(f'<path d="M{x},{y} L{a},{b}" stroke="{LINE}"/>')
    def text(self,s,x,y,size=14,colour=FG,bold=False,mono=False):
        self.parts.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{colour}" font-weight="{700 if bold else 400}"'+(' class="mono"' if mono else '')+'>'+html.escape(s)+'</text>')
    def paragraph(self,s,x,y,width,size=13,colour=MID):
        lines=textwrap.wrap(s,max(12,int(width/(size*.54))))
        for n,line in enumerate(lines):self.text(line,x,y+n*(size+6),size,colour)
        return y+len(lines)*(size+6)
    def key(self,s,x,y):
        w=max(48,len(s)*7+18);self.box(x,y,w,32,'#202222');self.text(s,x+9,y+21,11,FG,True);return w
    def save(self,path):path.write_text(''.join(self.parts)+'</svg>')

def controls(path):
    s=Sheet(1200,'01 / KEEP CLOSE')
    s.text('THE CONTROLS YOU NEED FIRST',32,98,10,MID,mono=True)
    s.text('Essential controls.',32,158,48,FG,True);s.text('Buttons and results.',32,216,48,FG,True)
    s.text('Push means click. Turn means rotate. Read the current B1–B3 labels.',32,252,14,FG,True)
    cards=[
        ('GO BACK',['SHIFT','+','ENCODER PUSH'],'Return/cancel. GEN returns to GEN first. In Mixer, Shift + Push renames a used track; in Song it adds a marker. In Menu it follows BACK.'),
        ('OPEN MIXER',['PAD FX','+','KNOB FX'],'Press together. Select a track; Push opens its sound. Hold encoder → HERE → TRACK SETUP changes its instrument/type.'),
        ('SELECT A TRACK',['PAD BANK','+','PAD 1–16'],'Hold PAD BANK, tap the track pad, then release. Check the track number in the header.'),
        ('OPEN THE SOUND',['SAMPLE'],'Stage is the selected track’s sound home. Return here from PATTERNS to play its instrument.'),
        ('OPEN PATTERNS',['SEQ','→','B1 PATTERNS'],'SEQ opens SEQUENCES for all tracks. B1 PATTERNS opens the selected track’s patterns.'),
        ('SAVE YOUR PROJECT',['SHIFT','+','PAD 16','→','B1 SAVE'],'First SAVE chooses a folder/name. On Project, SHIFT + B1 is SAVE AS; SHIFT + B3 opens RECOVERY.'),
        ('FINE-EDIT A VALUE',['SHIFT','+','ENCODER TURN'],'On Stage EDIT, touch the target K1–K3 knob first. Shift + encoder turn fine-adjusts that control.'),
        ('GENERATE AN IDEA',['SHIFT','+','B3 GEN'],'From SEQ → B1 PATTERNS. Check the target tracks and SOUND + MIDI / MIDI ONLY before applying.'),
    ]
    for n,(title,keys,body) in enumerate(cards):
        x=32+(n%2)*404;y=282+(n//2)*170;s.box(x,y,392,158,'#131515')
        s.text(f'{n+1:02}',x+14,y+25,10,MID,mono=True);s.text(title,x+38,y+25,12,FG,True)
        at=x+14
        for key in keys:
            if key in ('+','→'):s.text(key,at+2,y+66,13,MID);at+=18
            else:at+=s.key(key,at,y+44)+6
        s.paragraph(body,x+14,y+103,360,12)
    s.line(32,982,828,982)
    for x,title,body in [(32,'RECORD','SAMPLE → SEQ RECORD → PLAY. Play notes on the sound page.'),(306,'UNDO / REDO','SHIFT + − / +. For supported project edits.'),(580,'CUT STUCK AUDIO','STOP twice within 400 ms. SHIFT + STOP sends All Notes Off.')]:
        s.text(title,x,1011,11,FG,True);s.paragraph(body,x,1038,244,12)
    s.paragraph('NOTE REPEAT / ARP can temporarily replace soft keys with rate choices. Turn it off to restore those menus. In a modal, finish or cancel first.',32,1124,790,12)
    s.save(path)

def structure(path):
    s=Sheet(1420,'02 / THE BIG PICTURE')
    s.text('ONE PROJECT. CONNECTED IDEAS.',32,98,10,MID,mono=True)
    s.text('Projects and tracks.',32,158,48,FG,True);s.text('Patterns and audio.',32,216,48,FG,True)
    s.box(32,250,796,118);s.text('PROJECT',52,279,10,MID,mono=True);s.text('Your whole session.',52,313,27,FG,True)
    s.text('Tempo · key / scale · 16 tracks · sequences · arrangement',52,344,13,MID)
    s.text('↓ CHOOSE ONE OF 16 TRACKS',300,400,10,MID,mono=True)
    s.box(32,420,796,88);s.text('TRACK',52,447,10,MID,mono=True);s.text('Sound + patterns + mix',52,480,25,FG,True)
    for n in range(16):
        x=418+(n%8)*49;y=436+(n//8)*31;s.box(x,y,44,25,FG if n==0 else BG);s.text(f'{n+1:02}',x+13,y+17,10,BG if n==0 else MID,mono=True)
    s.text('↓ CHOOSE A TRACK TYPE',267,541,10,MID,mono=True)
    cards=[('PLUGIN','One instrument.','One synth or percussion sound across the keys. Choose its preset; shape it in EDIT.','Rhythmer Kit is a plug-in.','16 pad sounds, whole-kit presets and per-pad source editing.'),
           ('DRUM','Your samples.','Native Sample Kit. Load samples onto pads or import an MPC kit. Trim, chop, tune and shape each pad.','Break starts with a loop.','Load your audio, set 1–4 bars, generate chops and play BRK PERF.'),
           ('KEYGROUP','Across the keys.','One sample or multisampled instrument, played chromatically or in scale. Shape its zones, envelopes and filter.','One instrument, one track.','Load SFZ, XPM, EXS, SoundFont, DecentSampler or a single WAV.')]
    for n,(label,title,body,sub,note) in enumerate(cards):
        x=32+n*270;s.box(x,564,256,258,'#131515');s.text(label,x+14,593,10,MID,mono=True);s.text(title,x+14,626,23,FG,True)
        end=s.paragraph(body,x+14,653,228,12);s.line(x+14,729,x+242,729);s.text(sub,x+14,753,12,FG,True);s.paragraph(note,x+14,778,228,12)
    s.box(32,840,796,70);s.text('MIDI + AUDIO TRACKS',48,865,11,FG,True)
    s.paragraph('MIDI plays external gear through its port/channel. AUDIO records a pattern-length input take. A MIDI track alone makes no sound.',48,886,760,12)
    s.text('↓ NOTES LIVE IN PATTERNS · UP TO 128 PER TRACK',204,942,10,MID,mono=True)
    s.box(32,960,796,96)
    for x,title,body in [(52,'PATTERNS','Build each part'),(330,'SEQUENCES','Launch parts together'),(628,'SONG','Arrange / export')]:
        s.text(title,x,991,13,FG,True);s.text(body,x,1020,12,MID)
    s.text('→',284,1005,24,MID);s.text('→',574,1005,24,MID)
    for x,title,body in [(32,'PRESETS → EDIT','Choose the sound, then shape it. PERFORM holds separate track effects. GEN makes ideas; Acid / Break have their own workflows.'),(436,'MIXER → CHANNELS','Balance levels, sends A–D, eight submixes and effects. Engine effects, channel inserts and bus effects remain separate stages.')]:
        s.text(title,x,1102,19,FG,True);s.paragraph(body,x,1131,370,12)
    s.box(32,1222,796,112,FG,FG)
    s.text('PROJECT SAVE',48,1250,12,BG,True);s.paragraph('B1 SAVE keeps your named file. SHIFT + B1: SAVE AS. Idle autosave updates a separate recovery copy.',48,1275,355,12,BG)
    s.text('USER SOUND / USER KIT',438,1250,12,BG,True);s.paragraph('A reusable instrument or kit preset. Keep the original sample audio too.',438,1275,365,12,BG)
    s.paragraph('SAMPLE opens a track’s sound. SEQ opens SEQUENCES; B1 opens PATTERNS. Menu → HERE → TRACK SETUP changes its role; DELETE TRACK is separate and confirmed.',32,1370,790,12)
    s.save(path)

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output-dir',type=Path,required=True);parser.add_argument('--png',action='store_true');args=parser.parse_args()
    args.output_dir.mkdir(parents=True,exist_ok=True)
    for name,draw in [('essential-controls',controls),('os-structure',structure)]:
        svg=args.output_dir/(name+'.svg');draw(svg)
        if args.png:
            if sys.platform == 'darwin':
                data=svg.read_text();height=int(re.search(r'height="(\d+)"',data)[1])
                square=svg.with_name(svg.stem+'-export.svg')
                square_data=data.replace(f'width="860" height="{height}" viewBox="0 0 860 {height}"',f'width="{height}" height="{height}" viewBox="0 0 {height} {height}"',1)
                at=square_data.index('>')+1
                square_data=square_data[:at]+f'<g transform="translate({(height-860)/2} 0)">'+square_data[at:]
                square.write_text(square_data.replace('</svg>','</g></svg>'))
                subprocess.run(['/usr/bin/qlmanage','-t','-s',str(height),'-o',str(args.output_dir),str(square)],check=True,stdout=subprocess.DEVNULL)
                rendered=Path(str(square)+'.png')
                subprocess.run(['/usr/bin/sips','--cropOffset','0','0','--cropToHeightWidth',str(height),'860',str(rendered),'--out',str(svg.with_suffix('.png'))],check=True,stdout=subprocess.DEVNULL)
            else:
                import cairosvg
                cairosvg.svg2png(url=str(svg),write_to=str(svg.with_suffix('.png')))
    print('Reference sheets rendered from editable vectors')
