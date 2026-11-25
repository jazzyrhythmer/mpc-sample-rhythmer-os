// SPDX-License-Identifier: GPL-3.0-only
// Independent FM-1 control oracle: six_break_key's held priority and release
// calls jungle_roll / jungle_roll_off, without releasing the sampler's gates.
#include "rhythmer/engine/break_manipulator.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
extern "C" {
void jungle_init();int jungle_bind(const uint8_t*,unsigned,unsigned);
void jungle_sequence_mode(unsigned);void jungle_sequence_note(unsigned);void jungle_sequence_off(unsigned);
void jungle_play(unsigned);void jungle_tempo(unsigned);void jungle_tick_mask(uint32_t);void jungle_render(int32_t*,unsigned);
void jungle_roll(unsigned);void jungle_roll_off();
void jungle_feedback(unsigned);void jungle_variation(unsigned);void jungle_warp(unsigned);void jungle_freeze(unsigned);void jungle_straight();void jungle_stretch_grain(unsigned,unsigned);void jungle_stretch_slow(unsigned);void jungle_swap(const uint8_t*);
}
int main(){
 using namespace rhythmer;
 SampleData loop;loop.frames=48000;loop.left.resize(loop.frames);std::vector<uint8_t> packed(72000);
 for(unsigned i=0;i<loop.frames;i+=2){int a=int(1500*std::sin(i*.081)),b=int(1500*std::sin((i+1)*.081));loop.left[i]=a/2048.f;loop.left[i+1]=b/2048.f;unsigned aa=unsigned(a)&4095,bb=unsigned(b)&4095;packed[i/2*3]=aa;packed[i/2*3+1]=(aa>>8)|((bb&15)<<4);packed[i/2*3+2]=bb>>4;}
 SampleData loop_b;loop_b.frames=48000;loop_b.left.resize(loop_b.frames);std::vector<uint8_t> packed_b(72000);
 for(unsigned i=0;i<loop_b.frames;i+=2){int a=int(1200*std::sin(i*.043)),b=int(1200*std::sin((i+1)*.043));loop_b.left[i]=a/2048.f;loop_b.left[i+1]=b/2048.f;unsigned aa=unsigned(a)&4095,bb=unsigned(b)&4095;packed_b[i/2*3]=aa;packed_b[i/2*3+1]=(aa>>8)|((bb&15)<<4);packed_b[i/2*3+2]=bb>>4;}
 double worst=0;unsigned mismatches=0;
 for(unsigned mode=0;mode<4;mode++)for(unsigned roll=0;roll<16;roll++){
  BreakManipulator m;m.configure(2,0);m.tempo(1200);m.sources(&loop,&loop_b);m.transport(true,0);m.note(36,127);
  jungle_init();jungle_bind(packed.data(),packed.size(),48000);jungle_sequence_mode(1);jungle_tempo(1200);jungle_play(1);jungle_sequence_note(0);
  m.param(9,mode&1);jungle_sequence_mode(mode&1);
  if(mode&2){m.param(0,41);jungle_feedback(41);m.param(1,64);jungle_variation(64);m.param(2,19);jungle_warp(19);m.param(6,21);m.param(7,80);jungle_stretch_grain(21,80);m.param(8,3);jungle_stretch_slow(3);}
  double ticks=0;float previous=0;
  for(unsigned block=0;block<900;block++){
   if(block==100){m.live_note(roll,127);jungle_roll(roll);}
   if(block==150){m.live_note((roll+1)%16,100);jungle_roll((roll+1)%16);}
   if(block==200){m.live_off((roll+1)%16);jungle_roll(roll);}
   if(block==450){m.live_off(roll);jungle_roll_off();}
   if(block==550){m.note(17,1);jungle_swap(packed_b.data());}
   if(block==600){m.note(16,127);jungle_swap(packed.data());}
   if(block==650){m.note(18,1);jungle_variation(0);jungle_warp(0);jungle_straight();}
   if(block==500){m.note(37,127);jungle_sequence_note(1);}
   uint32_t mask=0;for(unsigned i=0;i<32;i++){ticks+=1200*4*48;if(ticks>=44100.0*600){ticks-=44100.0*600;mask|=uint32_t(1)<<i;}}
   int32_t expected[64]{};jungle_tick_mask(mask);jungle_render(expected,32);float l[32]{},r[32]{};m.render(l,r,32,44100);
   for(unsigned i=0;i<32;i++){double difference=std::abs(l[i]-previous);worst=std::max(worst,difference);if(!std::isfinite(l[i])||difference>.00001)++mismatches;previous=expected[2*i]/8388608.f;}
  }
 }
 std::printf("BREAK_LIVE_FM1_ORACLE effects=16 control_modes=4 hold_release,source_swap,return,grain,warp=checked sample_mismatches=%u max_error=%.9f\n",mismatches,worst);return mismatches?1:0;
}
