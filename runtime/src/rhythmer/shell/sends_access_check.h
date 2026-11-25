// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "rhythmer/engine/engine_host.h"
#include "rhythmer/shell/shell.h"
#include "rhythmer/shell/ask.h"
#include "rhythmer/pages/pages.h"
#include "rhythmer/model/project_file.h"
#include <functional>
#include <iostream>
namespace rhythmer {
inline int sends_access_check(const std::function<void(Shell&,const std::string&)>& capture={}) {
 auto host=std::make_unique<EngineHost>();auto shell=std::make_unique<Shell>(*host);auto& c=shell->context();unsigned checks=0;uint32_t now=10000;
#define SEND_CHECK(ok,msg) do{++checks;if(!(ok)){std::cerr<<"SENDS ACCESS FAIL "<<checks<<": "<<msg<<'\n';return 1;}}while(false)
 auto button=[&](SurfaceButton b,bool down){SurfaceEvent e;e.input.kind=SurfaceInput::Kind::Button;e.input.index=uint8_t(b);e.input.pressed=down;shell->surface(e,++now);};
 auto tap=[&](SurfaceButton b){button(b,true);button(b,false);};
 for(auto engine:{EngineKind::None,EngineKind::Sampler,EngineKind::Keygroup,EngineKind::Plugin,EngineKind::Midi,EngineKind::Audio}){
  c.ui=UiState{};c.ui.settings.full_features=false;c.project.tracks[0]=Track{};load_sound(c.project.tracks[0],engine,0);c.open(PageId::Mixer);c.edits.clear();c.dirty=false;
  const auto original=save_project(shell->project(),shell->patterns());
  auto cards=page_bindings(c.project,c.ui,c.pattern());
  SEND_CHECK(cards[0].bound()==c.track().used()&&!cards[1].bound()&&!cards[2].bound(),"Basic mixer retains send A on used tracks; empty strip has no controls");
  tap(SurfaceButton::TopLeft);SEND_CHECK(c.ui.ask==UiState::Ask::EnableFullSends&&!c.ui.settings.full_features,"B1 SENDS offers Full without changing a project");
  tap(SurfaceButton::TopLeft);SEND_CHECK(c.ui.ask==UiState::Ask::None&&!c.ui.settings.full_features,"Cancel retains Basic");
  tap(SurfaceButton::TopLeft);button(SurfaceButton::Shift,true);tap(SurfaceButton::WheelPush);button(SurfaceButton::Shift,false);
  SEND_CHECK(c.ui.ask==UiState::Ask::None&&!c.ui.settings.full_features,"Shift push cancels");
  tap(SurfaceButton::TopLeft);tap(SurfaceButton::TopRight);
  SEND_CHECK(c.ui.settings.full_features&&c.ui.ask==UiState::Ask::None&&c.ui.page==PageId::Mixer,"Enable opens native mixer Sends");
  SEND_CHECK(original==save_project(shell->project(),shell->patterns()),"Mode switch leaves sound and patterns untouched");
  cards=page_bindings(c.project,c.ui,c.pattern());
  SEND_CHECK(cards[0].bound()==c.track().used()&&cards[1].bound()==c.track().used()&&cards[2].bound()==c.track().used(),"A B C are reachable on used tracks");
  c.ui.mixer_row=1;cards=page_bindings(c.project,c.ui,c.pattern());SEND_CHECK(cards[2].bound()==c.track().used(),"D is reachable on second row on used tracks");
  c.ui.mixer_row=0;tap(SurfaceButton::TopLeft);SEND_CHECK(c.ui.mixer_b1==1,"Full B1 still cycles to FX");
  tap(SurfaceButton::TopLeft);SEND_CHECK(c.ui.mixer_b1==0,"Full B1 cycles back to Sends");
  if(capture)capture(*shell,"mixer-sends-"+std::to_string(int(engine)));
 }
 c.ui=UiState{};c.open(PageId::Tracks);SEND_CHECK(c.ui.page==PageId::Mixer,"Legacy Tracks bookmark opens mixer");
 c.open(PageId::Sends);SEND_CHECK(c.ui.page==PageId::Mixer,"Legacy Sends bookmark opens mixer");
 std::cout<<"SENDS_ACCESS_OK checks="<<checks<<" native v0.8 mixer A-D, basic doorway, cancel/back, project preservation\n";
#undef SEND_CHECK
 return 0;
}
}
