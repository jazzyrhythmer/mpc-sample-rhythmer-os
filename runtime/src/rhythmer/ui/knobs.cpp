// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#include "rhythmer/ui/knobs.h"
#include "rhythmer/ui/glyphs.h"
#include <algorithm>

namespace rhythmer::ui {

namespace {
float pixel_number(Paint& p,const juce::String& value,float x,float y,float room,juce::Colour ink) {
    static const char* glyphs[] {"01110100011001110101110011000101110","00100011000010000100001000010001110","01110100010000100010001000100011111","11110000010000101110000010000111110","00010001100101010010111110001000010","11111100001000011110000010000111110","01110100001000011110100011000101110","11111000010001000100010000100001000","01110100011000101110100011000101110","01110100011000101111000010000101110","00000000000000011111000000000000000","00000000000000000000000000010000100"};
    if(value.isEmpty()||value.length()*12-2>room)return -1;
    for(auto c:value)if(!((c>='0'&&c<='9')||c=='-'||c=='.'))return -1;
    unsigned col=0;
    for(auto c:value) {
        const char* bits=glyphs[c=='-'?10:c=='.'?11:c-'0'];
        for(unsigned yy=0;yy<7;yy++)for(unsigned xx=0;xx<5;xx++)
            if(bits[yy*5+xx]=='1')p.fill(x+col*12+xx*2,y+yy*2,2,2,ink);
        ++col;
    }
    return float(col*12-2);
}
}


// The Shift icon: an up arrow 7 px wide, hollow, or filled while Shift is held.
void shift_mark(Paint& p, float x, float y, juce::Colour c, bool filled) {
    juce::Path arrow;
    arrow.startNewSubPath(x + 3.5f, y + 0.5f);
    arrow.lineTo(x + 7, y + 4); arrow.lineTo(x + 5, y + 4); arrow.lineTo(x + 5, y + 7.5f);
    arrow.lineTo(x + 2, y + 7.5f); arrow.lineTo(x + 2, y + 4); arrow.lineTo(x, y + 4);
    arrow.closeSubPath();
    p.g.setColour(c);
    if (filled) p.g.fillPath(arrow);
    else p.g.strokePath(arrow, juce::PathStrokeType(1));
}

void knob_strip(Paint& p, const std::array<KnobCard, 3>& cards) {
    if (auto* seen = controls_sink()) { // the preview's controls audit: a pot with no job here has no label
        seen->knobs_drawn = true;
        for (size_t k = 0; k < 3; ++k) seen->knobs[k] = cards[k].state == KnobCard::State::Idle ? juce::String() : cards[k].label;
    }
    // 3 px above, 3 below, 6 px at the sides, 4 px between: 100 x 46 cards.
    constexpr float top = zone::knobs_y + 3, h = 46, gap = 4;
    constexpr float w = (zone::width - 2 * zone::pad - 2 * gap) / 3.0f;
    for (size_t n = 0; n < cards.size(); ++n) {
        const auto& c = cards[n];
        float x = zone::pad + n * (w + gap);
        using State = KnobCard::State;
        if (c.state == State::Idle) continue; // a pot with no job draws nothing: no empty box
        if (c.state == State::Turning) {
            p.fill(x, top, w, h, colour::bg, 3);
            p.fill(x, top, w, h, c.track.withAlpha(0x38 / 255.0f), 3);
            p.ring(x, top, w, h, c.shade, 3);
        } else {
            p.fill(x, top, w, h, colour::surf, 3);
            if (c.focus && c.state == State::Rest) p.ring(x + 0.5f, top + 0.5f, w - 1, h - 1, c.shade.withAlpha(0.6f), 3); // the wheel's card
        }
        float inner = x + 6, iw = w - 12;
        float lw = iw - (c.shift ? 9 : 0) - (c.knob_fx ? 12 : 0) - (c.fader ? 12 : 0);
        // A plug-in's own name: capitals where they fit, else mixed case (narrower, as on Stage), else mixed case over
        // two lines with the value a size smaller beneath, so no name is cut.
        auto mixed = plain(c.shade, type::small, Weight::SemiBold);
        juce::StringArray two;
        if (c.label_mixed.isNotEmpty() && p.width(c.label, caps_text(c.shade)) > lw && p.width(c.label_mixed, mixed) > lw) {
            auto style = mixed;
            two = p.wrap(c.label_mixed, style, lw, 2);
        }
        if (two.size() == 2) {
            p.text(two[0], inner, top + 1, lw, 12, mixed);
            p.text(two[1], inner, top + 12, lw, 12, mixed);
        } else if (c.label_mixed.isNotEmpty() && p.width(c.label, caps_text(c.shade)) > lw) {
            p.text(c.label_mixed, inner, top + 3, lw, 13, mixed);
        } else {
            p.text(c.label, inner, top + 3, lw, 13, caps_text(c.shade));
        }
        if (c.shift) shift_mark(p, x + w - 12, top + 5.5f, c.shift == 2 ? c.shade : colour::mid, c.shift == 2);
        if (c.knob_fx) icon(p, Icon::Knob, x + w - (c.shift ? 24 : 15), top + 4, 10, colour::text, 1.4f);
        if (c.fader) // left of the Shift icon and the knob glyph; mid until the fader reaches the value (takeover)
            icon(p, Icon::Fader, x + w - 15 - (c.shift ? 9 : 0) - (c.knob_fx ? 12 : 0), top + 4, 10,
                 c.state == State::Pickup ? colour::mid : c.shade, 1.4f);
        bool low = two.size() == 2; // under a two-line name the value sits lower and a size smaller
        auto value = plain(c.state == State::Pickup ? colour::dim : colour::text, low ? type::list : type::value, Weight::Medium);
        auto unit = plain(colour::mid, type::body);
        float room = iw - (c.unit.isNotEmpty() ? p.width(c.unit, unit) + 3 : 0.0f);
        auto shown = p.fit(c.value, value, room, type::floor); // a long name shrinks to fit
        float vw = low?-1:pixel_number(p,c.value,inner,top+19,room,value.colour);
        bool tall_value=false;
        if(vw<0&&!low&&p.width(c.value,plain(value.colour,type::floor,Weight::Medium))>room) {
            auto style=plain(value.colour,type::floor,Weight::Medium);auto lines=p.wrap(c.value,style,room,3);tall_value=lines.size()>2;float line_h=tall_value?11.0f:12.0f;
            for(int i=0;i<lines.size();++i)p.text(lines[i],inner,top+13+i*line_h,room,line_h,style);
            vw=room;
        }
        if(vw<0) vw = p.text(c.value, inner, low ? top + 23 : top + 15, room, low ? 16 : 22, shown);
        if (c.unit.isNotEmpty())
            p.text(c.unit, inner + vw + 3, low ? top + 24 : top + 18, iw - vw - 3, low ? 15 : 20, unit);
        // The gauge: a 2 px track with the value filled in the knob's shade.
        float gy = top + (tall_value?48:40), fill = std::clamp(c.fill, 0.0f, 1.0f);
        // Sixteen discrete pixels: numeric values remain exact in the card above.
        for(unsigned cell=0;cell<16;cell++) {
            float at=(cell+0.5f)/16;
            bool lit=c.bipolar?(at>=std::min(0.5f,fill)&&at<=std::max(0.5f,fill)):at<fill;
            p.fill(inner+cell*iw/16,gy,3,2,lit?colour::text:colour::line);
        }
        if (c.state == State::Pickup && c.pot >= 0) {
            float cx = inner + std::clamp(c.pot, 0.0f, 1.0f) * iw;
            p.circle(cx, gy + 1, 3, colour::text);
            p.circle(cx, gy + 1, 2, colour::surf);
        }
        if (c.frozen) { // freeze plan F2c: held by a print; turning it asks to unfreeze first
            p.fill(x, top, w, h, colour::bg.withAlpha(0.55f), 3);
            icon(p, Icon::Snow, x + w - 16 - (c.shift ? 10 : 0), top + 4.5f, 10, colour::ice, 1.4f);
        }
    }
}

} // namespace rhythmer::ui
